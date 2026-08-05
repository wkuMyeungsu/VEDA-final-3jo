#include "RiskEventSource.h"

#include <QDateTime>        // 시각 처리
#include <QJsonDocument>    // JSON 파싱
#include <QJsonObject>      // JSON 객체
#include <QJsonParseError>  // 파싱 에러 정보
#include <QLoggingCategory> // 로그 카테고리
#include <QTimer>           // 지연 실행

namespace {
Q_LOGGING_CATEGORY(lcRiskEventSource, "safety.risk.tcp")  // 로그 태그

// 재접속 시 재시도 간격 -- RtspVideoSource::scheduleReconnect()와 동일한 값 사용
// (ConnectionRefused 순간 재시도로 바쁜 루프 도는 것 방지 + 값 통일)
constexpr int kReconnectDelayMs = 3000;   // 재접속 대기 3초

// 접속 직후 몰려오는 백로그(최대 100건) 중 이 값보다 오래된 메시지는 화면/부저에
// 반영 안 함. 정상 동작 중 메시지 간격은 최대 200ms라 여유 있게 잡음.
// [미확정] 근거 없는 값 -- 실측/협의 후 조정 필요
constexpr qint64 kStaleThresholdMs = 1000;
}

RiskEventSource::RiskEventSource(QString host, quint16 port, QObject *parent)
    : IMetadataSource(parent)
    , m_host(std::move(host))   // 서버 주소 저장
    , m_port(port)               // 서버 포트 저장
{
    connect(&m_socket, &QTcpSocket::connected, this, &RiskEventSource::handleConnected);       // 연결 성공 시
    connect(&m_socket, &QTcpSocket::disconnected, this, &RiskEventSource::handleDisconnected); // 연결 끊길 시
    connect(&m_socket, &QTcpSocket::readyRead, this, &RiskEventSource::handleReadyRead);       // 데이터 도착 시
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &RiskEventSource::handleError);       // 에러 발생 시
}

void RiskEventSource::start()
{
    m_stopRequested = false;   // 재연결 허용 상태로
    setConnectionState(RiskTypes::ConnectionState::Connecting); // 상태를 연결 중으로
    m_socket.connectToHost(m_host, m_port); // 접속 시도
}

void RiskEventSource::stop()
{
    m_stopRequested = true;             // 이후 끊김은 재연결 안 함
    m_socket.disconnectFromHost();      // 연결 종료
    setConnectionState(RiskTypes::ConnectionState::Disconnected); // 상태를 끊김으로
}

void RiskEventSource::handleConnected()
{
    setConnectionState(RiskTypes::ConnectionState::Connected); // 상태를 연결됨으로
}

// 연결이 끊기면 버퍼도 같이 비움 -- HandoverClient::handleDisconnected()와 동일한 이유
// (이전 연결의 미완성 조각과 새 연결 데이터가 섞이는 것 방지)
void RiskEventSource::handleDisconnected()
{
    m_buffer.clear();          // 받다 만 데이터 버림
    setConnectionState(RiskTypes::ConnectionState::Disconnected); // 상태를 끊김으로
    if (!m_stopRequested)      // 의도치 않은 끊김이면
        scheduleReconnect();   // 재접속 예약
}

void RiskEventSource::handleError(QAbstractSocket::SocketError error)
{
    qCWarning(lcRiskEventSource) << "socket error:" << error << m_socket.errorString(); // 에러 로그
    setConnectionState(RiskTypes::ConnectionState::Disconnected); // 상태를 끊김으로
    if (!m_stopRequested)      // 의도치 않은 끊김이면
        scheduleReconnect();   // 재접속 예약
}

// errorOccurred + disconnected가 한 번의 끊김에 대해 같이 발생할 수 있어서
// m_reconnectPending으로 중복 예약을 막음 (RtspVideoSource와 동일한 이유)
void RiskEventSource::scheduleReconnect()
{
    if (m_reconnectPending)    // 이미 예약돼 있으면
        return;                // 중복 예약 방지
    m_reconnectPending = true; // 예약 표시

    // this가 먼저 파괴되면 QTimer::singleShot이 콜백을 알아서 취소해줌
    QTimer::singleShot(kReconnectDelayMs, this, [this]() {   // 3초 뒤 1회 실행
        m_reconnectPending = false;   // 예약 해제
        if (m_stopRequested)          // 그새 stop() 호출됐으면
            return;                   // 재접속 안 함
        setConnectionState(RiskTypes::ConnectionState::Connecting); // 상태를 연결 중으로
        m_socket.connectToHost(m_host, m_port); // 재접속 시도
    });
}

// TCP는 스트림이라 한 번의 readyRead가 메시지 절반만, 또는 여러 개를 한꺼번에
// 담을 수 있음 -- HandoverClient::handleReadyRead()와 동일한 버퍼링 방식
void RiskEventSource::handleReadyRead()
{
    m_buffer += m_socket.readAll();   // 도착 데이터를 버퍼에 이어붙임

    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {   // 줄바꿈 하나 = 메시지 1건
        const QByteArray line = m_buffer.left(newlineIndex);   // 완성된 한 줄
        m_buffer.remove(0, newlineIndex + 1);                  // 처리한 부분 버퍼에서 제거
        if (!line.trimmed().isEmpty())                         // 빈 줄 아니면
            processLine(line);                                 // 처리 함수로 전달
    }
}

// 완성된 한 줄을 RiskMetadata로 파싱. distance_m null -> distanceValid=false는
// RiskMetadata::fromJson()이 이미 처리함 (QML도 이미 "측정 불가"로 표시함, 새로
// 만들 필요 없음). 여기서 추가로 하는 일은 오래된 백로그 메시지 필터링뿐.
void RiskEventSource::processLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError); // JSON 파싱 시도
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) { // 깨진 줄이면
        qCWarning(lcRiskEventSource) << "malformed line, ignoring:" << parseError.errorString() << line; // 경고 로그
        return;                                                            // 버림
    }

    const RiskMetadata metadata = RiskMetadata::fromJson(doc.object()); // RiskMetadata로 변환

    const QDateTime utcTime = metadata.utcTime();  // 메시지 생성 시각
    if (!utcTime.isValid()) {                      // 시각 정보 없거나 이상하면
        // 오래된 메시지인지 판단 불가 -- 일단 반영하고 경고만 남김
        qCWarning(lcRiskEventSource) << "utc_time missing/unparseable, treating as fresh:" << line; // 경고 로그
        emit metadataReceived(metadata);            // 반영
        return;
    }

    const qint64 ageMs = utcTime.msecsTo(QDateTime::currentDateTimeUtc()); // 경과 시간 계산
    if (ageMs > kStaleThresholdMs) {   // 1초 넘게 오래됐으면
        // 접속 직후 몰려온 백로그 -- 로그만 남기고 화면엔 반영 안 함
        qCInfo(lcRiskEventSource) << "discarding stale backlog message, age(ms)=" << ageMs
                                   << "utc_time=" << utcTime.toString(Qt::ISODateWithMs); // 정보 로그
        return;                        // 버림
    }

    emit metadataReceived(metadata);   // 오래되지 않은 메시지 -- 정상 반영
}
