#pragma once

#include <QMediaPlayer>
#include <QUrl>
#include <QVideoSink>

#include "IVideoSource.h"

// 로컬 .mp4/.avi 파일을 반복 재생하는 영상 소스
// - 디코딩된 프레임을 다른 소스와 동일하게 IVideoSource로 내보냄
// - 실카메라 없을 때 녹화 영상으로 테스트하는 용도
// - QMediaPlayer(Qt Multimedia) 사용 (RtspVideoSource는 GStreamer 직접 사용)
class LocalFileVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    LocalFileVideoSource(QString cameraId, QUrl fileUrl, QObject *parent = nullptr);

    // 파일 재생 시작 (반복 재생 설정 포함)
    void start() override;
    // 재생 중지, 연결 상태를 Disconnected로 바꿈
    void stop() override;

private slots:
    // QVideoSink가 새 프레임을 디코딩할 때마다 호출
    void handleVideoFrame(const QVideoFrame &frame);
    // 파일이 없거나 코덱 문제 등으로 재생 실패 시 호출
    void handleMediaError(QMediaPlayer::Error error, const QString &errorString);

private:
    QString m_cameraId;
    QUrl m_fileUrl;
    QMediaPlayer m_player;
    QVideoSink m_sink;
    bool m_gotFirstFrame = false;   // 첫 프레임 도착 전까지는 "연결 중"으로만 표시
};
