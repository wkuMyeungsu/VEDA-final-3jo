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

constexpr qint64 kStaleThresholdMs = 1000; // - 오래된 메시지 기준(1초)

constexpr int kServerHeartbeatMs = 200;    // - 서버 하트비트 주기(ms)
constexpr int kWatchdogMultiplier = 3;     // - 워치독 배수
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

    m_lastMessageTimer.invalidate(); // - 워치독 기준 시각 초기화
    m_watchdogTimer.start();         // - 워치독 시작
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

    const QDateTime utcTime = metadata.utcTime(); // - 시간 정보 추출
    if (!utcTime.isValid()) { // - 시각 검증
        qCWarning(lcRiskEventSource) << "utc_time missing/unparseable, treating as fresh:" << payload; // - 경고 로그
        m_lastCameraId = metadata.cameraId(); // - 카메라 ID 기록
        m_lastMessageTimer.start();           // - 워치독 기준 시각 갱신
        emit metadataReceived(metadata);      // - 신호 발생
        return;
    }

    const qint64 ageMs = utcTime.msecsTo(QDateTime::currentDateTimeUtc()); // - 경과 시간 계산
    if (ageMs > kStaleThresholdMs) { // - 오래된 값 검증
        qCInfo(lcRiskEventSource) << "discarding stale retained message, age(ms)=" << ageMs
                                   << "utc_time=" << utcTime.toString(Qt::ISODateWithMs); // - 정보 로그
        return; // - 데이터 폐기
    }

    m_lastCameraId = metadata.cameraId(); // - 카메라 ID 기록
    m_lastMessageTimer.start();           // - 워치독 기준 시각 갱신
    emit metadataReceived(metadata);      // - 신호 발생
}

void RiskEventSource::handleWatchdogTimeout()
{
    if (!m_lastMessageTimer.isValid() || m_lastMessageTimer.elapsed() < kWatchdogThresholdMs) // - 정상 범위 판단
        return;

    qCWarning(lcRiskEventSource) << "no data for" << kWatchdogThresholdMs << "ms, reporting NetworkDisconnected"; // - 경고 로그

    RiskMetadata metadata; // - 임시 메타데이터 생성
    metadata.setCameraId(m_lastCameraId);                                       // - 카메라 ID 유지
    metadata.setExceptionState(RiskTypes::ExceptionState::NetworkDisconnected); // - 예외 상태 설정
    metadata.setUtcTime(QDateTime::currentDateTimeUtc());                       // - 시각 설정
    emit metadataReceived(metadata); // - 신호 발생

    m_lastMessageTimer.start(); // - 기준 시각 재설정
}
