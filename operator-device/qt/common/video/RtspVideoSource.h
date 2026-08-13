#pragma once

#include <QImage>
#include <QMutex>
#include <QUrl>
#include <atomic>
#include <thread>
#include "IVideoSource.h"

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

// 진짜 RTSP 카메라에 GStreamer로 접속하는 실제 구현체
// - 파이프라인: rtspsrc -> rtph264depay -> h264parse -> avdec_h264 -> videoconvert -> appsink
// - RGB 프레임 도착 시 onNewSample() 콜백이 QImage로 감싸 pushFrame()에 전달
// - pushFrame()은 단일 슬롯 메일박스에 최신 프레임만 남기고(latest-wins) 메인 스레드로
//   배달을 예약 -- 생산 속도가 소비 속도를 앞질러도 Qt 이벤트 큐가 무한히 쌓이지 않음
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

    // GStreamer 스트리밍 스레드에서 호출되는 진입점.
    // onNewSample()이 익명 네임스페이스의 자유 함수라 private 멤버에 직접 못 닿기 때문에
    // 이 public 메서드 하나만 노출하고, 뮤텍스/큐잉 관련 상태는 전부 private에 둔다.
    void pushFrame(const QImage &frame);

private:

    void busLoop(); //워커 스레드에서 실행되는 함수
    // 에러/EOS 발생 시 일정 시간 뒤 stop()+start()를 다시 호출해서 복구 시도
    void scheduleReconnect();

    // 메인 스레드로 배달을 1건만 예약한다 (Qt 이벤트 큐 중복 등록 방지)
    void scheduleDelivery();
    // 메인 스레드에서 실행 -- 슬롯에 남은 최신 프레임을 꺼내 frameReady로 emit
    void deliverPending();

    QString m_cameraId;
    QUrl m_rtspUrl;

    GstElement *m_pipeline = nullptr;
    GstBus *m_bus = nullptr;

    std::thread m_busThread;               // busLoop()을 돌리는 워커 스레드
    std::atomic_bool m_stopRequested{false}; // 메인 스레드 <-> 워커 스레드 간 종료 신호

    // 단일 슬롯 메일박스 (latest-wins) -- 프레임 생산 속도가 소비 속도를 앞지를 때
    // Qt 이벤트 큐에 QImage가 무한히 쌓이는 것을 막기 위한 배압(backpressure) 장치.
    // 큐에 항상 최신 프레임 1장만 남기고, 그 사이 도착한 프레임은 버린다.
    QMutex m_pendingMutex;                  // m_pendingFrame 보호용 (스트리밍 스레드 <-> 메인 스레드)
    QImage m_pendingFrame;                  // 슬롯: 아직 메인 스레드로 전달되지 않은 최신 프레임
    std::atomic_bool m_deliveryQueued{false}; // true면 deliverPending() 이벤트가 정확히 1건 큐에 있다는 뜻
    std::atomic_int m_droppedFrames{0};     // 슬롯에 이미 프레임이 있어서 덮어쓴(=버려진) 횟수, stop() 시 로그용

};