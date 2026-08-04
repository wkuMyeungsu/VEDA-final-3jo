#include "ActiveCameraController.h"

#include "network/IWarningDevice.h"
#include "services/MetadataDistributor.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"

#include <QLoggingCategory>
namespace {
Q_LOGGING_CATEGORY(lcActiveCamera, "safety.activecamera")
}

ActiveCameraController::ActiveCameraController(QVector<CameraInfo> cameras, MetadataDistributor *metadataDistributor,
                                                 VideoSourceManager *videoManager, IWarningDevice *warningDevice,
                                                 QObject *parent)
    : QObject(parent)
    , m_metadataDistributor(metadataDistributor)
    , m_videoManager(videoManager)
    , m_warningDevice(warningDevice)
{
    for (const CameraInfo &info : cameras)
        m_cameras.insert(info.cameraId, info);

    if (m_metadataDistributor)
        connect(m_metadataDistributor, &MetadataDistributor::metadataUpdated, this,
                &ActiveCameraController::handleMetadataUpdated);

    m_onvifParser = new OnvifBBoxParser(this);
    connect(m_onvifParser, &OnvifBBoxParser::personDetected, this, &ActiveCameraController::handlePersonDetected);
}

void ActiveCameraController::setActiveCameraId(const QString &cameraId)
{
    if (m_activeCameraId == cameraId)
        return;

    m_activeCameraId = cameraId;

    const CameraInfo info = m_cameras.value(cameraId);
    m_zone = info.zone;
    m_cameraName = info.name;

    m_latest = m_metadataDistributor ? m_metadataDistributor->latestFor(cameraId) : RiskMetadata();
    m_onvifActive = false;
    m_onvifPersonBBox = BBox();

    attachVideoConnection();

    if (m_warningDevice)
        m_warningDevice->setRiskLevel(m_latest.riskLevel());

    emit activeCameraIdChanged();
    emit metadataChanged();
}

void ActiveCameraController::attachVideoConnection()
{
    QObject::disconnect(m_videoConnection);
    QObject::disconnect(m_onvifConnection);

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
    m_onvifConnection = connect(source, &IVideoSource::onvifMetadataReceived, m_onvifParser,
                                 &OnvifBBoxParser::processMetadata);
    qCDebug(lcActiveCamera) << "attachVideoConnection camera:" << m_activeCameraId
                            << "onvif connection valid:" << bool(m_onvifConnection);
    emit videoConnectionStateChanged();
}

void ActiveCameraController::handlePersonDetected(const BBox &bbox)
{
    qCDebug(lcActiveCamera) << "handlePersonDetected valid:" << bbox.isValid() << "x:" << bbox.x()
                            << "y:" << bbox.y() << "w:" << bbox.width() << "h:" << bbox.height();
    m_onvifActive = true;
    if (m_onvifPersonBBox == bbox)
        return;
    m_onvifPersonBBox = bbox;
    emit metadataChanged();
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
