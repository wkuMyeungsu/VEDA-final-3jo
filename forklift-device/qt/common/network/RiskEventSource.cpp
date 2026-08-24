#include "RiskEventSource.h"
#include "services/LatencyTracker.h"

#if __has_include(<mosquitto/libmosquitto.h>)
#include <mosquitto/libmosquitto.h>
#else
#include <mosquitto.h>
#endif

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QMetaObject>

namespace {
Q_LOGGING_CATEGORY(lcRiskEventSource, "safety.risk.mqtt")                // - 로깅 카테고리 정의: MQTT 기반 위험 이벤트 통신 로그 분류용 이름 지정

constexpr int kReconnectDelayBaseSec = 3;                                 // - 재연결 시작 대기 시간(초): 기존 kReconnectBaseDelayMs(3초)와 동일
constexpr int kReconnectDelayMaxSec = 30;                                 // - 재연결 최대 대기 시간(초): 기존 kReconnectMaxDelayMs(30초)와 동일, 지수 백오프 상한
constexpr int kMqttKeepAliveSec = 60;                                     // - MQTT keepalive 주기(초): PINGREQ 전송 간격

constexpr qint64 kStaleThresholdMs = 1000;                                // - retain 메시지 허용 나이(1초): 접속 직후 첫 1건에만 적용된다(processPayload 참고)
constexpr int kStartupGracePeriodMs = 5000;                               // - 기동 유예 시간 (5초): 부팅 직후 네트워크 접속 및 서버 기동 대기 시간

// - 서버 publish 주기(현재 200ms, 2026-08-03 정책 확정)의 5배
constexpr int kServerHeartbeatMs = 200;                                   // - 서버 하트비트 주기: result_dispatcher.hpp의 확정값과 동일하게 유지
constexpr int kWatchdogMultiplier = 5;                                    // - 워치독 배수: 하트비트 몇 배 무수신 시 끊김으로 볼지 (5 * 200ms = 1000ms)
constexpr int kWatchdogThresholdMs = kServerHeartbeatMs * kWatchdogMultiplier; // - 워치독 만료 시간: 이 시간 동안 무수신이면 linkLost 신호 전달
constexpr int kWatchdogPollIntervalMs = 100;                              // - 워치독 폴링 주기: MQTT keepalive는 "연결 자체가 끊긴 경우"만 감지하므로,
                                                                           //   "연결은 살아있는데 publish가 멈춘 경우"를 잡기 위해 별도로 100ms마다 점검
}

RiskEventSource::RiskEventSource(QString brokerHost, quint16 brokerPort, QString terminalId, QObject *parent)
    : IMetadataSource(parent)
    , m_brokerHost(std::move(brokerHost))                                 // - 브로커 주소 저장
    , m_brokerPort(brokerPort)                                            // - 브로커 포트 저장
    , m_terminalId(std::move(terminalId))                                 // - 단말 ID 저장
    , m_topic(QStringLiteral("forklift/risk/%1").arg(m_terminalId))       // - 구독 토픽 구성: forklift/risk/{terminal_id}
{
    mosquitto_lib_init();                                                 // - mosquitto 라이브러리 초기화 (프로세스 내 인스턴스가 하나뿐이라는 전제 하에 생성자/소멸자에서 짝 호출)

    m_watchdogTimer.setInterval(kWatchdogPollIntervalMs);                 // - 워치독 폴링 간격 설정: 100ms
    connect(&m_watchdogTimer, &QTimer::timeout, this, &RiskEventSource::handleWatchdogTimeout); // - 워치독 폴링 이벤트 연결
}

RiskEventSource::~RiskEventSource()
{
    stop();                                                               // - mosquitto 핸들 및 워치독 정리
    mosquitto_lib_cleanup();                                              // - mosquitto 라이브러리 정리
}

