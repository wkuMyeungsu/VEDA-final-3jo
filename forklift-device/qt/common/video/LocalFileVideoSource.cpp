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
    // QMediaPlayer -> QVideoSink -> handleVideoFrame() 순서로 프레임이 흘러들어옴
    m_player.setVideoSink(&m_sink);
    m_player.setLoops(QMediaPlayer::Infinite); // 파일이 끝나면 자동으로 처음부터 다시 재생
    m_player.setSource(m_fileUrl);

    connect(&m_sink, &QVideoSink::videoFrameChanged, this, &LocalFileVideoSource::handleVideoFrame);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, &LocalFileVideoSource::handleMediaError);
}

void LocalFileVideoSource::start()
{
    // 첫 프레임 오기 전까지는 "연결 중" -- Connected 전환은 handleVideoFrame()에서
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

    // QVideoFrame(내부 GPU/플랫폼 종속 포맷) -> QImage(다른 소스들과 공통 포맷)로 변환
    const QImage image = frame.toImage();
    if (image.isNull())
        return;

    // 첫 프레임이 실제로 도착한 시점에야 비로소 "연결됨"으로 승격
    if (!m_gotFirstFrame) {
        m_gotFirstFrame = true;
        setConnectionState(RiskTypes::ConnectionState::Connected);
    }

    emit frameReady(image);
}

// 파일 경로가 틀렸거나 코덱을 못 읽는 등 재생 자체가 실패한 경우
void LocalFileVideoSource::handleMediaError(QMediaPlayer::Error error, const QString &errorString)
{
    if (error == QMediaPlayer::NoError)
        return;

    qCWarning(lcLocalFile) << "camera" << m_cameraId << "local file playback error:" << errorString;
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}
