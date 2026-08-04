#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "../models/CameraInfo.h"
#include "IVideoSource.h"

// 카메라마다 IVideoSource 1개씩 소유하고 camera_id로 꺼내주는 창구
// - 카메라 종류(Mock/LocalFile/Rtsp) 판단은 createSource()가 전담
// - VideoStream(QML)은 이 매니저에서만 소스를 받고, 직접 안 만듦
// - 앱마다 1개만 생성되는 싱글턴, instance()로 접근
class VideoSourceManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoSourceManager(QObject *parent = nullptr);
    // 보유 중인 IVideoSource들도 QObject 부모-자식 관계로 같이 정리됨
    ~VideoSourceManager() override;

    // 마지막으로 생성된 인스턴스를 반환 (앱당 1개만 만든다는 전제)
    static VideoSourceManager *instance();

    // 기존 소스는 전부 삭제하고 새 카메라 목록으로 다시 생성 (앱 시작 시 1회 호출)
    void setCameras(const QVector<CameraInfo> &cameras);
    // 모든 카메라의 start() 일괄 호출
    void startAll();
    // 모든 카메라의 stop() 일괄 호출
    void stopAll();

    // cameraId에 해당하는 소스 반환, 없으면 nullptr
    IVideoSource *sourceFor(const QString &cameraId) const;

private:
    // sourceType에 따라 Mock/LocalFile/Rtsp 중 실제로 인스턴스화할 클래스를 결정
    IVideoSource *createSource(const CameraInfo &info);

    QHash<QString, IVideoSource *> m_sources;
};
