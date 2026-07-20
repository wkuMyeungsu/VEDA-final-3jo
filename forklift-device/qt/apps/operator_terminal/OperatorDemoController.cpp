#include "OperatorDemoController.h"

#include "ActiveCameraController.h"

OperatorDemoController::OperatorDemoController(MockMetadataSource *metadataSource, VideoSourceManager *videoManager,
                                                 ServerConnectionService *serverConnection,
                                                 ActiveCameraController *activeCamera, QObject *parent)
    : DemoController(metadataSource, videoManager, serverConnection, parent)
    , m_activeCamera(activeCamera)
{
}

void OperatorDemoController::setActiveCameraId(const QString &cameraId)
{
    if (m_activeCamera)
        m_activeCamera->setActiveCameraId(cameraId);
}

void OperatorDemoController::triggerHandover(const QString &fromCameraId, const QString &toCameraId)
{
    if (!m_activeCamera)
        return;

    // A real handover would be server-driven; here we just require the
    // caller to name the camera it believes is currently active, so the
    // demo panel can show a clear "from -> to" affordance.
    if (m_activeCamera->activeCameraId() == fromCameraId)
        m_activeCamera->setActiveCameraId(toCameraId);
}
