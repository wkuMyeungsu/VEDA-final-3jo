#include "RiskEventSource.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcRiskEventSource, "safety.risk.tcp")                  // - 로깅 카테고리 정의: 위험 이벤트 통신 로그 분류용 이름 지정

constexpr int kReconnectBaseDelayMs = 3000;                               // - 재연결 시작 대기 시간 (3초): 첫 재시도 간격
constexpr int kReconnectMaxDelayMs = 30000;                               // - 재연결 최대 대기 시간 (30초): 서버 장기 다운 시 재시도 폭주 방지
// - 하트비트 5배 마진: 서버가 200ms 주기로 값을 다시 보내므로(2026-08-03 정책
//   확정, result_dispatcher.hpp), 네트워크 지연을 감안해도 1초 안엔 새 메시지가
//   와야 정상. 그보다 오래된 메시지는 접속 직후 밀려온 백로그로 간주해 버림
constexpr qint64 kStaleThresholdMs = 1000;                                // - 오래된 메시지 기준 시간 (1초): 지연된 경보 발생 방지 목적

// - 서버 publish 주기(현재 200ms, 2026-08-03 정책 확정)의 3배: 서버가 실험 후
//   1000~1500ms로 조정하면 이 값도 같이 조정해야 함
constexpr int kServerHeartbeatMs = 200;                                   // - 서버 하트비트 주기: result_dispatcher.hpp의 확정값과 동일하게 유지
constexpr int kWatchdogMultiplier = 3;                                    // - 워치독 배수: 하트비트 몇 배 무수신 시 끊김으로 볼지
constexpr int kWatchdogThresholdMs = kServerHeartbeatMs * kWatchdogMultiplier; // - 워치독 만료 시간: 이 시간 동안 무수신이면 NetworkDisconnected 표시
}

RiskEventSource::RiskEventSource(QString host, quint16 port, QObject *parent)
    : IMetadataSource(parent)
    , m_host(std::move(host))                                             // - 접속 주소 저장: 서버 호스트 정보 보관
    , m_port(port)                                                         // - 접속 포트 저장: 서버 포트 번호 보관
{
    m_reconnectDelayMs = kReconnectBaseDelayMs;                             // - 재연결 대기 시간 초기화: 첫 시도는 기본값(3초)부터 시작
    connect(&m_socket, &QTcpSocket::connected, this, &RiskEventSource::handleConnected);       // - 연결 성공 이벤트 연결
    connect(&m_socket, &QTcpSocket::disconnected, this, &RiskEventSource::handleDisconnected); // - 연결 끊김 이벤트 연결
    connect(&m_socket, &QTcpSocket::readyRead, this, &RiskEventSource::handleReadyRead);       // - 데이터 수신 이벤트 연결
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &RiskEventSource::handleError);       // - 통신 오류 이벤트 연결

    m_watchdogTimer.setSingleShot(true);                                    // - 워치독 반복 방지: 만료마다 한 번만 발화, 재시작은 수동으로 처리
    connect(&m_watchdogTimer, &QTimer::timeout, this, &RiskEventSource::handleWatchdogTimeout); // - 워치독 만료 이벤트 연결
}

void RiskEventSource::start()
{
    m_stopRequested = false;                                              // - 수동 종료 플래그 초기화: 자동 재연결 기능 활성화
    setConnectionState(RiskTypes::ConnectionState::Connecting);           // - 상태 변경: 연결 진행 중 상태로 설정
    m_socket.connectToHost(m_host, m_port);                               // - 서버 접속 시도: 지정 주소 및 포트로 연결 요청
}

void RiskEventSource::stop()
{
    m_stopRequested = true;                                               // - 수동 종료 설정: 자동 재연결 동작 차단
    m_watchdogTimer.stop();                                                // - 워치독 정지: 의도적 종료는 무수신 경고 대상 아님
    m_socket.disconnectFromHost();                                        // - 연결 해제: 서버와의 소켓 연결 종료
    setConnectionState(RiskTypes::ConnectionState::Disconnected);          // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
}

void RiskEventSource::handleConnected()
{
    m_reconnectDelayMs = kReconnectBaseDelayMs;                            // - 재연결 대기 시간 초기화: 연결 성공 시 backoff 처음부터 다시
    setConnectionState(RiskTypes::ConnectionState::Connected);            // - 상태 갱신: 연결 상태를 '연결됨'으로 변경
    m_watchdogTimer.start(kWatchdogThresholdMs);                           // - 워치독 시작: 접속 직후부터 무수신 시간 감시
}

void RiskEventSource::handleDisconnected()
{
    m_buffer.clear();                                                     // - 버퍼 초기화: 이전 수신 데이터 비우기
    m_watchdogTimer.stop();                                                // - 워치독 정지: 소켓 끊김은 ConnectionState로 이미 표시됨
    setConnectionState(RiskTypes::ConnectionState::Disconnected);          // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
    if (!m_stopRequested)                                                 // - 수동 종료 확인: 의도치 않은 끊김인 경우 재연결 진행
        scheduleReconnect();                                              // - 재연결 예약: 일정 시간 후 접속 재시도
}

void RiskEventSource::handleError(QAbstractSocket::SocketError error)
{
    qCWarning(lcRiskEventSource) << "socket error:" << error << m_socket.errorString(); // - 경고 로그 출력: 오류 내용 및 원인 기록
    m_watchdogTimer.stop();                                                // - 워치독 정지: 소켓 오류는 ConnectionState로 이미 표시됨
    setConnectionState(RiskTypes::ConnectionState::Disconnected);          // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
    if (!m_stopRequested)                                                 // - 수동 종료 확인: 의도치 않은 에러인 경우 재연결 진행
        scheduleReconnect();                                              // - 재연결 예약: 접속 실패 시 자동 복구 재시도
}

