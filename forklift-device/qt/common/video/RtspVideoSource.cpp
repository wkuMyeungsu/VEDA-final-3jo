#include "RtspVideoSource.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcRtsp, "safety.video.rtsp")
}

RtspVideoSource::RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent)
    : IVideoSource(parent)
    , m_cameraId(std::move(cameraId))
    , m_rtspUrl(std::move(rtspUrl))
{
}

void RtspVideoSource::start()
{
    qCWarning(lcRtsp) << "camera" << m_cameraId
                       << "source_type=rtsp but no GStreamer pipeline is implemented yet ("
                       << m_rtspUrl.toString() << "). Reporting DISCONNECTED instead of crashing.";
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void RtspVideoSource::stop()
{
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}
