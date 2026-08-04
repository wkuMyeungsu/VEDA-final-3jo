#pragma once

#include <QUrl>
#include <atomic>
#include <thread>
#include "IVideoSource.h"

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

// 진짜 RTSP 카메라에 GStreamer로 접속하는 실제 구현체
// - 파이프라인: rtspsrc -> rtph264depay -> h264parse -> avdec_h264 -> videoconvert -> appsink
// - RGB 프레임 도착 시 onNewSample() 콜백이 QImage로 감싸 frameReady로 emit
// - GStreamer 버스 메시지는 워커 스레드(m_busThread)에서 폴링 (메인 스레드 안 막음)
// - 연결 끊김/에러 시 자동 재연결
class RtspVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent = nullptr);
    // 워커 스레드 정지 + 파이프라인 정리까지 확실히 마무리
    ~RtspVideoSource() override;

    // 파이프라인 생성 + 재생 시작 + 버스 워커 스레드 기동
    void start() override;
    // 재생 정지 + 워커 스레드 종료 + 파이프라인 해제
    void stop() override;

private:

    void busLoop(); //워커 스레드에서 실행되는 함수
    // 에러/EOS 발생 시 일정 시간 뒤 stop()+start()를 다시 호출해서 복구 시도
    void scheduleReconnect();

    QString m_cameraId;
    QUrl m_rtspUrl;

    GstElement *m_pipeline = nullptr;
    GstBus *m_bus = nullptr;

    std::thread m_busThread;               // busLoop()을 돌리는 워커 스레드
    std::atomic_bool m_stopRequested{false}; // 메인 스레드 <-> 워커 스레드 간 종료 신호

};