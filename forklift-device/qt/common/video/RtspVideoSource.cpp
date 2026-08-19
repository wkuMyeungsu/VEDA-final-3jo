#include "RtspVideoSource.h"
#include <QLoggingCategory>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcRtsp, "safety.video.rtsp")                                // - 로깅 카테고리 정의: RTSP 통신 및 영상 수신 로그 분류용 이름 지정

constexpr int kConnectTimeoutMs = 5000;                                        // - 접속 제한 시간 (5초): 파이 실측상 오픈에 1.1~2초+가 걸려 2초로는 정상 접속도 끊겨서 늘림
constexpr int kReconnectBaseDelayMs = 1000;                                    // - 재연결 시작 대기 시간 (1초): 첫 재시도 간격
constexpr int kReconnectMaxDelayMs = 30000;                                    // - 재연결 최대 대기 시간 (30초): 계속 실패하는 채널의 재시도 폭주 방지
constexpr int kBusPollMs = 50;                                                 // - 버스 확인 주기 (50ms): 정지 요청에 늦어도 이만큼 안에 반응

GstFlowReturn onNewSample(GstElement *appsink, gpointer userData)
{
    auto *self = static_cast<RtspVideoSource *>(userData);                       // - 객체 참조 획득: 콜백 데이터에서 RtspVideoSource 객체 추출

    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));       // - 샘플 추출: GStreamer 수신 버퍼에서 샘플 획득
    if (!sample)                                                                // - 유효성 검증: 샘플 추출 실패 시 오류 반환
        return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);                          // - 버퍼 추출: 샘플 내 데이터 버퍼 획득
    GstCaps *caps = gst_sample_get_caps(sample);                               // - 포맷 정보 추출: 프레임 캡스 데이터 획득
    GstStructure *capsStruct = gst_caps_get_structure(caps, 0);                 // - 포맷 구조체 추출: 첫 번째 캡스 구조체 수집
    int width = 0, height = 0;                                                  // - 해상도 변수 선언: 프레임 가로/세로 크기 변수
    gst_structure_get_int(capsStruct, "width", &width);                        // - 가로 크기 수집: 정수형 width 값 읽기
    gst_structure_get_int(capsStruct, "height", &height);                      // - 세로 크기 수집: 정수형 height 값 읽기

    GstMapInfo map;                                                             // - 매핑 구조체 생성: 메모리 접근용 매핑 정보 객체
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {                           // - 메모리 매핑: 버퍼 데이터 읽기 모드 매핑 성공 시 처리
        QImage frame(map.data, width, height, QImage::Format_RGB888);            // - 프레임 이미지 생성: 수신 데이터 기반 RGB888 이미지 생성
        QImage copy = frame.copy();                                             // - 이미지 복사: 스레드 간 안전한 전달을 위한 복사본 생성
        gst_buffer_unmap(buffer, &map);                                         // - 매핑 해제: 데이터 버퍼 매핑 종료

        self->pushFrame(copy);                                                  // - 프레임 보관: 최신 1장만 남기고 메인 스레드 배달 예약 (큐 무한 증가 방지)
    }

    gst_sample_unref(sample);                                                   // - 샘플 해제: 사용 완료된 샘플 메모리 반환
    return GST_FLOW_OK;                                                         // - 처리 성공: 정상 처리 상태 반환
}

