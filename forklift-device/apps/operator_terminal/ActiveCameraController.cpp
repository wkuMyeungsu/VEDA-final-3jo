#include "ActiveCameraController.h"

#include "network/IWarningDevice.h"
#include "services/MetadataService.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"

ActiveCameraController::ActiveCameraController(QVector<CameraInfo> cameras, MetadataService *metadataService,
                                                 VideoSourceManager *videoManager, IWarningDevice *warningDevice,
                                                 QObject *parent)
    : QObject(parent)
    , m_metadataService(metadataService)
    , m_videoManager(videoManager)
    , m_warningDevice(warningDevice)
{
    for (const CameraInfo &info : cameras)
        m_cameras.insert(info.cameraId, info);

    if (m_metadataService)
        connect(m_metadataService, &MetadataService::metadataUpdated, this,
                &ActiveCameraController::handleMetadataUpdated);
}

void ActiveCameraController::setActiveCameraId(const QString &cameraId)
{
    if (m_activeCameraId == cameraId)
        return;

    m_activeCameraId = cameraId;

    const CameraInfo info = m_cameras.value(cameraId);
    m_zone = info.zone;
    m_cameraName = info.name;

    m_latest = m_metadataService ? m_metadataService->latestFor(cameraId) : RiskMetadata();

    attachVideoConnection();

    if (m_warningDevice)
        m_warningDevice->setRiskLevel(m_latest.riskLevel());

    emit activeCameraIdChanged();
    emit metadataChanged();
}

void ActiveCameraController::attachVideoConnection()
{
    QObject::disconnect(m_videoConnection);

    IVideoSource *source = m_videoManager ? m_videoManager->sourceFor(m_activeCameraId) : nullptr;
    if (!source) {
        m_videoConnectionState = RiskTypes::ConnectionState::Disconnected;
        emit videoConnectionStateChanged();
        return;
    }

    m_videoConnectionState = source->connectionState();
    m_videoConnection = connect(source, &IVideoSource::connectionStateChanged, this,
                                 [this](RiskTypes::ConnectionState state) {
                                     m_videoConnectionState = state;
                                     emit videoConnectionStateChanged();
                                 });
    emit videoConnectionStateChanged();
}

void ActiveCameraController::handleMetadataUpdated(const RiskMetadata &metadata)
{
    if (metadata.cameraId() != m_activeCameraId)
        return;

    m_latest = metadata;
    if (m_warningDevice)
        m_warningDevice->setRiskLevel(metadata.riskLevel());
    emit metadataChanged();
}
