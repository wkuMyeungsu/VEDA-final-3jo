#include "NetworkClient.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcNetworkClient, "safety.network.client")
}

NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this,
            [this] { setConnectionState(RiskTypes::ConnectionState::Connected); });
    connect(&m_socket, &QTcpSocket::disconnected, this,
            [this] { setConnectionState(RiskTypes::ConnectionState::Disconnected); });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        qCWarning(lcNetworkClient) << "control channel socket error:" << error << m_socket.errorString();
        setConnectionState(RiskTypes::ConnectionState::Disconnected);
    });
}

void NetworkClient::connectToServer(const QString &host, quint16 port)
{
    setConnectionState(RiskTypes::ConnectionState::Connecting);
    m_socket.connectToHost(host, port);
}

void NetworkClient::disconnectFromServer()
{
    m_socket.disconnectFromHost();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void NetworkClient::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged(state);
}
