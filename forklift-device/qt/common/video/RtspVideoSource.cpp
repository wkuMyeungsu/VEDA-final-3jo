#include "RtspVideoSource.h"
#include <QLoggingCategory>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcRtsp, "safety.video.rtsp")                                // - 로깅 카테고리 정의: RTSP 통신 및 영상 수신 로그 분류용 이름 지정

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

        QMetaObject::invokeMethod(self, [self, copy]() {                        // - 비동기 신호 전달: Qt 메인 스레드로 프레임 신호 전달
            emit self->frameReady(copy);
        }, Qt::QueuedConnection);
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

        QMetaObject::invokeMethod(self, [self, xml]() {                         // - 비동기 신호 전달: Qt 메인 스레드로 메타데이터 신호 전달
            emit self->onvifMetadataReceived(xml);
        }, Qt::QueuedConnection);
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
}

RtspVideoSource::~RtspVideoSource()
{
    stop();                                                                     // - 소멸자: 실행 중인 파이프라인 정지 및 리소스 정리
}

void RtspVideoSource::start()
{
    if (m_pipeline)                                                             // - 중복 실행 방지: 이미 파이프라인이 생성되어 실행 중인 경우 생략
        return;

    const QByteArray url = m_rtspUrl.toString().toUtf8();                       // - URL 문자열 변환: UTF-8 바이너리로 변환
    const QByteArray description = "rtspsrc name=src protocols=tcp latency=100 location=\"" + url + "\""
        + " src. ! application/x-rtp,media=video,encoding-name=H264 !"
          " rtph264depay ! h264parse ! avdec_h264 ! videoconvert !"
          " video/x-raw,format=RGB ! appsink name=sink emit-signals=true sync=false"
          " src. ! application/x-rtp,media=application,encoding-name=VND.ONVIF.METADATA !"
          " rtponvifmetadatadepay ! appsink name=metasink emit-signals=true sync=false"; // - 파이프라인 문자열 생성: RTSP 영상 및 ONVIF 디코딩 파이프라인 구성

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

    QTimer::singleShot(2000, this, [this]() {                                   // - 연결 타임아웃 감시: 2초 내 연결 미완료 시 재시도 진행
        if (m_pipeline && connectionState() == RiskTypes::ConnectionState::Connecting) {
            qCWarning(lcRtsp) << "camera" << m_cameraId << "connect timed out, retrying";
            stop();                                                             // - 소스 정지: 파이프라인 중단 처리
            start();                                                            // - 재시도: 파이프라인 재시작
        }
    });
}

void RtspVideoSource::stop()
{
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

    setConnectionState(RiskTypes::ConnectionState::Disconnected);               // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
}

void RtspVideoSource::scheduleReconnect()
{
    setConnectionState(RiskTypes::ConnectionState::Disconnected);               // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경

    QTimer::singleShot(3000, this, [this]() {                                   // - 재연결 예약: 3초 후 파이프라인 재구동 시도
        if (m_stopRequested)                                                    // - 수동 정지 확인: 정지 요청 상태인 경우 재연결 생략
            return;
        stop();                                                                 // - 기존 리소스 정리: 이전 파이프라인 정지 처리
        start();                                                                // - 파이프라인 재구동: 재생 재시도
    });
}

void RtspVideoSource::busLoop()
{
    while (!m_stopRequested) {                                                  // - 루프 순회: 정지 요청 전까지 메시지 감시 지속
        GstMessage *msg = gst_bus_timed_pop_filtered(                           // - 메시지 획득: 200ms 대기 후 에러/EOS/상태변화 메시지 팝
            m_bus, 200 * GST_MSECOND,
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
                QMetaObject::invokeMethod(this, [this, mapped]() { setConnectionState(mapped); }, // - 비동기 상태 전달: 메인 스레드로 연결 상태 반영 요청
                                           Qt::QueuedConnection);
            }
            break;
        default:
            break;
        }
        gst_message_unref(msg);                                                 // - 메시지 해제: 처리 완료된 메시지 메모리 반환
    }
}
