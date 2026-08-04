#pragma once

#include <QObject>

#include "../models/Types.h"

// Tracks the connection to the central server, as shown in both apps'
// top/status bars. Backed by HandoverClient once real server integration
// lands (see docs/INTEGRATION.md); until then it simply starts Connected
// and is driven by DemoController for demo purposes.
class ServerConnectionService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState WRITE setConnectionState NOTIFY
                   connectionStateChanged)

public:
    explicit ServerConnectionService(QObject *parent = nullptr);

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }
    void setConnectionState(RiskTypes::ConnectionState state);

signals:
    void connectionStateChanged();

private:
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Connected;
};
