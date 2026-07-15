#include "DemoController.h"

#include "../network/MockMetadataSource.h"
#include "../video/IVideoSource.h"
#include "../video/VideoSourceManager.h"
#include "ServerConnectionService.h"

DemoController::DemoController(MockMetadataSource *metadataSource, VideoSourceManager *videoManager,
                                ServerConnectionService *serverConnection, QObject *parent)
    : QObject(parent)
    , m_metadataSource(metadataSource)
    , m_videoManager(videoManager)
    , m_serverConnection(serverConnection)
{
}

void DemoController::setDemoModeEnabled(bool enabled)
{
    if (m_demoModeEnabled == enabled)
        return;
    m_demoModeEnabled = enabled;
    emit demoModeEnabledChanged();
}

void DemoController::setAutoPlay(bool enabled)
{
    if (m_autoPlay == enabled)
        return;
    m_autoPlay = enabled;
    if (m_metadataSource)
        m_metadataSource->setAutoPlay(enabled);
    emit autoPlayChanged();
}

void DemoController::setServerConnected(bool connected)
{
    if (m_serverConnection)
        m_serverConnection->setConnectionState(connected ? RiskTypes::ConnectionState::Connected
                                                           : RiskTypes::ConnectionState::Disconnected);
}

void DemoController::setCameraConnected(const QString &cameraId, bool connected)
{
    if (!m_videoManager)
        return;
    if (IVideoSource *source = m_videoManager->sourceFor(cameraId))
        source->setSimulatedConnected(connected);
}

void DemoController::setCameraRisk(const QString &cameraId, int riskLevel, double distanceM,
                                    const QString &exceptionState)
{
    if (!m_metadataSource)
        return;
    m_metadataSource->setRiskOverride(cameraId, RiskTypes::riskLevelFromInt(riskLevel), distanceM,
                                       RiskTypes::exceptionStateFromString(exceptionState));
}

void DemoController::clearCameraRisk(const QString &cameraId)
{
    if (m_metadataSource)
        m_metadataSource->clearRiskOverride(cameraId);
}

void DemoController::setPersonBBox(const QString &cameraId, double x, double y, double width, double height)
{
    if (m_metadataSource)
        m_metadataSource->setPersonBBoxOverride(cameraId, BBox(x, y, width, height));
}

void DemoController::setForkliftBBox(const QString &cameraId, double x, double y, double width, double height)
{
    if (m_metadataSource)
        m_metadataSource->setForkliftBBoxOverride(cameraId, BBox(x, y, width, height));
}

void DemoController::clearBBoxOverrides(const QString &cameraId)
{
    if (m_metadataSource)
        m_metadataSource->clearBBoxOverrides(cameraId);
}
