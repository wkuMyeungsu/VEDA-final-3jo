#pragma once

#include <QObject>

#include "../models/RiskMetadata.h"
#include "../models/Types.h"

// Abstraction over "however we receive risk-detection events from the
// central server". MetadataService only ever talks to this interface;
// MockMetadataSource is the only implementation wired up today, but
// TcpMetadataSource (or a WebSocket equivalent) can replace it without
// touching any UI or service code -- see docs/INTEGRATION.md.
class IMetadataSource : public QObject
{
    Q_OBJECT

public:
    explicit IMetadataSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IMetadataSource() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    void metadataReceived(const RiskMetadata &metadata);
    void connectionStateChanged(RiskTypes::ConnectionState state);

protected:
    void setConnectionState(RiskTypes::ConnectionState state)
    {
        if (m_connectionState == state)
            return;
        m_connectionState = state;
        emit connectionStateChanged(state);
    }

private:
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
};