void RiskEventSource::start()
{
    if (m_mosq)                                                          // - 중복 시작 방지: 이미 실행 중이면 무시
        return;

    setConnectionState(RiskTypes::ConnectionState::Connecting);          // - 상태 변경: 연결 진행 중 상태로 설정

    const QByteArray clientId = (QStringLiteral("forklift-") + m_terminalId).toUtf8(); // - 클라이언트 ID 구성: 단말 ID 기반 식별자
    m_mosq = mosquitto_new(clientId.constData(), true, this);             // - mosquitto 클라이언트 생성: clean session, userdata로 this 전달(콜백에서 사용)

    mosquitto_reconnect_delay_set(m_mosq, kReconnectDelayBaseSec, kReconnectDelayMaxSec, true); // - 재연결 백오프 설정: 3초 시작, 최대 30초, 지수 증가

    mosquitto_connect_callback_set(m_mosq, &RiskEventSource::onConnect);       // - 접속 콜백 등록
    mosquitto_disconnect_callback_set(m_mosq, &RiskEventSource::onDisconnect); // - 연결 끊김 콜백 등록
    mosquitto_message_callback_set(m_mosq, &RiskEventSource::onMessage);       // - 메시지 콜백 등록

    mosquitto_connect_async(m_mosq, m_brokerHost.toUtf8().constData(), m_brokerPort, kMqttKeepAliveSec); // - 비동기 접속 시도
    mosquitto_loop_start(m_mosq);                                        // - 네트워크 스레드 시작: 이후 접속/재접속/수신을 내부 스레드가 처리

    m_hasReceivedFirstMessage = false;
    m_staleCheckArmed = false;                                           // - 아직 접속 전이라 retain 메시지가 올 수 없다
    m_lastMessageTimer.start();                                          // - 기동 시점부터 타이머 시작 (첫 기동 시 5초 유예 적용)
    m_watchdogTimer.start();                                             // - 워치독 폴링 시작
}

void RiskEventSource::stop()
{
    m_watchdogTimer.stop();                                              // - 워치독 정지: 의도적 종료는 무수신 경고 대상 아님
    if (m_mosq) {
        mosquitto_disconnect(m_mosq);                                    // - 연결 해제 요청
        mosquitto_loop_stop(m_mosq, true);                               // - 네트워크 스레드 정지
        mosquitto_destroy(m_mosq);                                       // - 클라이언트 핸들 해제
        m_mosq = nullptr;
    }
    setConnectionState(RiskTypes::ConnectionState::Disconnected);        // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
}

void RiskEventSource::onConnect(struct mosquitto *mosq, void *obj, int rc)
{
    auto *self = static_cast<RiskEventSource *>(obj);

    if (rc == 0)                                                         // - 접속 성공 시에만 구독: mosquitto_subscribe는 mosq 핸들만 사용하므로 라이브러리 스레드에서 바로 호출해도 안전
        mosquitto_subscribe(mosq, nullptr, self->m_topic.toUtf8().constData(), 0);

    QMetaObject::invokeMethod(self, [self, rc]() {                       // - Qt 상태(연결 상태, 워치독 기준 시각) 갱신은 메인 스레드로 위임
        if (rc != 0) {
            qCWarning(lcRiskEventSource) << "connect failed, rc=" << rc;
            return;
        }
        qCInfo(lcRiskEventSource) << "connected to broker, subscribed to" << self->m_topic; // - 정보 로그: 접속 및 구독 완료 기록
        self->setConnectionState(RiskTypes::ConnectionState::Connected); // - 상태 갱신: 연결 상태를 '연결됨'으로 변경
        self->m_lastMessageTimer.start();                                // - 워치독 기준 시각 재설정: 접속 시점부터 무수신 시간 감시
        self->m_staleCheckArmed = true;                                  // - 구독 직후 도착할 retain 메시지 1건만 나이를 검사한다
    }, Qt::QueuedConnection);
}

void RiskEventSource::onDisconnect(struct mosquitto *mosq, void *obj, int rc)
{
    Q_UNUSED(mosq);
    auto *self = static_cast<RiskEventSource *>(obj);

    QMetaObject::invokeMethod(self, [self, rc]() {
        qCWarning(lcRiskEventSource) << "disconnected from broker, rc=" << rc; // - 경고 로그: 연결 끊김 원인 기록
        self->setConnectionState(RiskTypes::ConnectionState::Disconnected); // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경 (재연결은 라이브러리가 자동 처리)
    }, Qt::QueuedConnection);
}

