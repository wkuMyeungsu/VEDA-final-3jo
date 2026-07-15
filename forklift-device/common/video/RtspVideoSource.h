#pragma once

#include <QUrl>

#include "IVideoSource.h"

// Skeleton for a future GStreamer-based RTSP source. This is the intended
// integration point for real cameras -- see docs/INTEGRATION.md. Until the
// pipeline below is implemented, start() simply reports DISCONNECTED so the
// rest of the app degrades gracefully instead of crashing or hanging.
class RtspVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent = nullptr);

    void start() override;
    void stop() override;

private:
    QString m_cameraId;
    QUrl m_rtspUrl;

    // TODO(RTSP integration): build and run a GStreamer pipeline such as
    //   rtspsrc location=<rtspUrl> latency=200 ! decodebin ! videoconvert !
    //   appsink
    // on a worker thread, pull decoded buffers from the appsink, convert
    // each to QImage, and emit frameReady(). Call setConnectionState(...)
    // to reflect the actual pipeline state (Connecting while negotiating,
    // Connected once frames flow, Disconnected on pipeline error/EOS).
};
