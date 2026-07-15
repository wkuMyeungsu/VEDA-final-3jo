#include "LocalFileVideoSource.h"

#include <QLoggingCategory>
#include <QVideoFrame>

namespace {
Q_LOGGING_CATEGORY(lcLocalFile, "safety.video.localfile")
}

LocalFileVideoSource::LocalFileVideoSource(QString cameraId, QUrl fileUrl, QObject *parent)
    : IVideoSource(parent)
    , m_cameraId(std::move(cameraId))
    , m_fileUrl(std::move(fileUrl))
{
    m_player.setVideoSink(&m_sink);
    m_player.setLoops(QMediaPlayer::Infinite);
    m_player.setSource(m_fileUrl);

    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &LocalFileVideoSource::handleVideoFrame);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, &LocalFileVideoSource::handleMediaError);
}

void LocalFileVideoSource::start()
{
    setConnectionState(RiskTypes::ConnectionState::Connecting);
    m_gotFirstFrame = false;
    m_player.play();
}

void LocalFileVideoSource::stop()
{
    m_player.stop();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void LocalFileVideoSource::handleVideoFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    const QImage image = frame.toImage();
    if (image.isNull())
        return;

    if (!m_gotFirstFrame) {
        m_gotFirstFrame = true;
        setConnectionState(RiskTypes::ConnectionState::Connected);
    }

    emit frameReady(image);
}

void LocalFileVideoSource::handleMediaError(QMediaPlayer::Error error, const QString &errorString)
{
    if (error == QMediaPlayer::NoError)
        return;

    qCWarning(lcLocalFile) << "camera" << m_cameraId << "local file playback error:" << errorString;
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}
