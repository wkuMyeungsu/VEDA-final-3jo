#include "HandoverClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcHandoverClient, "safety.handover.client")
}

HandoverClient::HandoverClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, &HandoverClient::handleConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &HandoverClient::handleDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &HandoverClient::handleReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &HandoverClient::handleError);
}

void HandoverClient::connectToServer(const QString &host, quint16 port)
{
    setConnectionState(RiskTypes::ConnectionState::Connecting);

    m_socket.connectToHost(host, port);
}

void HandoverClient::disconnectFromServer()
{
    m_socket.disconnectFromHost();
}

void HandoverClient::handleConnected()
{
    setConnectionState(RiskTypes::ConnectionState::Connected);
}

// 연결이 끊기면 버퍼도 같이 비움 -- 재연결 시 이전 연결에서 남은 미완성
// 데이터 조각과 새 연결의 데이터가 섞여서 잘못 파싱되는 걸 방지
void HandoverClient::handleDisconnected()
{
    m_buffer.clear();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void HandoverClient::handleError(QAbstractSocket::SocketError error)
{
    qCWarning(lcHandoverClient) << "socket error:" << error << m_socket.errorString();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

// TCP는 "스트림"이라 한 번의 readyRead가 메시지 절반만 담고 있을 수도,
// 여러 메시지를 한꺼번에 담고 있을 수도 있음. 그래서 일단 buffer에 다 모아두고
// 줄바꿈('\n')이 나올 때마다 그 앞부분만 완성된 메시지 1건으로 잘라서 처리.
// 줄바꿈 구분 방식 자체가 아직 확정 프로토콜이 아니라 임시 규격임 (헤더 주석 참고)
void HandoverClient::handleReadyRead()
{
    m_buffer += m_socket.readAll();

    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
        const QByteArray line = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        if (!line.trimmed().isEmpty())
            processLine(line);
    }
}

// 완성된 한 줄을 JSON으로 해석. 기대하는 형태(임시):
// {"type": "camera_assignment", "camera_id": "..."}
// type이 camera_assignment가 아니거나 camera_id가 없으면 조용히 무시(경고 로그만) --
// 서버가 나중에 다른 type의 메시지를 같은 채널로 보내게 되더라도 여기서 안 죽게 함
void HandoverClient::processLine(const QByteArray &line)
{
    const QJsonObject obj = QJsonDocument::fromJson(line).object();

    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("camera_assignment")) {
        qCWarning(lcHandoverClient) << "ignoring unrecognized message:" << line;
        return;
    }

    const QString cameraId = obj.value(QStringLiteral("camera_id")).toString();
    if (cameraId.isEmpty()) {
        qCWarning(lcHandoverClient) << "camera_assignment message missing camera_id:" << line;
        return;
    }

    emit cameraHandoverRequested(cameraId);
}

void HandoverClient::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged(state);
}
