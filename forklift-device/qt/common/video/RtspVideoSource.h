#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QUrl>
#include <atomic>
#include <thread>
#include "IVideoSource.h"

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;

class QTimer;

// - RTSP 영상 수신 클래스 (GStreamer 기반 실시간 스트리밍 재생 및 ONVIF 메타데이터 수신 처리)
class RtspVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent = nullptr); // - 생성자: 카메라 ID, RTSP 주소, 부모 객체 지정 및 초기화
    ~RtspVideoSource() override;                                              // - 소멸자: 스레드 정지 및 파이프라인 리소스 해제

    void start() override;                                                     // - 재생 시작: 파이프라인 수립, 스트리밍 구동 및 감시 스레드 시작
    void stop() override;                                                      // - 재생 정지: 스트리밍 중단, 스레드 종료 및 파이프라인 해제

    // - 아래 두 함수는 GStreamer 스트리밍 스레드에서 불림 (콜백이 전역 함수라 public 필요)
    void pushFrame(const QImage &frame);                                       // - 프레임 전달: 최신 프레임 1장만 보관하고 메인 스레드 배달 예약
    void pushMetadata(const QByteArray &xml);                                  // - 메타데이터 전달: 최신 메타데이터 1건만 보관하고 메인 스레드 배달 예약

private:
    void busLoop();                                                            // - 상태 감시 루프: 워커 스레드에서 GStreamer 메시지 모니터링 실행
    void scheduleReconnect();                                                  // - 재연결 예약: 에러/종료 발생 시 일정 시간 후 재생 재시도
    void handleConnectTimeout();                                               // - 접속 시간 초과 처리: 제한 시간 내 연결 실패 시 재연결로 넘김
    void handleReconnectTimeout();                                             // - 재연결 시각 도달 처리: 파이프라인 정지 후 재구동
    void scheduleDelivery();                                                   // - 배달 예약: 메인 스레드로 넘길 이벤트를 1건만 등록
    void deliverPending();                                                     // - 배달 처리: 메인 스레드에서 보관 중인 최신 값 신호 발생

    QString m_cameraId;                                                        // - 카메라 ID: 카메라 식별자 보관
    QUrl m_rtspUrl;                                                            // - RTSP 주소: 스트리밍 접속 URL 보관

    GstElement *m_pipeline = nullptr;                                          // - 파이프라인 포인터: GStreamer 파이프라인 객체 참조
    GstBus *m_bus = nullptr;                                                   // - 버스 포인터: GStreamer 이벤트 버스 객체 참조

    std::thread m_busThread;                                                   // - 감시 스레드: 상태 메시지 모니터링 전용 스레드
    std::atomic_bool m_stopRequested{false};                                   // - 루프 종료 신호: 버스 스레드에게만 쓰는 종료 플래그

    QTimer *m_connectTimeoutTimer = nullptr;                                   // - 접속 감시 타이머: 제한 시간 내 연결 여부 확인용 (메인 스레드 전용)
    QTimer *m_reconnectTimer = nullptr;                                        // - 재연결 타이머: 다음 재시도 시각 예약용 (메인 스레드 전용)
    bool m_userStopped = false;                                                // - 수동 정지 상태: stop() 호출 후 자동 재연결을 막는 의도 플래그
    int m_reconnectDelayMs = 0;                                                // - 다음 재연결 대기 시간: 실패할수록 2배씩 증가, 접속 성공 시 0으로 초기화

    QMutex m_pendingMutex;                                                     // - 보관용 잠금: 스트리밍 스레드와 메인 스레드의 슬롯 접근 보호
    QImage m_pendingFrame;                                                     // - 프레임 슬롯: 아직 배달하지 않은 최신 프레임 1장
    QByteArray m_pendingMetadata;                                              // - 메타데이터 슬롯: 아직 배달하지 않은 최신 메타데이터 1건
    std::atomic_bool m_deliveryQueued{false};                                  // - 배달 예약 여부: 참이면 배달 이벤트가 큐에 정확히 1건 있음
    std::atomic_int m_droppedFrames{0};                                        // - 버린 프레임 수: 배달 전에 새 프레임으로 덮인 횟수 (소비 지연 지표)
    QElapsedTimer m_openClock;                                                 // - 오픈 시간 계측: start() 호출부터 첫 프레임까지 걸린 시간 측정
    bool m_firstFrameLogged = false;                                           // - 첫 프레임 기록 여부: 오픈 시간 로그를 1회만 남기기 위한 표시
};
