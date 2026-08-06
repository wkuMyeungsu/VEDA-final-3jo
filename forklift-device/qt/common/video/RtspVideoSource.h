#pragma once

#include <QUrl>
#include <atomic>
#include <thread>
#include "IVideoSource.h"

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

// - RTSP 영상 수신 클래스 (GStreamer 기반 실시간 스트리밍 재생 및 ONVIF 메타데이터 수신 처리)
class RtspVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent = nullptr); // - 생성자: 카메라 ID, RTSP 주소, 부모 객체 지정 및 초기화
    ~RtspVideoSource() override;                                              // - 소멸자: 스레드 정지 및 파이프라인 리소스 해제

    void start() override;                                                     // - 재생 시작: 파이프라인 수립, 스트리밍 구동 및 감시 스레드 시작
    void stop() override;                                                      // - 재생 정지: 스트리밍 중단, 스레드 종료 및 파이프라인 해제

private:
    void busLoop();                                                            // - 상태 감시 루프: 워커 스레드에서 GStreamer 메시지 모니터링 실행
    void scheduleReconnect();                                                  // - 재연결 예약: 에러/종료 발생 시 일정 시간 후 재생 재시도

    QString m_cameraId;                                                        // - 카메라 ID: 카메라 식별자 보관
    QUrl m_rtspUrl;                                                            // - RTSP 주소: 스트리밍 접속 URL 보관

    GstElement *m_pipeline = nullptr;                                          // - 파이프라인 포인터: GStreamer 파이프라인 객체 참조
    GstBus *m_bus = nullptr;                                                   // - 버스 포인터: GStreamer 이벤트 버스 객체 참조

    std::thread m_busThread;                                                   // - 감시 스레드: 상태 메시지 모니터링 전용 스레드
    std::atomic_bool m_stopRequested{false};                                   // - 정지 요청 플래그: 스레드 간 안전한 종료 신호 보관
};
