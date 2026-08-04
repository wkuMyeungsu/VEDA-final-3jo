#pragma once

#include <QObject>

#include "../models/BBox.h"
#include "../models/Types.h"

class MockMetadataSource;
class VideoSourceManager;
class ServerConnectionService;

// --demo 패널(개발/시연용 조작 버튼)이 호출하는 대상
// - QML에 "demoController" context property로 노출 (두 앱 공통)
// - --demo 옵션 없으면 QML이 패널 자체를 안 띄워서 호출될 일 없음
// - 실제 값 생성은 MockMetadataSource, 이 클래스는 QML 호출을 그쪽으로 연결하는 다리
class DemoController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool demoModeEnabled READ demoModeEnabled WRITE setDemoModeEnabled NOTIFY demoModeEnabledChanged)
    Q_PROPERTY(bool autoPlay READ autoPlay WRITE setAutoPlay NOTIFY autoPlayChanged)

public:
    // metadataSource/videoManager/serverConnection은 앱이 이미 만들어둔 걸 그대로 받음
    DemoController(MockMetadataSource *metadataSource, VideoSourceManager *videoManager,
                   ServerConnectionService *serverConnection, QObject *parent = nullptr);

    bool demoModeEnabled() const { return m_demoModeEnabled; }
    // --demo 패널 자체를 보여줄지 (QML이 이 값 보고 패널 렌더링 여부 결정)
    void setDemoModeEnabled(bool enabled);

    // true: MockMetadataSource가 매 tick마다 자동으로 랜덤 이벤트 생성 (데모가 "살아있는" 느낌)
    // false: 수동으로 setCameraRisk() 등을 호출했을 때만 값이 바뀜 (시나리오 재현용)
    bool autoPlay() const { return m_autoPlay; }
    void setAutoPlay(bool enabled);

    // 데모 버튼: 서버 연결 끊김 상황을 흉내냄
    Q_INVOKABLE void setServerConnected(bool connected);
    // 데모 버튼: 특정 카메라만 연결 끊김 상황을 흉내냄
    Q_INVOKABLE void setCameraConnected(const QString &cameraId, bool connected);

    // riskLevel: 0=Safe 1=Caution 2=Danger 3=Emergency.
    // exceptionState: "NONE" | "SENSOR_FAULT" | "DEAD_RECKONING" | "EMERGENCY_IMPACT" |
    //                 "NETWORK_DISCONNECTED" | "CAMERA_DISCONNECTED" | "UNCONFIRMED_PROXIMITY"
    //                 (문자열은 RiskTypes::exceptionStateFromString()이 인식하는 값과 동일해야 함)
    Q_INVOKABLE void setCameraRisk(const QString &cameraId, int riskLevel, double distanceM,
                                   const QString &exceptionState);
    // 강제 고정 해제 -- autoPlay면 다시 랜덤 값으로 복귀
    Q_INVOKABLE void clearCameraRisk(const QString &cameraId);

    // x/y/width/height는 0.0~1.0 정규화 좌표 (실제 픽셀 아님 -- 영상 해상도와 무관하게 동작)
    Q_INVOKABLE void setPersonBBox(const QString &cameraId, double x, double y, double width, double height);
    Q_INVOKABLE void setForkliftBBox(const QString &cameraId, double x, double y, double width, double height);
    // 사람/지게차 bbox override를 둘 다 해제
    Q_INVOKABLE void clearBBoxOverrides(const QString &cameraId);

signals:
    void demoModeEnabledChanged();
    void autoPlayChanged();

protected:
    // protected인 이유: OperatorDemoController(forklift-device)가 상속해서 확장함
    MockMetadataSource *m_metadataSource;
    VideoSourceManager *m_videoManager;
    ServerConnectionService *m_serverConnection;

private:
    bool m_demoModeEnabled = false;
    bool m_autoPlay = true;
};