void RiskEventSource::onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    Q_UNUSED(mosq);
    if (!message || !message->payload || message->payloadlen <= 0)
        return;

    auto *self = static_cast<RiskEventSource *>(obj);
    const QByteArray payload(static_cast<const char *>(message->payload), message->payloadlen); // - 페이로드 즉시 복사: 콜백 반환 후 message는 무효화되므로 라이브러리 스레드에서 복사해 둠

    QMetaObject::invokeMethod(self, [self, payload]() {                 // - 파싱 및 신호 emit은 메인 스레드에서 처리
        self->processPayload(payload);
    }, Qt::QueuedConnection);
}

void RiskEventSource::processPayload(const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError); // - JSON 변환: 수신 페이로드 해석 및 데이터 파싱

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) { // - 유효성 검증: JSON 형식 에러 또는 객체가 아닌 경우 필터링
        qCWarning(lcRiskEventSource) << "malformed payload, ignoring:" << parseError.errorString() << payload; // - 경고 로그: 손상된 데이터 기록
        return;
    }

    const RiskMetadata metadata = RiskMetadata::fromJson(doc.object());   // - 데이터 객체 생성: JSON을 RiskMetadata로 변환

    // - stale 검사는 접속 직후 retain 메시지 1건만 대상으로 한다. 아래 어느 경로로
    //   빠져나가든 여기서 소진하므로, 두 번째 메시지부터는 나이를 보지 않는다.
    const bool checkStale = m_staleCheckArmed;
    m_staleCheckArmed = false;

    const QDateTime utcTime = metadata.utcTime();                         // - 시간 정보 추출: 메시지 생성 시각 확인
    if (!utcTime.isValid()) {                                             // - 시각 검증: 시간 정보가 없거나 유효하지 않은 경우 처리
        qCWarning(lcRiskEventSource) << "utc_time missing/unparseable, treating as fresh:" << payload; // - 경고 로그: 시간 정보 없음 기록
        m_hasReceivedFirstMessage = true;
        m_lastMessageTimer.start();                                       // - 워치독 기준 시각 재설정: 데이터가 도착했으니 무수신 시계 초기화
        emit metadataReceived(metadata);                                  // - 신호 발생: 시간 확인 불가 데이터 전달 처리
        return;
    }

    const qint64 ageMs = utcTime.msecsTo(QDateTime::currentDateTimeUtc());// - 경과 시간 계산: 서버가 찍은 시각과 단말 시계의 차이
    if (ageMs < 0) {
        // - 단말 시계가 서버보다 앞선 경우(NTP 미동기 등): 폐기하지 않고 수용
        qCWarning(lcRiskEventSource) << "utc_time is in the future (NTP skew?), accepting:"
                                      << utcTime.toString(Qt::ISODateWithMs);
    } else if (checkStale && ageMs > kStaleThresholdMs) {
        // - 접속 직후 첫 메시지가 오래됨 = 서버가 멈춘 뒤 브로커에 남아 있던 retain 값일 가능성.
        //   접속당 최대 1회만 찍히므로 경고 수준으로 남긴다. 매 접속마다 반복된다면
        //   메시지가 진짜 오래된 게 아니라 단말 시계가 뒤처진 것을 의심해야 한다.
        qCWarning(lcRiskEventSource) << "discarding stale retained message on connect, age(ms)=" << ageMs
                                      << "utc_time=" << utcTime.toString(Qt::ISODateWithMs)
                                      << "-- if this repeats every connect, check clock sync (chronyc tracking)";
        return;                                                           // - 데이터 폐기: 워치독 기준 시각도 갱신하지 않는다
    }

    m_hasReceivedFirstMessage = true;
    m_lastMessageTimer.start();                                           // - 워치독 기준 시각 재설정: 정상 데이터 수신 시점으로 갱신
    emit metadataReceived(metadata);                                      // - 신호 발생: 정상 위험 이벤트 데이터 전달
}

void RiskEventSource::handleWatchdogTimeout()
{
    const int threshold = m_hasReceivedFirstMessage ? kWatchdogThresholdMs : kStartupGracePeriodMs;
    if (!m_lastMessageTimer.isValid() || m_lastMessageTimer.elapsed() < threshold)
        return;

    qCWarning(lcRiskEventSource) << "no data for" << threshold << "ms, reporting linkLost";

    m_hasReceivedFirstMessage = true;                                      // - 첫 두절 통지 이후에는 정상 주기(1000ms)로 감시
    emit linkLost();                                                       // - 신호 발생: MetadataDistributor가 전 채널로 팬아웃

    m_lastMessageTimer.start();
}
