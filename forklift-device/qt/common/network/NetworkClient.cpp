// Forklift Device Network Client
#include "NetworkClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcNetworkClient, "safety.network.client")
}

// NetworkClient is a control channel to the central server. It receives camera-assignment
// messages from the server.
NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, &NetworkClient::handleConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &NetworkClient::handleDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &NetworkClient::handleReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &NetworkClient::handleError);
}

// Connect to the server at the given host and port. The connection state will be updated
// and the connectionStateChanged signal will be emitted when the connection state changes.
void NetworkClient::connectToServer(const QString &host, quint16 port)
{
    setConnectionState(RiskTypes::ConnectionState::Connecting);
    
    m_socket.connectToHost(host, port);
}

// Disconnect from the server. The connection state will be updated and the connectionStateChanged
// signal will be emitted when the connection state changes.
void NetworkClient::disconnectFromServer()
{
    m_socket.disconnectFromHost();
}

// Handle the connected signal from the socket. Update the connection state and emit the connectionStateChanged signal.
void NetworkClient::handleConnected()
{
    setConnectionState(RiskTypes::ConnectionState::Connected);
}

// Handle the disconnected signal from the socket. Clear the buffer and update the connection state.
void NetworkClient::handleDisconnected()
{
    m_buffer.clear();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

// Handle the errorOccurred signal from the socket. Log the error and update the connection state.
void NetworkClient::handleError(QAbstractSocket::SocketError error)
{
    qCWarning(lcNetworkClient) << "socket error:" << error << m_socket.errorString();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

// Handle the readyRead signal from the socket. Read all available data and process complete lines.
void NetworkClient::handleReadyRead()
{
    m_buffer += m_socket.readAll();

    // Newline-delimited JSON, one message per line -- placeholder framing
    // until real message delimiting is agreed on with the handover owner.
    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
        const QByteArray line = m_buffer.left(newlineIndex);
        m_buffer.remove(0, newlineIndex + 1);
        if (!line.trimmed().isEmpty())
            processLine(line);
    }
}

// Process a single line of input from the server. The line is expected to be a JSON object with a "type" field and a "camera_id" field. If the type is "camera_assignment", emit the cameraHandoverRequested signal with the camera ID.
void NetworkClient::processLine(const QByteArray &line)
{
    const QJsonObject obj = QJsonDocument::fromJson(line).object();

    // Placeholder schema: {"type": "camera_assignment", "camera_id": "..."}.
    // Not yet confirmed with the handover-logic owner.
    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("camera_assignment")) {
        qCWarning(lcNetworkClient) << "ignoring unrecognized message:" << line;
        return;
    }

    const QString cameraId = obj.value(QStringLiteral("camera_id")).toString();
    if (cameraId.isEmpty()) {
        qCWarning(lcNetworkClient) << "camera_assignment message missing camera_id:" << line;
        return;
    }

    emit cameraHandoverRequested(cameraId);
}

// Set the connection state and emit the connectionStateChanged signal if the state has changed.
void NetworkClient::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged(state);
}