GstFlowReturn onNewMetadataSample(GstElement *appsink, gpointer userData)
{
    auto *self = static_cast<RtspVideoSource *>(userData);                       // - 객체 참조 획득: 콜백 데이터에서 RtspVideoSource 객체 추출

    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));       // - 샘플 추출: GStreamer 수신 버퍼에서 메타데이터 샘플 획득
    if (!sample)                                                                // - 유효성 검증: 샘플 추출 실패 시 오류 반환
        return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);                          // - 버퍼 추출: 샘플 내 데이터 버퍼 획득
    GstMapInfo map;                                                             // - 매핑 구조체 생성: 메모리 접근용 매핑 정보 객체
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {                           // - 메모리 매핑: 버퍼 데이터 읽기 모드 매핑 성공 시 처리
        const QByteArray xml(reinterpret_cast<const char *>(map.data), static_cast<int>(map.size)); // - 데이터 변환: 메타데이터 바이너리를 QByteArray로 변환
        gst_buffer_unmap(buffer, &map);                                         // - 매핑 해제: 데이터 버퍼 매핑 종료

        qCDebug(lcRtsp) << "onvif metadata bytes:" << xml.size();               // - 디버그 로그: 수신된 메타데이터 크기 기록

        self->pushMetadata(xml);                                                // - 메타데이터 보관: 최신 1건만 남기고 메인 스레드 배달 예약
    }

    gst_sample_unref(sample);                                                   // - 샘플 해제: 사용 완료된 샘플 메모리 반환
    return GST_FLOW_OK;                                                         // - 처리 성공: 정상 처리 상태 반환
}

}

RtspVideoSource::RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent)
    : IVideoSource(parent)
    , m_cameraId(std::move(cameraId))                                           // - 카메라 ID 저장: 카메라 식별 문자열 보관
    , m_rtspUrl(std::move(rtspUrl))                                             // - RTSP URL 저장: 스트리밍 접속 주소 보관
{
    // - 타이머를 멤버로 소유하면 start()를 다시 불러도 새로 생기지 않고 다시 시작될 뿐이라
    // - "타이머 1개 = 예약 최대 1건"이 보장됨 (기존 singleShot은 부를 때마다 예약이 쌓였음)
    m_connectTimeoutTimer = new QTimer(this);                                   // - 접속 감시 타이머 생성: 부모를 this로 지정해 메인 스레드 소속 및 자동 해제
    m_connectTimeoutTimer->setSingleShot(true);                                 // - 1회성 설정: 발화 후 스스로 정지
    connect(m_connectTimeoutTimer, &QTimer::timeout, this, &RtspVideoSource::handleConnectTimeout); // - 신호 연결: 시간 초과 처리 함수 연결

    m_reconnectTimer = new QTimer(this);                                        // - 재연결 타이머 생성: 부모를 this로 지정해 메인 스레드 소속 및 자동 해제
    m_reconnectTimer->setSingleShot(true);                                      // - 1회성 설정: 발화 후 스스로 정지
    connect(m_reconnectTimer, &QTimer::timeout, this, &RtspVideoSource::handleReconnectTimeout); // - 신호 연결: 재연결 처리 함수 연결
}

RtspVideoSource::~RtspVideoSource()
{
    stop();                                                                     // - 소멸자: 실행 중인 파이프라인 정지 및 리소스 정리
}

