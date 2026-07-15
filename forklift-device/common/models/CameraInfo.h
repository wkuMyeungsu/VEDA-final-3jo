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

// Static description of a camera as read from config/cameras.json.
struct CameraInfo {
    QString cameraId;
    QString name;
    QString zone;
    VideoSourceType sourceType = VideoSourceType::Mock;
    QString rtspUrl;
    QString localFilePath;
};
