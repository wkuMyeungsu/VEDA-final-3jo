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
    Q_UNUSED(fromCameraId);
    if (m_activeCamera && !toCameraId.isEmpty())
        m_activeCamera->setActiveCameraId(toCameraId);
}
