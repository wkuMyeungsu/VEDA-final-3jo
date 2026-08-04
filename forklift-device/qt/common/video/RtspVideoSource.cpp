#include "RtspVideoSource.h"
#include <QLoggingCategory>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcRtsp, "safety.video.rtsp")

// appsink("sink")에 새 프레임 도착 시 GStreamer가 호출 (GStreamer 스레드에서 실행)
// invokeMethod(QueuedConnection)로 Qt 메인 스레드로 넘겨서 emit
GstFlowReturn onNewSample(GstElement *appsink, gpointer userData)
{
    auto *self = static_cast<RtspVideoSource *>(userData);

    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
    if (!sample)
        return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    GstStructure *capsStruct = gst_caps_get_structure(caps, 0);
    int width = 0, height = 0;
    gst_structure_get_int(capsStruct, "width", &width);
    gst_structure_get_int(capsStruct, "height", &height);

    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        // width*3이 4의 배수가 아닌 해상도라면 GStreamer가 행마다 padding을
        // 넣을 수 있음 (여기선 800x600이라 2400이 이미 4의 배수라 안전).
        QImage frame(map.data, width, height, QImage::Format_RGB888);
        QImage copy = frame.copy();
        gst_buffer_unmap(buffer, &map);

        QMetaObject::invokeMethod(self, [self, copy]() {
            emit self->frameReady(copy);
        }, Qt::QueuedConnection);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

// appsink("metasink")에 새 ONVIF 메타데이터 도착 시 호출
// XML 파싱은 안 하고 raw 바이트 그대로 onvifMetadataReceived로 전달
// (파싱은 OnvifBBoxParser 책임 -- 여긴 GStreamer 데이터 수신까지만)
GstFlowReturn onNewMetadataSample(GstElement *appsink, gpointer userData)
{
    auto *self = static_cast<RtspVideoSource *>(userData);

    GstSample *sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink));
    if (!sample)
        return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        const QByteArray xml(reinterpret_cast<const char *>(map.data), static_cast<int>(map.size));
        gst_buffer_unmap(buffer, &map);

        qCDebug(lcRtsp) << "onvif metadata bytes:" << xml.size();

        QMetaObject::invokeMethod(self, [self, xml]() {
            emit self->onvifMetadataReceived(xml);
        }, Qt::QueuedConnection);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

}

RtspVideoSource::RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent)
    : IVideoSource(parent)
    , m_cameraId(std::move(cameraId))
    , m_rtspUrl(std::move(rtspUrl))
{
}

RtspVideoSource::~RtspVideoSource()
{
    stop();
}

