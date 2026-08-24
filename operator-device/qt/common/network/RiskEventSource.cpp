#include "RiskEventSource.h"

// - 클라이언트 전용 헤더 사용 (<mosquitto.h>는 Windows에서 브로커 헤더까지 끌고 와 빌드 깨짐)
#include <mosquitto/libmosquitto.h>

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QMetaObject>

namespace {
Q_LOGGING_CATEGORY(lcRiskEventSource, "safety.risk.mqtt") // - 로그 카테고리

constexpr int kReconnectDelayBaseSec = 3;  // - 재연결 시작 대기(초)
constexpr int kReconnectDelayMaxSec = 30;  // - 재연결 최대 대기(초)
constexpr int kMqttKeepAliveSec = 60;      // - MQTT keepalive 주기(초)

constexpr qint64 kStaleThresholdMs = 1000; // - retain 메시지 허용 나이(1초): 접속 직후 첫 1건에만 적용된다(processPayload 참고)
constexpr int kStartupGracePeriodMs = 5000; // - 기동 유예 시간 (5초)

constexpr int kServerHeartbeatMs = 200;    // - 서버 하트비트 주기(ms)
constexpr int kWatchdogMultiplier = 5;     // - 워치독 배수 (5 * 200ms = 1000ms)
constexpr int kWatchdogThresholdMs = kServerHeartbeatMs * kWatchdogMultiplier; // - 워치독 만료 시간(ms)
constexpr int kWatchdogPollIntervalMs = 100; // - 워치독 폴링 주기(ms)
}

RiskEventSource::RiskEventSource(QString brokerHost, quint16 brokerPort, QString terminalId, QObject *parent)
    : IMetadataSource(parent)
    , m_brokerHost(std::move(brokerHost))                           // - 브로커 주소 저장
    , m_brokerPort(brokerPort)                                      // - 브로커 포트 저장
    , m_terminalId(std::move(terminalId))                           // - 단말 ID 저장
    , m_topic(QStringLiteral("forklift/risk/%1").arg(m_terminalId)) // - 구독 토픽 구성
{
    mosquitto_lib_init(); // - mosquitto 라이브러리 초기화

    m_watchdogTimer.setInterval(kWatchdogPollIntervalMs); // - 워치독 폴링 간격 설정
    connect(&m_watchdogTimer, &QTimer::timeout, this, &RiskEventSource::handleWatchdogTimeout); // - 워치독 타이머 연결
}

RiskEventSource::~RiskEventSource()
{
    stop();                  // - 정리
    mosquitto_lib_cleanup(); // - mosquitto 라이브러리 정리
}

void RiskEventSource::start()
{
    if (m_mosq) // - 중복 시작 방지
        return;

    setConnectionState(RiskTypes::ConnectionState::Connecting); // - 상태 변경: 연결 중

    // - 관제 센터는 단말 앱과 안 겹치게 접두어 구분
    const QByteArray clientId = (QStringLiteral("control-center-") + m_terminalId).toUtf8(); // - 클라이언트 ID 구성
    m_mosq = mosquitto_new(clientId.constData(), true, this); // - mosquitto 클라이언트 생성

    mosquitto_reconnect_delay_set(m_mosq, kReconnectDelayBaseSec, kReconnectDelayMaxSec, true); // - 재연결 백오프 설정

    mosquitto_connect_callback_set(m_mosq, &RiskEventSource::onConnect);       // - 접속 콜백 등록
    mosquitto_disconnect_callback_set(m_mosq, &RiskEventSource::onDisconnect); // - 연결 끊김 콜백 등록
    mosquitto_message_callback_set(m_mosq, &RiskEventSource::onMessage);       // - 메시지 콜백 등록

    mosquitto_connect_async(m_mosq, m_brokerHost.toUtf8().constData(), m_brokerPort, kMqttKeepAliveSec); // - 비동기 접속 시도
    mosquitto_loop_start(m_mosq); // - 네트워크 스레드 시작

    m_hasReceivedFirstMessage = false;
    m_staleCheckArmed = false;   // - 아직 접속 전이라 retain 메시지가 올 수 없다
    m_lastMessageTimer.start();   // - 기동 시점부터 타이머 시작 (첫 기동 시 5초 유예 적용)
    m_watchdogTimer.start();      // - 워치독 시작
}

void RiskEventSource::stop()
{
    m_watchdogTimer.stop(); // - 워치독 정지
    if (m_mosq) {
        mosquitto_disconnect(m_mosq);      // - 연결 해제
        mosquitto_loop_stop(m_mosq, true); // - 네트워크 스레드 정지
        mosquitto_destroy(m_mosq);         // - 핸들 해제
        m_mosq = nullptr;
    }
    setConnectionState(RiskTypes::ConnectionState::Disconnected); // - 상태 변경: 연결 끊김
}

