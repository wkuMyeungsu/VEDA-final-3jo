#pragma once

#include <QImage>
#include <QObject>

#include "../models/Types.h"

// Abstraction over "however we get decoded video frames for one camera".
// VideoStream and the rest of the UI only ever talk to this interface;
// VideoSourceManager decides which concrete implementation (Mock,
// LocalFile, Rtsp) to instantiate based on config/cameras.json.
class IVideoSource : public QObject
{
    Q_OBJECT

public:
    explicit IVideoSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IVideoSource() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    // Demo-only hook: lets DemoController simulate a camera being
    // physically unplugged without needing to know the concrete source
    // type. Real sources (Rtsp) simply ignore this.
    virtual void setSimulatedConnected(bool connected) { Q_UNUSED(connected); }

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    void frameReady(const QImage &frame);
    void connectionStateChanged(RiskTypes::ConnectionState state);

    // ONVIF 메타데이터(raw XML) 수신 — RTSP 전용
    void onvifMetadataReceived(const QByteArray &xml);

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