void RtspVideoSource::start()
{
    if (m_pipeline)
        return; // 이미 실행 중

    // 카메라(192.168.0.3, profile2)로 gst-launch-1.0 -v 실측 검증된 구성:
    // 이 URL 하나에 영상(H264) + ONVIF 메타데이터 트랙이 같이 들어있고,
    // 캡스 필터(media=video,encoding-name=H264)로 영상 트랙만 골라 씀.
    const QByteArray url = m_rtspUrl.toString().toUtf8();
    // location 값을 따옴표로 감싼다 -- QUrl::toString()이 %21을 다시 '!'로 풀어버릴 수 있는데,
    // 따옴표 없이 넣으면 gst_parse_launch가 그 '!'를 파이프라인 구분자로 오해해서
    // location 값이 중간에 잘린다 (Resource not found로 이어짐).
    // 같은 RTSP 세션의 ONVIF 메타데이터 트랙(사람 bbox)도 두 번째 브랜치로 받는다.
    // 카메라가 이 트랙을 안 주면 이 브랜치만 조용히 안 붙고 영상은 그대로 동작.
    const QByteArray description = "rtspsrc name=src protocols=tcp latency=100 location=\"" + url + "\""
        + " src. ! application/x-rtp,media=video,encoding-name=H264 !"
          " rtph264depay ! h264parse ! avdec_h264 ! videoconvert !"
          " video/x-raw,format=RGB ! appsink name=sink emit-signals=true sync=false"
          " src. ! application/x-rtp,media=application,encoding-name=VND.ONVIF.METADATA !"
          " rtponvifmetadatadepay ! appsink name=metasink emit-signals=true sync=false";

    GError *error = nullptr;
    m_pipeline = gst_parse_launch(description.constData(), &error);
    if (!m_pipeline || error) {
        qCWarning(lcRtsp) << "camera" << m_cameraId << "failed to build pipeline:"
                           << (error ? error->message : "unknown error");
        if (error)
            g_error_free(error);
        m_pipeline = nullptr;
        setConnectionState(RiskTypes::ConnectionState::Disconnected);
        return;
    }

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
    g_signal_connect(appsink, "new-sample", G_CALLBACK(onNewSample), this);
    gst_object_unref(appsink);

    // ONVIF 브랜치는 선택적 -- 카메라가 트랙을 안 주면 "metasink"가 없을 수 있어 null 체크
    if (GstElement *metaSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "metasink")) {
        g_signal_connect(metaSink, "new-sample", G_CALLBACK(onNewMetadataSample), this);
        gst_object_unref(metaSink);
    }

    m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));

    m_stopRequested = false;
    m_busThread = std::thread(&RtspVideoSource::busLoop, this);

    setConnectionState(RiskTypes::ConnectionState::Connecting);
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);

    // rtspsrc가 카메라 쪽 동시 접속 경합 등으로 아예 응답을 안 주면
    // ERROR/EOS도 안 뜨고 Connecting에 무한정 멈출 수 있다. 일정 시간
    // 안에 Connected가 안 되면 직접 재시도한다.
    QTimer::singleShot(2000, this, [this]() {
        if (m_pipeline && connectionState() == RiskTypes::ConnectionState::Connecting) {
            qCWarning(lcRtsp) << "camera" << m_cameraId << "connect timed out, retrying";
            stop();
            start();
        }
    });
}

void RtspVideoSource::stop()
{
    m_stopRequested = true;
    if (m_busThread.joinable())
        m_busThread.join(); // busLoop()이 200ms 이내에 stop 요청을 보고 빠져나옴

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bus) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }

    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void RtspVideoSource::scheduleReconnect()
{
    setConnectionState(RiskTypes::ConnectionState::Disconnected);

    // 카메라 쪽 순간적인 연결 경합(동시 접속 시 가끔 1채널 실패하는 것) 등에서
    // 자동으로 복구하기 위해 3초 후 파이프라인을 다시 만들어본다.
    // this가 먼저 파괴되면 QTimer::singleShot이 알아서 콜백을 취소해준다.
    QTimer::singleShot(3000, this, [this]() {
        if (m_stopRequested)
            return;
        stop();
        start();
    });
}


void RtspVideoSource::busLoop()
{
    // GstBus는 "소식함" -- 파이프라인 상태변화/에러/EOS 메시지가 여기 쌓인다.
    // 200ms 타임아웃으로 계속 확인하면서, 그 사이사이 m_stopRequested를 체크해
    // stop()이 스레드를 오래 기다리지 않고 끝낼 수 있게 한다.
    while (!m_stopRequested) {
        GstMessage *msg = gst_bus_timed_pop_filtered(
            m_bus, 200 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
        if (!msg)
            continue;

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            qCWarning(lcRtsp) << "camera" << m_cameraId << "pipeline error:" << err->message;
            g_error_free(err);
            g_free(debug);
            QMetaObject::invokeMethod(this, &RtspVideoSource::scheduleReconnect, Qt::QueuedConnection);
            break;
    }
        case GST_MESSAGE_EOS:
            QMetaObject::invokeMethod(this, &RtspVideoSource::scheduleReconnect, Qt::QueuedConnection);
            break;

        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
                GstState newState;
                gst_message_parse_state_changed(msg, nullptr, &newState, nullptr);
                RiskTypes::ConnectionState mapped = RiskTypes::ConnectionState::Connecting;
                if (newState == GST_STATE_PLAYING)
                    mapped = RiskTypes::ConnectionState::Connected;
                else if (newState == GST_STATE_NULL)
                    mapped = RiskTypes::ConnectionState::Disconnected;
                QMetaObject::invokeMethod(this, [this, mapped]() { setConnectionState(mapped); },
                                           Qt::QueuedConnection);
            }
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }
}