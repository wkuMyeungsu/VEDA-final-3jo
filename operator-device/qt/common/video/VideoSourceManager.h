#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "../models/CameraInfo.h"
#include "IVideoSource.h"

// Owns one IVideoSource per configured camera and hands out pointers by
// camera_id. This is the single place that decides which concrete source
// class to instantiate based on CameraInfo::sourceType -- VideoStream
// (and therefore QML) never constructs a video source directly, so it
// never knows whether a camera is Mock, a local file, or RTSP.
//
// One instance is created in each app's main.cpp; VideoStream reaches
// it through VideoSourceManager::instance().
class VideoSourceManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoSourceManager(QObject *parent = nullptr);
    ~VideoSourceManager() override;

    static VideoSourceManager *instance();

    void setCameras(const QVector<CameraInfo> &cameras);
    void startAll();
    void stopAll();

    IVideoSource *sourceFor(const QString &cameraId) const;

private:
    IVideoSource *createSource(const CameraInfo &info);

    QHash<QString, IVideoSource *> m_sources;
};
