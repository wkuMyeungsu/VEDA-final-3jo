#include "RiskEventSource.h"

#include <mosquitto.h>

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

// - 하트비트 5배 마진: 서버가 200ms 주기로 값을 다시 보내므로(2026-08-03 정책
//   확정, result_dispatcher.hpp), 네트워크 지연을 감안해도 1초 안엔 새 메시지가
//   와야 정상. 그보다 오래된 메시지는 retain으로 밀려온 백로그로 간주해 버림
constexpr qint64 kStaleThresholdMs = 1000;                                // - 오래된 메시지 기준 시간 (1초): 지연된 경보 발생 방지 목적

// - 서버 publish 주기(현재 200ms, 2026-08-03 정책 확정)의 3배: 서버가 실험 후
//   1000~1500ms로 조정하면 이 값도 같이 조정해야 함
constexpr int kServerHeartbeatMs = 200;                                   // - 서버 하트비트 주기: result_dispatcher.hpp의 확정값과 동일하게 유지
constexpr int kWatchdogMultiplier = 3;                                    // - 워치독 배수: 하트비트 몇 배 무수신 시 끊김으로 볼지
constexpr int kWatchdogThresholdMs = kServerHeartbeatMs * kWatchdogMultiplier; // - 워치독 만료 시간: 이 시간 동안 무수신이면 NetworkDisconnected 표시
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

    m_lastMessageTimer.invalidate();                                     // - 수신 기준 시각 초기화: 아직 연결 전이므로 워치독 판단 보류
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
            qCWarning(lcRiskEventSource) << "connect failed, rc=" << rc << mosquitto_connack_string(rc); // - 경고 로그: 접속 실패 원인 기록
            return;
        }
        qCInfo(lcRiskEventSource) << "connected to broker, subscribed to" << self->m_topic; // - 정보 로그: 접속 및 구독 완료 기록
        self->setConnectionState(RiskTypes::ConnectionState::Connected); // - 상태 갱신: 연결 상태를 '연결됨'으로 변경
        self->m_lastMessageTimer.start();                                // - 워치독 기준 시각 재설정: 접속 시점부터 무수신 시간 감시
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

    const QDateTime utcTime = metadata.utcTime();                         // - 시간 정보 추출: 메시지 생성 시각 확인
    if (!utcTime.isValid()) {                                             // - 시각 검증: 시간 정보가 없거나 유효하지 않은 경우 처리
        qCWarning(lcRiskEventSource) << "utc_time missing/unparseable, treating as fresh:" << payload; // - 경고 로그: 시간 정보 없음 기록
        m_lastCameraId = metadata.cameraId();                             // - 마지막 카메라 ID 기록: 워치독 만료 시 표시용
        m_lastMessageTimer.start();                                       // - 워치독 기준 시각 재설정: 데이터가 도착했으니 무수신 시계 초기화
        emit metadataReceived(metadata);                                  // - 신호 발생: 시간 확인 불가 데이터 전달 처리
        return;
    }

    const qint64 ageMs = utcTime.msecsTo(QDateTime::currentDateTimeUtc());// - 경과 시간 계산: 현재 시간과의 차이(ms) 측정
    if (ageMs > kStaleThresholdMs) {                                      // - 시간 지연 검증: retain으로 수신된 오래된 값 여부 확인
        qCInfo(lcRiskEventSource) << "discarding stale retained message, age(ms)=" << ageMs
                                   << "utc_time=" << utcTime.toString(Qt::ISODateWithMs); // - 정보 로그: 오래된 데이터 폐기 기록
        return;                                                           // - 데이터 폐기: 지나간 경보 데이터 반영 생략 (워치독 기준 시각도 갱신하지 않음)
    }

    m_lastCameraId = metadata.cameraId();                                 // - 마지막 카메라 ID 기록: 워치독 만료 시 표시용
    m_lastMessageTimer.start();                                           // - 워치독 기준 시각 재설정: 정상 데이터 수신 시점으로 갱신
    emit metadataReceived(metadata);                                      // - 신호 발생: 정상 위험 이벤트 데이터 전달
}

void RiskEventSource::handleWatchdogTimeout()
{
    if (!m_lastMessageTimer.isValid() || m_lastMessageTimer.elapsed() < kWatchdogThresholdMs) // - 아직 연결 전이거나 기준 시간 이내면 정상 상태로 판단
        return;

    qCWarning(lcRiskEventSource) << "no data for" << kWatchdogThresholdMs << "ms, reporting NetworkDisconnected"; // - 경고 로그: 무수신 시간 초과 기록

    RiskMetadata metadata;                                                // - 임시 메타데이터 생성: 통신 끊김 표시 전용
    metadata.setCameraId(m_lastCameraId);                                  // - 카메라 ID 유지: 마지막으로 보던 카드에 표시되도록
    metadata.setExceptionState(RiskTypes::ExceptionState::NetworkDisconnected); // - 예외 상태 설정: 통신 끊김으로 지정
    metadata.setUtcTime(QDateTime::currentDateTimeUtc());                  // - 시각 설정: 워치독 발화 시각으로 지정
    emit metadataReceived(metadata);                                       // - 신호 발생: 통신 끊김 상태를 화면에 전달

    // - 기준 시각을 지금으로 재설정: 계속 무수신이면 kWatchdogThresholdMs 후 다시 통지.
    //   100ms마다 폴링하되 통지 자체는 기존 워치독과 동일한 주기(600ms)로 유지해
    //   EventLogModel에 동일 이벤트가 100ms 간격으로 중복 쌓이는 것을 막는다.
    m_lastMessageTimer.start();
}
