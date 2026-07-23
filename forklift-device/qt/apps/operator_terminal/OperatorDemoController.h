#pragma once

#include "services/DemoController.h"

class ActiveCameraController;

// Adds operator-terminal-specific demo actions (camera_id switch, handover
// simulation) on top of the shared DemoController surface.
class OperatorDemoController : public DemoController
{
    Q_OBJECT

public:
    OperatorDemoController(MockMetadataSource *metadataSource, VideoSourceManager *videoManager,
                            ServerConnectionService *serverConnection, ActiveCameraController *activeCamera,
                            QObject *parent = nullptr);

    Q_INVOKABLE void setActiveCameraId(const QString &cameraId);
    Q_INVOKABLE void triggerHandover(const QString &fromCameraId, const QString &toCameraId);

private:
    ActiveCameraController *m_activeCamera;
};
