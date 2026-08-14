#pragma once

#include <QString>

// Which concrete IVideoSource implementation VideoSourceManager should
// instantiate for a camera. This is the single switch that turns a camera
// from Mock into a real feed -- see docs/INTEGRATION.md.
enum class VideoSourceType {
    Mock,
    LocalFile,
    Rtsp
};

QString videoSourceTypeToString(VideoSourceType type);
VideoSourceType videoSourceTypeFromString(const QString &value);

struct CameraInfo {
    QString streamId;
    QString cameraId;
    int channel = 0;
    QString name;
    QString zone;
    VideoSourceType sourceType = VideoSourceType::Mock;
    QString rtspUrl;
    QString localFilePath;

    QString effectiveId() const { return streamId.isEmpty() ? cameraId : streamId; }
};
