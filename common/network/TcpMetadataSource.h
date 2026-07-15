#pragma once

#include <QByteArray>
#include <QTcpSocket>

#include "IMetadataSource.h"

// Skeleton for a future TCP connection to the central server's metadata
// stream (a WebSocket-based source would look almost identical). Wiring
// this up and swapping it in for MockMetadataSource in main.cpp is the
// integration point described in docs/INTEGRATION.md.
//
// The socket connection itself is real -- if there is no server listening,
// it fails and reports DISCONNECTED like any other unavailable source
// instead of hanging the app. Only the wire-protocol parsing is left as a
// TODO, since the real message format is not defined yet.
class TcpMetadataSource : public IMetadataSource
{
    Q_OBJECT

public:
    TcpMetadataSource(QString host, quint16 port, QObject *parent = nullptr);

    void start() override;
    void stop() override;

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleReadyRead();
    void handleSocketError(QAbstractSocket::SocketError error);

private:
    QString m_host;
    quint16 m_port;
    QTcpSocket m_socket;
    QByteArray m_receiveBuffer;

    // TODO(server integration): define the actual wire protocol (e.g.
    // newline-delimited JSON objects matching the RiskMetadata::fromJson
    // schema) and parse complete messages out of m_receiveBuffer here,
    // emitting metadataReceived() for each one.
};