void RtspVideoSource::start()
{
    if (m_pipeline)                                                             // - 중복 실행 방지: 이미 파이프라인이 생성되어 실행 중인 경우 생략
        return;

    m_userStopped = false;                                                      // - 수동 정지 해제: handleReconnectTimeout()이 stop() 다음에 이 함수를 부르므로 여기서 반드시 풀어줘야 재연결이 이어짐
    m_openClock.start();                                                        // - 오픈 시간 측정 시작: 첫 프레임까지 걸린 시간을 재기 위한 기준점
    m_firstFrameLogged = false;                                                 // - 기록 표시 초기화: 이번 구동의 첫 프레임 로그를 다시 남기도록 설정

    const QByteArray url = m_rtspUrl.toString().toUtf8();                       // - URL 문자열 변환: UTF-8 바이너리로 변환
    QByteArray description = "rtspsrc protocols=tcp latency=100 location=\"" + url + "\""
        + " ! decodebin ! videoconvert !"
          " video/x-raw,format=RGB ! appsink name=sink emit-signals=true sync=false max-buffers=2 drop=true"; // - 파이프라인 문자열 생성: 자동 코덱(H.264/H.265) 디코딩 및 RGB appsink 출력

    GError *error = nullptr;                                                    // - 에러 객체 생성: 파이프라인 생성 에러 보관용
    m_pipeline = gst_parse_launch(description.constData(), &error);             // - 파이프라인 생성: 문자열 기반 GStreamer 파이프라인 수립
    if (!m_pipeline || error) {                                                 // - 생성 검증: 파이프라인 생성 실패 시 에러 로그 출력 및 상태 변경
        qCWarning(lcRtsp) << "camera" << m_cameraId << "failed to build pipeline:"
                           << (error ? error->message : "unknown error");
        if (error)
            g_error_free(error);                                                // - 에러 해제: 생성된 에러 메모리 반환
        m_pipeline = nullptr;                                                   // - 포인터 초기화: 파이프라인 포인터 해제
        setConnectionState(RiskTypes::ConnectionState::Disconnected);           // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
        return;
    }

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");     // - 엘리먼트 추출: 영상 디코딩 출력용 appsink 획득
    g_signal_connect(appsink, "new-sample", G_CALLBACK(onNewSample), this);     // - 신호 연결: 새 프레임 도착 콜백 함수 연결
    gst_object_unref(appsink);                                                  // - 참조 해제: 가져온 엘리먼트 참조 카운트 감소

    if (GstElement *metaSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "metasink")) { // - 메타데이터 엘리먼트 추출: ONVIF 메타데이터 출력용 metasink 획득
        g_signal_connect(metaSink, "new-sample", G_CALLBACK(onNewMetadataSample), this); // - 신호 연결: 새 메타데이터 도착 콜백 함수 연결
        gst_object_unref(metaSink);                                             // - 참조 해제: 가져온 엘리먼트 참조 카운트 감소
    }

    m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));                     // - 버스 획득: 파이프라인 상태 메시지 모니터링용 버스 추출

    m_stopRequested = false;                                                    // - 정지 플래그 초기화: 스레드 루프 실행 가능 상태 설정
    m_busThread = std::thread(&RtspVideoSource::busLoop, this);                 // - 스레드 생성: 파이프라인 메시지 수신 전용 스레드 시작

    setConnectionState(RiskTypes::ConnectionState::Connecting);                // - 상태 갱신: 연결 상태를 '연결 중'으로 변경
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);                       // - 재생 시작: 파이프라인 상태를 PLAYING으로 변경

    m_connectTimeoutTimer->start(kConnectTimeoutMs);                            // - 연결 타임아웃 감시: 제한 시간 내 연결 미완료 시 재연결로 넘김 (이미 돌고 있으면 재시작될 뿐)
}

void RtspVideoSource::handleConnectTimeout()
{
    if (!m_pipeline || connectionState() != RiskTypes::ConnectionState::Connecting) // - 상태 확인: 이미 연결됐거나 정지된 경우 처리 생략
        return;

    qCWarning(lcRtsp) << "camera" << m_cameraId << "connect timed out, retrying"; // - 경고 로그: 접속 시간 초과 기록

    scheduleReconnect();                                                        // - 재연결 예약: 즉시 재시작하지 않고 대기 시간을 두고 재시도 (실패 채널의 무한 재시도 방지)
}

void RtspVideoSource::handleReconnectTimeout()
{
    if (m_userStopped)                                                          // - 수동 정지 확인: 예약 후 정지 요청이 들어온 경우 재시도 생략
        return;

    if (connectionState() == RiskTypes::ConnectionState::Connected)             // - 연결 확인: 대기 중에 늦게 붙은 스트림은 다시 끊지 않음
        return;

    stop();                                                                     // - 기존 리소스 정리: 이전 파이프라인 정지 처리
    start();                                                                    // - 파이프라인 재구동: 재생 재시도
}

