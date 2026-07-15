#pragma once

#include <QMediaPlayer>
#include <QUrl>
#include <QVideoSink>

#include "IVideoSource.h"

// Plays a local .mp4/.avi file on loop and re-publishes decoded frames as
// QImage through the same IVideoSource interface as every other source.
// Useful for testing with a real recorded clip before a live RTSP feed is
// available.
class LocalFileVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    LocalFileVideoSource(QString cameraId, QUrl fileUrl, QObject *parent = nullptr);

    void start() override;
    void stop() override;

private slots:
    void handleVideoFrame(const QVideoFrame &frame);
    void handleMediaError(QMediaPlayer::Error error, const QString &errorString);

private:
    QString m_cameraId;
    QUrl m_fileUrl;
    QMediaPlayer m_player;
    QVideoSink m_sink;
    bool m_gotFirstFrame = false;
};
