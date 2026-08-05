// 지게차 단말 - 서버 핸드오버 제어 채널 구현
#include "HandoverClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcHandoverClient, "safety.handover.client")

// 재연결 대기 시간(ms). 값의 근거:
//  - ConnectionRefused는 거의 즉시 돌아오므로, 쿨다운이 0이면 사실상 무한 루프로
//    서버를 두드린다 -> 최소한의 간격이 필요하다.
//  - 안전 단말이라 너무 길면 복구가 느리다. 3초는 "빠른 복구 vs 서버/로그 안 두드림"의 타협점.
//  - RtspVideoSource의 재연결과 같은 값이라, 단말 안의 두 연결(영상/제어)이 똑같이 동작한다.
// 배포별로 다르게 하고 싶으면 이 상수 한 곳만 terminal.json 설정으로 빼면 된다.
constexpr int kReconnectDelayMs = 3000;
}

HandoverClient::HandoverClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, &HandoverClient::handleConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &HandoverClient::handleDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &HandoverClient::handleReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &HandoverClient::handleError);
}

// 서버에 접속한다. 재연결 때 재사용하려고 host/port를 저장하고, 이전에 걸어둔
// "의도적 끊김" 표시를 지워 자동 재연결을 다시 켠다.
void HandoverClient::connectToServer(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_intentionalDisconnect = false;

    setConnectionState(RiskTypes::ConnectionState::Connecting);
    m_socket.connectToHost(host, port);
}

// 서버 연결을 의도적으로 끊는다. m_intentionalDisconnect를 세워 두어, 뒤이어 오는
// 끊김 처리기가 자동 재연결을 걸지 않게 한다.
void HandoverClient::disconnectFromServer()
{
    m_intentionalDisconnect = true;
    m_socket.disconnectFromHost();
}

// 접속 성공.
void HandoverClient::handleConnected()
{
    setConnectionState(RiskTypes::ConnectionState::Connected);
}

// 연결돼 있던 소켓이 끊겼을 때. 버퍼를 비우고 상태를 갱신한 뒤, (의도적으로 끊은 게
// 아니라면) 재연결을 예약한다.
void HandoverClient::handleDisconnected()
{
    m_buffer.clear();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
    scheduleReconnect();
}

// 소켓 오류. 로그를 남기고 상태를 갱신한 뒤 재연결을 예약한다. "첫 접속 실패"(서버가
// 아직 안 뜬 경우)는 errorOccurred만 오고 disconnected는 오지 않으므로, 여기서
// 재시도를 걸어줘야 서버보다 먼저 켠 단말이 나중에라도 붙을 수 있다.
void HandoverClient::handleError(QAbstractSocket::SocketError error)
{
    qCWarning(lcHandoverClient) << "소켓 오류:" << error << m_socket.errorString();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
    scheduleReconnect();
}

// 마지막으로 접속했던 곳으로 일정 시간 뒤 다시 시도한다. RtspVideoSource::
// scheduleReconnect()와 같은 방식이다: this가 소유하는 QTimer::singleShot이라
// 이 객체가 먼저 파괴되면 콜백이 자동 취소된다(허상 접근 방지). m_reconnectPending은
// 한 번의 끊김에 errorOccurred+disconnected가 둘 다 와도 재시도를 하나로 합친다.
//
// 재시도 중에는 일부러 Connecting 상태를 내보내지 않는다 -> 서버가 죽어 있는 동안
// 배지가 Connecting<->Disconnected로 깜빡이지 않고 Disconnected를 유지하고,
// 실제로 붙었을 때만 Connected로 바뀐다.
void HandoverClient::scheduleReconnect()
{
    if (m_intentionalDisconnect || m_reconnectPending || m_host.isEmpty())
        return;

    m_reconnectPending = true;
    QTimer::singleShot(kReconnectDelayMs, this, [this]() {
        m_reconnectPending = false;
        if (m_intentionalDisconnect)
            return;
        // 대기하는 동안 누군가 connectToServer()로 이미 (재)접속을 시작했다면,
        // 접속을 두 번 시도하지 않도록 여기서 멈춘다.
        if (m_socket.state() != QAbstractSocket::UnconnectedState)
            return;

        m_socket.connectToHost(m_host, m_port);
    });
}

// 도착한 데이터를 버퍼에 모으고, 개행(\n)으로 끝나는 완성된 줄만 골라 처리한다.
void HandoverClient::handleReadyRead()
{
    m_buffer += m_socket.readAll();

    // 한 줄에 JSON 하나(개행 구분) -- 팀과 프레이밍이 확정되기 전까지 쓰는 임시 방식.
    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
        const QByteArray line = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        if (!line.trimmed().isEmpty())
            processLine(line);
    }
}

// 서버가 보낸 한 줄(JSON)을 해석한다. "type"이 "camera_assignment"이고 "camera_id"가
// 있으면 cameraHandoverRequested 신호를 낸다. (스키마는 아직 임시값 - 헤더 참고.)
void HandoverClient::processLine(const QByteArray &line)
{
    const QJsonObject obj = QJsonDocument::fromJson(line).object();

    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("camera_assignment")) {
        qCWarning(lcHandoverClient) << "알 수 없는 메시지 무시:" << line;
        return;
    }

    if (!m_terminalId.isEmpty()) {
        if (!obj.contains(QStringLiteral("terminal_id"))) {
            // 수신자 불명 명령 -- 카메라를 바꾸면 엉뚱한 화면이 뜨므로 무시
            qCWarning(lcHandoverClient) << "terminal_id 없는 camera_assignment 무시:" << line;
            return;
        }
        const QString msgTerminalId = obj.value(QStringLiteral("terminal_id")).toString();
        if (msgTerminalId != m_terminalId) {
            qCWarning(lcHandoverClient) << "다른 단말(" << msgTerminalId << ") 대상 명령 무시:" << line;
            return;
        }
    }

    const QString cameraId = obj.value(QStringLiteral("camera_id")).toString();
    if (cameraId.isEmpty()) {
        qCWarning(lcHandoverClient) << "camera_assignment에 camera_id 없음:" << line;
        return;
    }

    emit cameraHandoverRequested(cameraId);
}

// 상태가 실제로 바뀐 경우에만 갱신하고 connectionStateChanged 신호를 낸다.
void HandoverClient::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged(state);
}