void RtspVideoSource::stop()
{
    m_userStopped = true;                                                       // - 수동 정지 표시: 예약된 재연결이 되살아나지 않도록 의도를 남김
    m_connectTimeoutTimer->stop();                                              // - 접속 감시 취소: 예약된 시간 초과 처리 제거
    m_reconnectTimer->stop();                                                   // - 재연결 예약 취소: 대기 중인 재시도 제거

    m_stopRequested = true;                                                     // - 정지 요청 설정: 스레드 루프 종료 플래그 설정
    if (m_busThread.joinable())                                                 // - 스레드 종료 대기: 버스 스레드 정상 종료까지 대기 후 합류
        m_busThread.join();

    if (m_pipeline) {                                                           // - 파이프라인 정리: 파이프라인 상태 초기화 및 메모리 해제
        gst_element_set_state(m_pipeline, GST_STATE_NULL);                      // - 상태 초기화: 파이프라인 상태를 NULL로 변경
        gst_object_unref(m_pipeline);                                           // - 메모리 해제: 파이프라인 객체 해제
        m_pipeline = nullptr;                                                   // - 포인터 초기화: 파이프라인 포인터 초기화
    }
    if (m_bus) {                                                                // - 버스 정리: 버스 객체 해제
        gst_object_unref(m_bus);                                                // - 메모리 해제: 버스 객체 메모리 해제
        m_bus = nullptr;                                                        // - 포인터 초기화: 버스 포인터 초기화
    }

    {
        QMutexLocker locker(&m_pendingMutex);                                   // - 보관 슬롯 잠금: 스트리밍 스레드와 동시 접근 방지
        m_pendingFrame = QImage();                                              // - 프레임 슬롯 비우기: 배달 못 한 이미지 메모리 반환
        m_pendingMetadata.clear();                                              // - 메타데이터 슬롯 비우기: 배달 못 한 데이터 메모리 반환
    }
    // - m_deliveryQueued는 일부러 건드리지 않음: "참이면 큐에 배달 이벤트 1건"이라는 약속을 깨면
    // - 이미 큐에 있는 이벤트 위에 하나가 더 얹힐 수 있음. 남은 이벤트는 빈 슬롯을 보고 그냥 플래그만 내림

    const int dropped = m_droppedFrames.exchange(0);                            // - 버린 프레임 수 확인: 값을 읽으면서 다음 구동을 위해 0으로 초기화
    if (dropped > 0)                                                            // - 기록 조건: 버린 프레임이 있을 때만 로그를 남김
        qCInfo(lcRtsp) << "camera" << m_cameraId << "dropped" << dropped << "frames (display too slow)"; // - 정보 로그: 화면 소비가 얼마나 밀렸는지 기록

    setConnectionState(RiskTypes::ConnectionState::Disconnected);               // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
}

void RtspVideoSource::scheduleReconnect()
{
    if (m_userStopped)                                                          // - 수동 정지 확인: 사용자가 멈춘 소스는 자동으로 되살리지 않음
        return;

    setConnectionState(RiskTypes::ConnectionState::Disconnected);               // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경

    if (m_reconnectTimer->isActive())                                           // - 중복 예약 차단: 이미 재시도가 잡혀 있으면 시각을 뒤로 밀지 않음
        return;

    if (m_reconnectDelayMs <= 0)                                                // - 첫 실패 확인: 대기 시간이 비어 있으면 기본값부터 시작
        m_reconnectDelayMs = kReconnectBaseDelayMs;

    const int delay = m_reconnectDelayMs;                                       // - 이번 대기 시간 확정: 다음 시도용 증가 전 값 사용
    m_reconnectDelayMs = qMin(m_reconnectDelayMs * 2, kReconnectMaxDelayMs);    // - 다음 대기 시간 증가: 실패할수록 간격을 2배씩 늘림(상한 30초)

    qCInfo(lcRtsp) << "camera" << m_cameraId << "reconnecting in" << delay << "ms"; // - 정보 로그: 다음 재시도까지 남은 시간 기록

    m_reconnectTimer->start(delay);                                             // - 재연결 예약: 대기 시간 후 재구동 (타이머가 1개라 예약도 최대 1건)
}

