#include "RtspVideoSource.h"
#include <QLoggingCategory>
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcRtsp, "safety.video.rtsp")

// appsink("sink")에 새 프레임 도착 시 GStreamer가 호출 (GStreamer 스트리밍 스레드에서 실행)
// 익명 네임스페이스의 자유 함수라 RtspVideoSource의 private 멤버에 직접 접근할 수 없어서,
// 복사한 프레임을 public 진입점인 pushFrame()에 그대로 넘긴다. 큐잉/배압 처리는 그 안에서 담당.
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

        self->pushFrame(copy);
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
    const QByteArray description = "rtspsrc name=src protocols=tcp latency=100 location=\"" + url + "\""
        + " src. ! application/x-rtp,media=video,encoding-name=H264 !"
          " rtph264depay ! h264parse ! avdec_h264 ! videoconvert !"
          " video/x-raw,format=RGB ! appsink name=sink emit-signals=true sync=false max-buffers=2 drop=true";

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

    // 대기 중이던 프레임을 비워 메모리를 즉시 반환한다.
    // m_deliveryQueued는 여기서 리셋하지 않는다 -- "플래그 true = 이벤트가 정확히 1건 큐에
    // 있다"는 불변식을 유지하기 위해서다. 리셋해버리면 stop() 이후에도 큐에 남아있던
    // deliverPending() 이벤트가 도착했을 때 플래그가 이미 false라, 그 뒤에 재시작된
    // 스트림이 올린 진짜 예약과 뒤섞여 이벤트가 중복 등록될 수 있다.
    int dropped = 0;
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingFrame = QImage();
        dropped = m_droppedFrames.exchange(0);
    }
    if (dropped > 0)
        qCInfo(lcRtsp) << "camera" << m_cameraId << "dropped" << dropped
                        << "frame(s) while consumer lagged behind";

    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

// GStreamer 스트리밍 스레드에서 호출됨. 슬롯에 최신 프레임을 덮어쓰고 메인 스레드로
// 배달을 예약한다 -- 이미 슬롯에 프레임이 있었다면 그건 소비되지 못하고 버려진 것.
void RtspVideoSource::pushFrame(const QImage &frame)
{
    {
        QMutexLocker locker(&m_pendingMutex);
        if (!m_pendingFrame.isNull())
            m_droppedFrames.fetch_add(1);
        m_pendingFrame = frame;
    }
    scheduleDelivery();
}

// 메인 스레드로의 배달을 최대 1건만 큐에 올린다. exchange(true)가 이미 true를
// 반환했다면(=이미 예약된 이벤트가 있다면) 곧 그 이벤트가 슬롯의 최신 프레임을
// 가져갈 것이므로 여기서는 그냥 리턴 -- Qt 이벤트 큐에 중복으로 쌓이는 것을 막는다.
void RtspVideoSource::scheduleDelivery()
{
    if (m_deliveryQueued.exchange(true))
        return;

    QMetaObject::invokeMethod(this, [this]() { deliverPending(); }, Qt::QueuedConnection);
}

// 메인 스레드에서 실행. 슬롯에서 최신 프레임을 꺼내 frameReady로 emit한다.
void RtspVideoSource::deliverPending()
{
    // 플래그를 먼저 내린 다음 슬롯을 비운다. 순서를 반대로 하면 "슬롯 비우기"와
    // "플래그 내리기" 사이에 들어온 push가 scheduleDelivery()에서 예약을 건너뛰게
    // 되는데(플래그가 아직 true라서), 정작 배달은 이미 끝난 뒤라 그 프레임은 다음
    // push가 올 때까지 슬롯에 갇힌다. 스트림이 그 직후 끊기면 그 프레임이 메모리에
    // 영구히 남는다. 먼저 내리면 최악의 경우 이벤트가 2건 큐에 들어갈 수 있지만
    // (여전히 상한 내) 영구 정체는 구조적으로 불가능하다.
    m_deliveryQueued.store(false);

    QImage frame;
    {
        QMutexLocker locker(&m_pendingMutex);
        if (m_pendingFrame.isNull())
            return; // stop() 등으로 슬롯이 비었으면 emit할 것이 없음
        frame = std::move(m_pendingFrame);
        m_pendingFrame = QImage();
    }

    emit frameReady(frame);
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