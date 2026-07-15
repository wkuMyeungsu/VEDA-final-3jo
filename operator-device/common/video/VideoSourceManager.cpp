#include "VideoSourceManager.h"

#include <QLoggingCategory>

#include "LocalFileVideoSource.h"
#include "MockVideoSource.h"
#include "RtspVideoSource.h"

namespace {
Q_LOGGING_CATEGORY(lcVideoManager, "safety.video.manager")
VideoSourceManager *g_instance = nullptr;
}

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
    qDeleteAll(m_sources);
    m_sources.clear();

    for (const CameraInfo &info : cameras) {
        if (IVideoSource *source = createSource(info))
            m_sources.insert(info.cameraId, source);
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

    qCWarning(lcVideoManager) << "unknown source type for camera" << info.cameraId << "-- falling back to mock";
    return new MockVideoSource(info.cameraId, info.zone, this);
}