void RtspVideoSource::pushFrame(const QImage &frame)
{
    {
        QMutexLocker locker(&m_pendingMutex);                                   // - 보관 슬롯 잠금: 메인 스레드와 동시 접근 방지
        if (!m_pendingFrame.isNull())                                           // - 미배달 확인: 아직 안 가져간 프레임이 있으면 그 장은 버려지는 것
            m_droppedFrames.fetch_add(1);                                       // - 버린 수 증가: 소비 지연 정도를 세어 둠
        m_pendingFrame = frame;                                                 // - 최신 프레임 덮어쓰기: 화면에 필요한 건 항상 최신 1장뿐
    }

    scheduleDelivery();                                                         // - 배달 예약: 메인 스레드로 넘길 이벤트 등록
}

void RtspVideoSource::pushMetadata(const QByteArray &xml)
{
    {
        QMutexLocker locker(&m_pendingMutex);                                   // - 보관 슬롯 잠금: 메인 스레드와 동시 접근 방지
        m_pendingMetadata = xml;                                                // - 최신 메타데이터 덮어쓰기: 위험 판정에 쓰는 건 항상 최신 1건뿐
    }

    scheduleDelivery();                                                         // - 배달 예약: 메인 스레드로 넘길 이벤트 등록
}

void RtspVideoSource::scheduleDelivery()
{
    if (m_deliveryQueued.exchange(true))                                        // - 중복 예약 차단: 이미 배달 이벤트가 큐에 있으면 추가로 넣지 않음
        return;

    QMetaObject::invokeMethod(this, [this]() {                                  // - 비동기 배달 요청: Qt 메인 스레드에서 보관 값을 꺼내도록 예약
        deliverPending();
    }, Qt::QueuedConnection);
}

void RtspVideoSource::deliverPending()
{
    // - 슬롯을 꺼내기 "전에" 플래그를 내려야 함. 순서가 반대면 꺼낸 뒤 플래그를 내리기 전에 들어온
    // - 프레임이 예약을 건너뛰는데 배달은 이미 끝나서, 그 프레임이 슬롯에 계속 남게 됨
    m_deliveryQueued = false;                                                   // - 예약 해제: 새 프레임이 다음 배달을 예약할 수 있도록 먼저 풀어줌

    QImage frame;                                                               // - 배달용 프레임: 잠금 밖에서 신호를 보내기 위한 지역 변수
    QByteArray metadata;                                                        // - 배달용 메타데이터: 잠금 밖에서 신호를 보내기 위한 지역 변수
    {
        QMutexLocker locker(&m_pendingMutex);                                   // - 보관 슬롯 잠금: 스트리밍 스레드와 동시 접근 방지
        frame = m_pendingFrame;                                                 // - 프레임 꺼내기: 내부 공유 방식이라 복사 비용은 거의 없음
        m_pendingFrame = QImage();                                              // - 프레임 슬롯 비우기: 같은 장을 두 번 보내지 않도록 초기화
        metadata = m_pendingMetadata;                                           // - 메타데이터 꺼내기: 내부 공유 방식이라 복사 비용은 거의 없음
        m_pendingMetadata.clear();                                              // - 메타데이터 슬롯 비우기: 같은 건을 두 번 보내지 않도록 초기화
    }

    if (!frame.isNull()) {                                                      // - 프레임 확인: 보관된 프레임이 있을 때만 전달
        if (!m_firstFrameLogged) {                                              // - 첫 프레임 확인: 구동 후 처음 받은 프레임인 경우
            m_firstFrameLogged = true;                                          // - 기록 표시: 같은 로그를 반복하지 않도록 설정
            qCInfo(lcRtsp) << "camera" << m_cameraId << "first frame after" << m_openClock.elapsed() << "ms"; // - 정보 로그: 화면 전환 지연 감시용 오픈 시간 기록
        }
        emit frameReady(frame);                                                 // - 신호 발생: 메인 스레드에서 새 프레임 전달
    }

    if (!metadata.isEmpty())                                                    // - 메타데이터 확인: 보관된 데이터가 있을 때만 전달
        emit onvifMetadataReceived(metadata);                                   // - 신호 발생: 메인 스레드에서 메타데이터 전달
}