void RiskEventSource::onConnect(struct mosquitto *mosq, void *obj, int rc)
{
    auto *self = static_cast<RiskEventSource *>(obj);

    if (rc == 0) // - 접속 성공 시 구독
        mosquitto_subscribe(mosq, nullptr, self->m_topic.toUtf8().constData(), 0);

    QMetaObject::invokeMethod(self, [self, rc]() { // - 메인 스레드로 위임
        if (rc != 0) {
            // - Windows에선 mosquitto_connack_string() 못 씀 (파일 상단 헤더 설명 참고)
            qCWarning(lcRiskEventSource) << "connect failed, rc=" << rc; // - 경고 로그
            return;
        }
        qCInfo(lcRiskEventSource) << "connected to broker, subscribed to" << self->m_topic; // - 정보 로그
        self->setConnectionState(RiskTypes::ConnectionState::Connected); // - 상태 변경: 연결됨
        self->m_lastMessageTimer.start(); // - 워치독 기준 시각 갱신
        self->m_staleCheckArmed = true;   // - 구독 직후 도착할 retain 메시지 1건만 나이를 검사한다
    }, Qt::QueuedConnection);
}

void RiskEventSource::onDisconnect(struct mosquitto *mosq, void *obj, int rc)
{
    Q_UNUSED(mosq);
    auto *self = static_cast<RiskEventSource *>(obj);

    QMetaObject::invokeMethod(self, [self, rc]() {
        qCWarning(lcRiskEventSource) << "disconnected from broker, rc=" << rc; // - 경고 로그
        self->setConnectionState(RiskTypes::ConnectionState::Disconnected); // - 상태 변경: 연결 끊김
    }, Qt::QueuedConnection);
}

void RiskEventSource::onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    Q_UNUSED(mosq);
    if (!message || !message->payload || message->payloadlen <= 0)
        return;

    auto *self = static_cast<RiskEventSource *>(obj);
    const QByteArray payload(static_cast<const char *>(message->payload), message->payloadlen); // - 페이로드 복사

    QMetaObject::invokeMethod(self, [self, payload]() { // - 메인 스레드에서 처리
        self->processPayload(payload);
    }, Qt::QueuedConnection);
}

void RiskEventSource::processPayload(const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError); // - JSON 파싱

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) { // - 유효성 검증
        qCWarning(lcRiskEventSource) << "malformed payload, ignoring:" << parseError.errorString() << payload; // - 경고 로그
        return;
    }

    const RiskMetadata metadata = RiskMetadata::fromJson(doc.object()); // - 데이터 변환

    // - stale 검사는 접속 직후 retain 메시지 1건만 대상으로 한다. 아래 어느 경로로
    //   빠져나가든 여기서 소진하므로, 두 번째 메시지부터는 나이를 보지 않는다.
    const bool checkStale = m_staleCheckArmed;
    m_staleCheckArmed = false;

    const QDateTime utcTime = metadata.utcTime(); // - 시간 정보 추출
    if (!utcTime.isValid()) { // - 시각 검증
        qCWarning(lcRiskEventSource) << "utc_time missing/unparseable, treating as fresh:" << payload; // - 경고 로그
        m_hasReceivedFirstMessage = true;
        m_lastMessageTimer.start();           // - 워치독 기준 시각 갱신
        emit metadataReceived(metadata);      // - 신호 발생
        return;
    }

    const qint64 ageMs = utcTime.msecsTo(QDateTime::currentDateTimeUtc()); // - 경과 시간 계산
    if (ageMs < 0) {
        qCWarning(lcRiskEventSource) << "utc_time is in the future (NTP skew?), accepting:" << utcTime.toString(Qt::ISODateWithMs);
    } else if (checkStale && ageMs > kStaleThresholdMs) {
        // - 접속 직후 첫 메시지가 오래됨 = 서버가 멈춘 뒤 브로커에 남아 있던 retain 값일 가능성.
        //   접속당 최대 1회만 찍히므로 경고 수준으로 남긴다. 매 접속마다 반복된다면
        //   메시지가 진짜 오래된 게 아니라 단말 시계가 뒤처진 것을 의심해야 한다.
        qCWarning(lcRiskEventSource) << "discarding stale retained message on connect, age(ms)=" << ageMs
                                      << "utc_time=" << utcTime.toString(Qt::ISODateWithMs)
                                      << "-- if this repeats every connect, check clock sync (chronyc tracking)";
        return; // - 데이터 폐기
    }

    m_hasReceivedFirstMessage = true;
    m_lastMessageTimer.start();           // - 워치독 기준 시각 갱신
    emit metadataReceived(metadata);      // - 신호 발생
}

void RiskEventSource::handleWatchdogTimeout()
{
    const int threshold = m_hasReceivedFirstMessage ? kWatchdogThresholdMs : kStartupGracePeriodMs;
    if (!m_lastMessageTimer.isValid() || m_lastMessageTimer.elapsed() < threshold)
        return;

    qCWarning(lcRiskEventSource) << "no data for" << threshold << "ms, reporting linkLost";

    m_hasReceivedFirstMessage = true;
    emit linkLost(); // - MetadataDistributor가 전 채널로 팬아웃

    m_lastMessageTimer.start(); // - 기준 시각 재설정
}
