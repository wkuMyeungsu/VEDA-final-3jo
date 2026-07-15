#include "TcpMetadataSource.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcTcpMeta, "safety.network.tcpmetadata")
}

TcpMetadataSource::TcpMetadataSource(QString host, quint16 port, QObject *parent)
    : IMetadataSource(parent)
    , m_host(std::move(host))
    , m_port(port)
{
    connect(&m_socket, &QTcpSocket::connected, this, &TcpMetadataSource::handleConnected);
    connect(&m_socket, &QTcpSocket::disconnected, this, &TcpMetadataSource::handleDisconnected);
    connect(&m_socket, &QTcpSocket::readyRead, this, &TcpMetadataSource::handleReadyRead);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &TcpMetadataSource::handleSocketError);
}

void TcpMetadataSource::start()
{
    setConnectionState(RiskTypes::ConnectionState::Connecting);
    m_socket.connectToHost(m_host, m_port);
}

void TcpMetadataSource::stop()
{
    m_socket.disconnectFromHost();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void TcpMetadataSource::handleConnected()
{
    setConnectionState(RiskTypes::ConnectionState::Connected);
}

void TcpMetadataSource::handleDisconnected()
{
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void TcpMetadataSource::handleReadyRead()
{
    m_receiveBuffer.append(m_socket.readAll());
    // TODO(server integration): extract complete messages from
    // m_receiveBuffer, parse each with RiskMetadata::fromJson(), and
    // emit metadataReceived() for it.
}

void TcpMetadataSource::handleSocketError(QAbstractSocket::SocketError error)
{
    qCWarning(lcTcpMeta) << "metadata socket error:" << error << m_socket.errorString();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}