void RtspVideoSource::busLoop()
{
    while (!m_stopRequested) {                                                  // - 루프 순회: 정지 요청 전까지 메시지 감시 지속
        GstMessage *msg = gst_bus_timed_pop_filtered(                           // - 메시지 획득: 50ms 대기 후 에러/EOS/상태변화 메시지 팝 (stop() 대기 시간도 같이 줄어듦)
            m_bus, kBusPollMs * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
        if (!msg)                                                               // - 메시지 검증: 수신 메시지 없으면 다음 루프 진행
            continue;

        switch (GST_MESSAGE_TYPE(msg)) {                                       // - 메시지 유형 분류: 수신 메시지 종류 판별
        case GST_MESSAGE_ERROR: {                                               // - 파이프라인 에러 처리: 오류 메시지 수신 시 발생
            GError *err = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);                         // - 에러 파싱: 에러 구조체 데이터 추출
            qCWarning(lcRtsp) << "camera" << m_cameraId << "pipeline error:" << err->message; // - 경고 로그: 파이프라인 에러 내용 기록
            g_error_free(err);                                                  // - 에러 해제: 에러 구조체 메모리 해제
            g_free(debug);                                                      // - 디버그 문자열 해제: 디버그 메모리 해제
            QMetaObject::invokeMethod(this, &RtspVideoSource::scheduleReconnect, Qt::QueuedConnection); // - 비동기 재연결 요청: 메인 스레드로 재연결 호출 전달
            break;
        }
        case GST_MESSAGE_EOS:                                                   // - 스트림 종료 처리: 재생 완료/종료 메시지 수신 시 발생
            QMetaObject::invokeMethod(this, &RtspVideoSource::scheduleReconnect, Qt::QueuedConnection); // - 비동기 재연결 요청: 메인 스레드로 재연결 호출 전달
            break;

        case GST_MESSAGE_STATE_CHANGED:                                         // - 상태 변경 처리: 파이프라인 상태 변화 메시지 수신 시 발생
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {               // - 소스 검증: 파이프라인 본체 메시지 여부 확인
                GstState newState;
                gst_message_parse_state_changed(msg, nullptr, &newState, nullptr); // - 상태 파싱: 새 파이프라인 상태값 추출
                RiskTypes::ConnectionState mapped = RiskTypes::ConnectionState::Connecting; // - 상태 매핑: 기본값 '연결 중' 지정
                if (newState == GST_STATE_PLAYING)                               // - 재생 상태 비교: PLAYING 상태 시 '연결됨'으로 매핑
                    mapped = RiskTypes::ConnectionState::Connected;
                else if (newState == GST_STATE_NULL)                             // - 정지 상태 비교: NULL 상태 시 '연결 끊김'으로 매핑
                    mapped = RiskTypes::ConnectionState::Disconnected;
                QMetaObject::invokeMethod(this, [this, mapped]() {               // - 비동기 상태 전달: 메인 스레드로 연결 상태 반영 요청
                    if (mapped == RiskTypes::ConnectionState::Connected)        // - 접속 성공 확인: 재생이 시작된 경우
                        m_reconnectDelayMs = 0;                                 // - 대기 시간 초기화: 다음 실패는 다시 1초부터 시작
                    setConnectionState(mapped);                                 // - 상태 갱신: 파이프라인 상태를 화면용 연결 상태로 반영
                }, Qt::QueuedConnection);
            }
            break;
        default:
            break;
        }
        gst_message_unref(msg);                                                 // - 메시지 해제: 처리 완료된 메시지 메모리 반환
    }
}
