#include "CameraInfo.h"

QString videoSourceTypeToString(VideoSourceType type)
{
    switch (type) {
    case VideoSourceType::Mock: return QStringLiteral("mock");
    case VideoSourceType::LocalFile: return QStringLiteral("local_file");
    case VideoSourceType::Rtsp: return QStringLiteral("rtsp");
    }
    return QStringLiteral("mock");
}

VideoSourceType videoSourceTypeFromString(const QString &value)
{
    if (value == QStringLiteral("local_file")) return VideoSourceType::LocalFile;
    if (value == QStringLiteral("rtsp")) return VideoSourceType::Rtsp;
    return VideoSourceType::Mock;
}
