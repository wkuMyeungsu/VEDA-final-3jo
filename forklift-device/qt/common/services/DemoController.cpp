#include "DemoController.h"

#include "../network/MockMetadataSource.h"
#include "../video/IVideoSource.h"
#include "../video/VideoSourceManager.h"
#include "ServerConnectionService.h"

// 세 의존 객체는 앱 시작 시 이미 만들어진 것을 그대로 받아서 보관 (여기서 새로 안 만듦)
DemoController::DemoController(MockMetadataSource *metadataSource, VideoSourceManager *videoManager,
                                ServerConnectionService *serverConnection, QObject *parent)
    : QObject(parent)
    , m_metadataSource(metadataSource)
    , m_videoManager(videoManager)
    , m_serverConnection(serverConnection)
{
}

// --demo 패널 자체의 표시 여부 -- QML이 이 값 보고 패널을 그릴지 결정
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

// 데모 버튼으로 "서버 연결 끊김" 상황을 흉내냄
void DemoController::setServerConnected(bool connected)
{
    if (m_serverConnection)
        m_serverConnection->setConnectionState(connected ? RiskTypes::ConnectionState::Connected
                                                           : RiskTypes::ConnectionState::Disconnected);
}

// 데모 버튼으로 특정 카메라만 "연결 끊김" 상황을 흉내냄
void DemoController::setCameraConnected(const QString &cameraId, bool connected)
{
    if (!m_videoManager)
        return;
    if (IVideoSource *source = m_videoManager->sourceFor(cameraId))
        source->setSimulatedConnected(connected);
}

// QML(DemoPanel.qml 버튼)이 넘긴 int/QString을 실제 enum으로 변환해서
// MockMetadataSource에 "이 카메라는 강제로 이 상태로 고정해라"라고 지시
void DemoController::setCameraRisk(const QString &cameraId, int riskLevel, double distanceM,
                                    const QString &exceptionState)
{
    if (!m_metadataSource)
        return;
    m_metadataSource->setRiskOverride(cameraId, RiskTypes::riskLevelFromInt(riskLevel), distanceM,
                                       RiskTypes::exceptionStateFromString(exceptionState));
}

// 강제 고정 해제 -- autoPlay가 켜져 있으면 다시 랜덤 값으로 돌아감
void DemoController::clearCameraRisk(const QString &cameraId)
{
    if (m_metadataSource)
        m_metadataSource->clearRiskOverride(cameraId);
}

// 사람 bbox를 지정 좌표로 고정 (랜덤 워크 대신 이 값 사용)
void DemoController::setPersonBBox(const QString &cameraId, double x, double y, double width, double height)
{
    if (m_metadataSource)
        m_metadataSource->setPersonBBoxOverride(cameraId, BBox(x, y, width, height));
}

// 지게차 bbox를 지정 좌표로 고정
void DemoController::setForkliftBBox(const QString &cameraId, double x, double y, double width, double height)
{
    if (m_metadataSource)
        m_metadataSource->setForkliftBBoxOverride(cameraId, BBox(x, y, width, height));
}

// bbox override를 둘 다 해제하고 랜덤 워크로 복귀
void DemoController::clearBBoxOverrides(const QString &cameraId)
{
    if (m_metadataSource)
        m_metadataSource->clearBBoxOverrides(cameraId);
}
