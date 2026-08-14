#include "VideoSourceManager.h"

#include <QLoggingCategory>

#include "LocalFileVideoSource.h"
#include "MockVideoSource.h"
#include "RtspVideoSource.h"

namespace {
Q_LOGGING_CATEGORY(lcVideoManager, "safety.video.manager")
VideoSourceManager *g_instance = nullptr;
}

// 프로세스당 1개만 허용 (싱글턴) -- 두 번째로 만들려고 하면 디버그 빌드에서 assert로 즉시 걸림
VideoSourceManager::VideoSourceManager(QObject *parent)
    : QObject(parent)
{
    Q_ASSERT_X(g_instance == nullptr, "VideoSourceManager", "only one instance is supported per process");
    g_instance = this;
}

VideoSourceManager::~VideoSourceManager()
{
    if (g_instance == this)
        g_instance = nullptr;
}

VideoSourceManager *VideoSourceManager::instance()
{
    return g_instance;
}

void VideoSourceManager::setCameras(const QVector<CameraInfo> &cameras)
{
    // 재호출 시 이전 소스는 전부 정리 (재연결/재시작 시나리오 대비)
    qDeleteAll(m_sources);
    m_sources.clear();

    for (const CameraInfo &info : cameras) {
        if (IVideoSource *source = createSource(info)) {
            const QString effId = info.effectiveId();
            m_sources.insert(effId, source);
            if (!info.streamId.isEmpty() && info.streamId != effId)
                m_sources.insert(info.streamId, source);
            if (!info.cameraId.isEmpty() && !m_sources.contains(info.cameraId))
                m_sources.insert(info.cameraId, source);
        }
    }
}

void VideoSourceManager::startAll()
{
    for (IVideoSource *source : std::as_const(m_sources))
        source->start();
}

void VideoSourceManager::stopAll()
{
    for (IVideoSource *source : std::as_const(m_sources))
        source->stop();
}

IVideoSource *VideoSourceManager::sourceFor(const QString &cameraId) const
{
    return m_sources.value(cameraId, nullptr);
}

// "카메라 종류 -> 실제 C++ 클래스" 매핑이 이 함수 하나뿐
// 새 소스 타입 추가 시 case만 늘리면 됨
IVideoSource *VideoSourceManager::createSource(const CameraInfo &info)
{
    switch (info.sourceType) {
    case VideoSourceType::Mock:
        return new MockVideoSource(info.cameraId, info.zone, this);
    case VideoSourceType::LocalFile:
        return new LocalFileVideoSource(info.cameraId, QUrl::fromLocalFile(info.localFilePath), this);
    case VideoSourceType::Rtsp:
        return new RtspVideoSource(info.cameraId, QUrl(info.rtspUrl), this);
    }

    // enum에 없는 값(설정 파일 오타 등) -- 앱이 죽는 대신 Mock으로 대체해서 계속 동작하게 함
    qCWarning(lcVideoManager) << "unknown source type for camera" << info.cameraId << "-- falling back to mock";
    return new MockVideoSource(info.cameraId, info.zone, this);
}
