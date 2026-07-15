#pragma once

#include <QObject>
#include <QTcpSocket>

#include "../models/Types.h"

// Skeleton for the general command/control connection to the central
// server (distinct from the metadata stream handled by TcpMetadataSource).
// A real deployment would use this for things like heartbeats or receiving
// the operator terminal's assigned camera_id from the server. Not used by
// the demo apps yet -- ServerConnectionService reports a simulated state
// until this is wired up (see docs/INTEGRATION.md).
class NetworkClient : public QObject
{
    Q_OBJECT

public:
    explicit NetworkClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    void connectionStateChanged(RiskTypes::ConnectionState state);

private:
    void setConnectionState(RiskTypes::ConnectionState state);

    QTcpSocket m_socket;
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;

    // TODO(server integration): define the control-channel wire protocol
    // (e.g. JSON command/response messages) and implement send/receive
    // methods here.
};
