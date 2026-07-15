#pragma once

#include <QObject>

#include "../models/BBox.h"
#include "../models/Types.h"

class MockMetadataSource;
class VideoSourceManager;
class ServerConnectionService;

// Control surface for the --demo panels. Exposed to QML as the
// "demoController" context property in both apps. All of this is inert in
// a real deployment: demoModeEnabled only becomes true when --demo is
// passed on the command line, and QML gates the panel/shortcut on that
// property, so these methods are simply unreachable otherwise.
class DemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool demoModeEnabled READ demoModeEnabled WRITE setDemoModeEnabled NOTIFY demoModeEnabledChanged)
    Q_PROPERTY(bool autoPlay READ autoPlay WRITE setAutoPlay NOTIFY autoPlayChanged)

public:
    DemoController(MockMetadataSource *metadataSource, VideoSourceManager *videoManager,
                   ServerConnectionService *serverConnection, QObject *parent = nullptr);

    bool demoModeEnabled() const { return m_demoModeEnabled; }
    void setDemoModeEnabled(bool enabled);

    bool autoPlay() const { return m_autoPlay; }
    void setAutoPlay(bool enabled);

    Q_INVOKABLE void setServerConnected(bool connected);
    Q_INVOKABLE void setCameraConnected(const QString &cameraId, bool connected);

    // riskLevel: 0=Safe 1=Caution 2=Danger 3=Emergency.
    // exceptionState: "NONE" | "SENSOR_FAULT" | "DEAD_RECKONING" |
    //                 "EMERGENCY_IMPACT" | "NETWORK_DISCONNECTED" | "CAMERA_DISCONNECTED"
    Q_INVOKABLE void setCameraRisk(const QString &cameraId, int riskLevel, double distanceM,
                                   const QString &exceptionState);
    Q_INVOKABLE void clearCameraRisk(const QString &cameraId);

    Q_INVOKABLE void setPersonBBox(const QString &cameraId, double x, double y, double width, double height);
    Q_INVOKABLE void setForkliftBBox(const QString &cameraId, double x, double y, double width, double height);
    Q_INVOKABLE void clearBBoxOverrides(const QString &cameraId);

signals:
    void demoModeEnabledChanged();
    void autoPlayChanged();

protected:
    MockMetadataSource *m_metadataSource;
    VideoSourceManager *m_videoManager;
    ServerConnectionService *m_serverConnection;

private:
    bool m_demoModeEnabled = false;
    bool m_autoPlay = true;
};