void RiskEventSource::handleWatchdogTimeout()
{
    qCWarning(lcRiskEventSource) << "no data for" << kWatchdogThresholdMs << "ms, reporting NetworkDisconnected"; // - 경고 로그: 무수신 시간 초과 기록

    RiskMetadata metadata;                                                // - 임시 메타데이터 생성: 통신 끊김 표시 전용
    metadata.setCameraId(m_lastCameraId);                                  // - 카메라 ID 유지: 마지막으로 보던 카드에 표시되도록
    metadata.setExceptionState(RiskTypes::ExceptionState::NetworkDisconnected); // - 예외 상태 설정: 통신 끊김으로 지정
    metadata.setUtcTime(QDateTime::currentDateTimeUtc());                  // - 시각 설정: 워치독 발화 시각으로 지정
    emit metadataReceived(metadata);                                       // - 신호 발생: 통신 끊김 상태를 화면에 전달

    m_watchdogTimer.start(kWatchdogThresholdMs);                           // - 워치독 재시작: 계속 무수신이면 같은 경고를 주기적으로 재통지
}

void RiskEventSource::scheduleReconnect()
{
    if (m_reconnectPending)                                               // - 중복 시도 차단: 이미 예약된 경우 실행 생략
        return;
    m_reconnectPending = true;                                            // - 예약 상태 설정: 중복 타이머 생성 방지

    const int delay = m_reconnectDelayMs;                                 // - 이번 대기 시간 확정: 다음 시도용 증가 전 값 사용
    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, kReconnectMaxDelayMs); // - 다음 대기 시간 증가: 실패할수록 간격을 2배씩 늘림(상한 30초)

    QTimer::singleShot(delay, this, [this]() {                            // - 지연 실행 예약: 현재 backoff 간격만큼 대기 후 재접속 작업 진행
        m_reconnectPending = false;                                        // - 예약 상태 해제: 대기 상태 초기화
        if (m_stopRequested)                                              // - 수동 종료 확인: 대기 중 종료 요청 시 중단
            return;
        setConnectionState(RiskTypes::ConnectionState::Connecting);      // - 상태 변경: 연결 진행 중 상태로 설정
        m_socket.connectToHost(m_host, m_port);                            // - 재접속 시도: 저장된 주소 및 포트로 접속 요청
    });
}

void RiskEventSource::handleReadyRead()
{
    m_watchdogTimer.start(kWatchdogThresholdMs);                           // - 워치독 재시작: 데이터가 도착했으니 무수신 시계 초기화
    m_buffer += m_socket.readAll();                                       // - 데이터 누적: 수신 데이터를 버퍼에 연속 저장

    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {               // - 줄 단위 처리: 개행 문자(\n) 검색 및 반복 처리
        const QByteArray line = m_buffer.left(newlineIndex);              // - 한 줄 추출: 개행 문자 전까지 데이터 잘라내기
        m_buffer.remove(0, newlineIndex + 1);                             // - 버퍼 정리: 처리된 데이터 및 개행 문자 제거
        if (!line.trimmed().isEmpty())                                    // - 유효 검증: 공백이 아닌 경우 해석 함수 호출
            processLine(line);
    }
}

void RiskEventSource::processLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError); // - JSON 변환: 수신 문장 해석 및 데이터 파싱

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) { // - 유효성 검증: JSON 형식 에러 또는 객체가 아닌 경우 필터링
        qCWarning(lcRiskEventSource) << "malformed line, ignoring:" << parseError.errorString() << line; // - 경고 로그: 손상된 데이터 기록
        return;
    }

    const RiskMetadata metadata = RiskMetadata::fromJson(doc.object());   // - 데이터 객체 생성: JSON을 RiskMetadata로 변환

    const QDateTime utcTime = metadata.utcTime();                         // - 시간 정보 추출: 메시지 생성 시각 확인
    if (!utcTime.isValid()) {                                             // - 시각 검증: 시간 정보가 없거나 유효하지 않은 경우 처리
        qCWarning(lcRiskEventSource) << "utc_time missing/unparseable, treating as fresh:" << line; // - 경고 로그: 시간 정보 없음 기록
        m_lastCameraId = metadata.cameraId();                             // - 마지막 카메라 ID 기록: 워치독 만료 시 표시용
        emit metadataReceived(metadata);                                  // - 신호 발생: 시간 확인 불가 데이터 전달 처리
        return;
    }

    const qint64 ageMs = utcTime.msecsTo(QDateTime::currentDateTimeUtc());// - 경과 시간 계산: 현재 시간과의 차이(ms) 측정
    if (ageMs > kStaleThresholdMs) {                                      // - 시간 지연 검증: 1초 이상 지난 지연 데이터 여부 확인
        qCInfo(lcRiskEventSource) << "discarding stale backlog message, age(ms)=" << ageMs
                                   << "utc_time=" << utcTime.toString(Qt::ISODateWithMs); // - 정보 로그: 오래된 데이터 폐기 기록
        return;                                                           // - 데이터 폐기: 지나간 경보 데이터 반영 생략
    }

    m_lastCameraId = metadata.cameraId();                                 // - 마지막 카메라 ID 기록: 워치독 만료 시 표시용
    emit metadataReceived(metadata);                                      // - 신호 발생: 정상 위험 이벤트 데이터 전달
}
