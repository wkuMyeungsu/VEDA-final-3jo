// main.cpp
//
// 입력값: 카메라 RTSP URL
// 처리과정:
//   1) GStreamer로 RTSP 세션을 열고 media=application 트랙만 골라 appsink로 흘려보냄
//   2) appsink 콜백에서 패킷 하나(raw RTP)를 받을 때마다 RTP 헤더 파싱 -> payload 추출
//   3) OnvifMetadataReassembler에 순서대로 넣어서 marker bit 뜰 때까지 이어붙임
//      (시퀀스 번호 연속성도 같이 검사해서, 패킷이 중간에 빠지면 깨진 프레임을 버림)
//   4) 완성된 XML을 종류별로 분기해 객체탐지 또는 ArUco 파서로 파싱
//   5) 콘솔에 출력 + 종류별 CSV 파일에 누적 저장 (재연결돼도 계속 이어붙여짐)
//   6) Ctrl+C(SIGINT) 수신 시 파이프라인에 EOS를 보내서 CSV가 안전하게 마무리되도록 함.
//      시그널 핸들러는 플래그만 세팅하고, 실제 EOS 전송은 메인 스레드(메인 루프)에서
//      처리함 — 핸들러 안에서 GStreamer API를 직접 부르면 내부 락과 얽혀 데드락 날 수
//      있어서 분리함.
//   7) [재연결] PLAYING 도달 전에 파이프라인이 ERROR로 끊기면(예: RTSP 파싱 에러,
//      DESCRIBE 타임아웃 등 — 실측상 재연결 타이밍에 카메라 쪽 리소스가 덜 풀린
//      상태에서 자주 발생) 파이프라인을 통째로 버리고 새로 만들어서 재시도함.
//      Ctrl+C(EOS)나 정상 EOS로 끝난 경우는 재시도하지 않음.
// 참고: 이 환경에서는 RTSP 연결이 giolibproxy.dll 로드 실패로 인해 느려질 수 있음
//       (매 요청 2~5초) — 실행 전 다음 환경변수를 설정하면 크게 빨라지는 것으로 확인됨:
//       $env:GIO_USE_PROXY_RESOLVER = "dummy"  (또는 setx로 영구 등록)
// 출력값: 콘솔 실시간 출력 + detections_YYYYMMDD_HHMMSS.csv
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <iostream>
#include <cstring>
#include <ctime>
#include <csignal>
#include <chrono>
#include <atomic>
#include <thread>
#include <limits>
#include <optional>

#include "input/rtp_metadata_receiver.hpp"
#include "input/onvif_metadata_parser.hpp"
#include "logging/csv_logger.hpp"
#include "input/metadata_router.hpp"
#include "common/metadata_timing.hpp"
#include "input/aruco_metadata_parser.hpp"
#include "logging/aruco_csv_logger.hpp"
#include "config/terminal_config.hpp"
#include "logic/judgment/judgment_pipeline.h"
#include "logic/tracking/marker_channel_tracker.hpp"
#include "network/result_publisher.hpp"
#include "network/result_dispatcher.hpp"
#include "network/camera_assignment_server.hpp"
#include "logging/event_logger.hpp"
#include "logging/latency_logger.hpp"

namespace {
// 활성 카메라와 단말 ID는 더 이상 상수가 아니다:
//   - terminal_id / marker_id / 핸드오버 파라미터 -> 설정 파일(server/config/terminal_*.json)
//   - active_camera_id -> MarkerChannelTracker가 실시간으로 결정 (지게차 마커가 보이는 채널)
// 설정 파일 경로를 인자로 안 주면 이 단말 ID로 기본 파일을 찾는다.
constexpr const char* kDefaultTerminalId = "TERM_01";

// 마커가 아직 어느 카메라에서도 확정되지 않은 상태의 활성 카메라 값.
// JudgmentPipeline 규약상 음수는 "미확정"이라 하류 JSON에 camera_id=null로 나간다.
// 예전처럼 1로 시작하면, 실제로는 지게차 위치를 모르는 구간에 cam 1이 확정된 것처럼
// 보고하게 된다.
constexpr int kUnknownCameraId = -1;

// 판정 결과(risk_event) 채널은 TCP 9000에서 MQTT로 옮겨갔다(2026-08-11) - 서버가 직접
// 포트를 열지 않으므로 포트 상수도 없다. 브로커 주소는 ResultPublisher 생성자 기본값 참고.
constexpr uint16_t kCameraAssignmentServerPort = 9001;
}  // namespace

#ifdef _WIN32
#include <windows.h>

// Windows 콘솔에 한글(UTF-8)이 깨지지 않게 출력하기 위한 streambuf.
// SetConsoleOutputCP(CP_UTF8)만으로는, std::cout이 문자열을 여러 조각으로 나눠
// WriteConsoleA를 호출할 때 한글 한 글자(UTF-8 3바이트)가 조각 경계에서 잘려
// 깨지는 문제가 있음(레거시 콘솔 호스트의 고질적 버그). 대신 문자열을 버퍼에
// 모아뒀다가 flush 시점에 한 번에 UTF-16으로 변환해서 WriteConsoleW로 직접 쓰면,
// 콘솔 코드페이지 설정과 무관하게 항상 정확히 출력됨.
class Utf8ConsoleStreambuf : public std::streambuf {
public:
    explicit Utf8ConsoleStreambuf(HANDLE consoleHandle) : handle_(consoleHandle) {}

protected:
    int overflow(int c) override {
        if (c != EOF) buffer_.push_back(static_cast<char>(c));
        flushBuffer();  // 원래 std::cout처럼 즉시 화면에 반영되도록 매번 flush
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        buffer_.append(s, static_cast<size_t>(n));
        flushBuffer();  // 여기서도 즉시 flush (이걸 안 해서 출력이 안 보이던 버그가 있었음)
        return n;
    }

    int sync() override {
        flushBuffer();
        return 0;
    }

private:
    void flushBuffer() {
        if (buffer_.empty()) return;
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, buffer_.data(),
                                           static_cast<int>(buffer_.size()), nullptr, 0);
        if (wideLen > 0) {
            std::wstring wide(static_cast<size_t>(wideLen), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, buffer_.data(),
                                 static_cast<int>(buffer_.size()), &wide[0], wideLen);
            DWORD written = 0;
            WriteConsoleW(handle_, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr);
        }
        buffer_.clear();
    }

    HANDLE handle_;
    std::string buffer_;
};
#endif

// appsink 콜백에서 공유할 상태 (재조립기 + 로거 + 판정 인프라)
struct AppState {
    OnvifMetadataReassembler reassembler;
    CsvLogger* objectLogger = nullptr;
    ArucoCsvLogger* arucoLogger = nullptr;

    // ArUco/Object 두 토픽이 비동기로 도착하므로, ObjectDetection 판정 시점에
    // 참조할 수 있게 가장 최근 ArUco 프레임을 캐시해 둔다.
    std::optional<ArucoFrame> lastAruco;

    // 판정 인프라. 선언 순서가 곧 생성 순서이므로, 서로를 참조하는 멤버(judgmentPipeline
    // -> sensorReader, resultDispatcher -> resultPublisher, markerTracker/judgmentPipeline
    // -> config)는 참조 대상을 먼저 선언한다.
    forklift::config::TerminalConfig config;

    // 지게차 마커가 어느 카메라 채널에 보이는지 추적. 액티브 채널이 바뀌는 순간에만
    // 신호를 주므로, 그 신호를 받아 9001 camera_assignment를 보낸다.
    MarkerChannelTracker markerTracker;

    StubSensorReader sensorReader;
    JudgmentPipeline judgmentPipeline;

    risk_transport::ResultPublisher resultPublisher;
    risk_log::EventLogger eventLogger;
    risk_log::LatencyLogger latencyLogger;
    risk_transport::ResultDispatcher resultDispatcher;

    // 9001 카메라 할당 채널. 전환 시점 판단은 markerTracker가 하고, 이 클래스는
    // "확정된 단말에게 실제로 보내는" 역할만 한다.
    risk_transport::CameraAssignmentServer cameraAssignmentServer;

    explicit AppState(forklift::config::TerminalConfig cfg)
        : config(std::move(cfg)),
          markerTracker(config.forklift.marker_id,
                        config.handover.confirm_frames,
                        config.handover.lostGrace()),
          judgmentPipeline(kUnknownCameraId, config.forklift.terminal_id, sensorReader),
          // 브로커 host/port는 생성자 기본값(localhost:1883)을 그대로 쓴다 - 브로커가
          // 서버와 같은 머신에 있는 현재 배치 기준. 다른 머신으로 옮기면 여기서 넘긴다.
          resultPublisher(config.forklift.terminal_id),
          resultDispatcher(
              [this](const std::string& json) { resultPublisher.publish(json); },
              std::chrono::milliseconds(200)),
          cameraAssignmentServer("0.0.0.0", kCameraAssignmentServerPort) {}
};

// 액티브 카메라가 실제로 바뀌었을 때만 불린다(MarkerChannelTracker가 그때만 신호를 준다).
// 하는 일 두 가지:
//   1) 판정 파이프라인의 담당 카메라 갱신 -> 하류 판정 JSON의 camera_id가 따라간다
//   2) 단말에 camera_assignment 전송 -> 운전석 화면이 새 카메라로 전환된다
//
// 단말이 아직 9001에 붙지 않았으면 2)는 false를 돌려주는데, 이건 오류가 아니라 흔한
// 상태(서버가 먼저 뜬 경우)라 경고만 남기고 계속 간다. 단말은 접속 직후 hello를 보내고,
// 그 다음 전환 때 정상적으로 받게 된다.
static void applyActiveCameraChange(AppState& state, int camera_id) {
    state.judgmentPipeline.setActiveCameraId(camera_id);

    // camera_id 문자열 표기는 판정 JSON과 같은 규칙(cameraIdToString)을 쓴다.
    // 두 채널이 같은 카메라를 다르게 부르면 단말에서 대조가 안 된다.
    const std::string camera_id_str = cameraIdToString(camera_id);

    // [TODO] zone 매핑 미확정(김진석) — 확정 전까지 빈 문자열.
    //        판정 JSON 쪽 규약(빈 값 -> null)과 맞춘다.
    const bool sent = state.cameraAssignmentServer.sendCameraAssignment(
        state.config.forklift.terminal_id, camera_id_str, /*zone=*/"", nowIso8601Ms());

    std::cout << "[핸드오버] 액티브 카메라 전환 -> channel=" << camera_id_str
              << " (marker_id=" << state.config.forklift.marker_id
              << ", terminal_id=" << state.config.forklift.terminal_id
              << ", 단말 전송=" << (sent ? "성공" : "실패(단말 미접속)") << ")\n";
}

// Ctrl+C 핸들러에서만 접근하는 전역 포인터.
// 시그널 핸들러 안에서는 이것 말고 다른 걸 건드리면 위험하므로 최소한으로만 사용.
static GstElement* g_pipeline = nullptr;

// EOS 요청 여부와 그 시각. 메인 루프에서 "EOS 보낸 지 얼마나 지났는지" 판단에 사용.
// std::atomic<bool>은 시그널 핸들러에서 안전하게 쓸 수 있음.
static std::atomic<bool> g_eosRequested{false};
static std::chrono::steady_clock::time_point g_eosRequestedAt;

// 시그널 핸들러는 "Ctrl+C가 눌렸다"는 플래그만 세팅함.
// 절대 여기서 gst_element_send_event() 같은 GStreamer API를 직접 부르면 안 됨:
// 시그널이 도착한 시점에 메인 스레드가 GStreamer 내부 락을 들고 있는 중이었다면,
// 핸들러 쪽에서 같은 락을 기다리다가 영원히 멈추는 데드락이 날 수 있음.
// 실제 EOS 전송은 메인 루프에서, 메인 스레드가 안전한 타이밍에 처리함.
static std::atomic<bool> g_sigintReceived{false};

void handleSigint(int) {
    g_sigintReceived = true;
}

// 파싱 결과를 사람이 읽기 좋게 출력
static void printFrame(const MetadataFrame& frame) {
    std::cout << "\n=== Frame UtcTime: " << frame.utcTime << " ===\n";
    std::cout << "Detected objects: " << frame.objects.size() << "\n";
    for (const auto& obj : frame.objects) {
        std::cout << "  ObjectId=" << obj.objectId;
        if (obj.parentId != -1) std::cout << " (Parent=" << obj.parentId << ")";
        std::cout << " Class=" << obj.classInfo.type
                  << " (" << obj.classInfo.likelihood << ")"
                  << " BBox=(" << obj.bbox.left << "," << obj.bbox.top
                  << ")-(" << obj.bbox.right << "," << obj.bbox.bottom << ")\n";
    }
}

static void printArucoFrame(const ArucoFrame& frame) {
    std::cout << "\n=== ArUco UtcTime: " << frame.utcTime
              << " Channel: " << frame.channel << " ===\n";
    std::cout << "Detected markers: " << frame.markers.size() << "\n";
    for (const auto& marker : frame.markers) {
        std::cout << "  MarkerId=" << marker.id << " Corners=";
        for (int i = 0; i < 4; ++i) {
            if (i > 0) std::cout << ",";
            std::cout << "(" << marker.corners[i].x
                      << "," << marker.corners[i].y << ")";
        }
        std::cout << "\n";
    }
}

// appsink에 새 샘플(=RTP 패킷 하나)이 도착할 때마다 GStreamer가 호출하는 콜백
static GstFlowReturn onNewSample(GstAppSink* sink, gpointer userData) {
    AppState* state = static_cast<AppState*>(userData);

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        RtpHeaderInfo header;
        if (parseRtpHeader(map.data, map.size, header)) {
            const uint8_t* payload = map.data + header.headerLength;
            size_t payloadSize = map.size - header.headerLength;

            auto completedXml = state->reassembler.feed(
                payload, payloadSize, header.sequenceNumber, header.marker);
            if (completedXml) {
                const auto serverReceived = std::chrono::system_clock::now();
                switch (classifyMetadata(*completedXml)) {
                case MetadataType::ObjectDetection: {
                    MetadataFrame frame = parseOnvifMetadata(*completedXml);
                    const auto timing = forklift::common::makeMetadataTiming(frame.utcTime, serverReceived);
                    frame.serverReceivedUtc = timing.server_received_utc;
                    frame.deltaMs = timing.delta_ms;
                    std::cout << "[수신] type=object camera_utc=" << frame.utcTime
                              << " server_received_utc=" << frame.serverReceivedUtc
                              << " delta_ms=" << frame.deltaMs << "\n";
                    printFrame(frame);
                    if (state->objectLogger) state->objectLogger->logFrame(frame);

                    // ── 위험 판정 트리거 ─────────────────────────────────
                    // 호모그래피(3단계)가 아직 없으므로 WorldPoint 좌표는 상수 더미로
                    // 고정한다. 픽셀 좌표를 미터인 것처럼 넣으면 거짓 정밀도로 위험
                    // 판정을 그르치므로 절대 쓰지 않는다. 불리언 신호(person_detected/
                    // forklift_localized)만 실제 검출값을 반영한다.
                    {
                        bool person_detected = false;
                        for (const auto& obj : frame.objects) {
                            if (obj.classInfo.type == "Human") {
                                person_detected = true;
                                break;
                            }
                        }
                        const bool forklift_localized =
                            state->lastAruco.has_value() && !state->lastAruco->markers.empty();

                        // [더미] 호모그래피 도입 전까지 안전거리(5m)로 고정.
                        const WorldPoint forkliftPoint{0.0, 0.0};
                        const WorldPoint personPoint{5.0, 0.0};

                        NearestPersonResult nearest;
                        nearest.found = person_detected;
                        // 사람이 어느 카메라에서 보였는지는 아직 상류에서 안 올라온다
                        // (호모그래피/Re-ID 미연동). 지금 담당 중인 카메라를 그대로 쓴다 -
                        // 이렇게 두면 isCameraIdMismatch()가 항상 false라 핸드오버 의심
                        // 플래그가 오탐하지 않는다. 마커가 아직 안 잡혀 담당 카메라가
                        // 미확정(-1)이면 그 값이 그대로 내려가 하류 JSON에서 null이 된다.
                        nearest.camera_id =
                            person_detected ? state->judgmentPipeline.activeCameraId() : -1;
                        nearest.position = person_detected ? personPoint : WorldPoint{};
                        nearest.distance_m = person_detected
                                                  ? 5.0
                                                  : std::numeric_limits<double>::max();

                        const PipelineOutput judgmentOut = state->judgmentPipeline.processFrame(
                            forkliftPoint, forklift_localized, nearest);
                        state->resultDispatcher.submit(judgmentOut.result);

                        std::cout << "[판정] final_risk=" << toString(judgmentOut.result.final_risk)
                                  << " exception_state=" << toString(judgmentOut.result.exception)
                                  << "\n";
                    }
                    break;
                }
                case MetadataType::ArucoDetection: {
                    const auto frame = parseArucoMetadata(*completedXml);
                    if (!frame) {
                        std::cerr << "[경고] ArUco XML 필드 검증 실패, 프레임 건너뜀\n";
                        break;
                    }
                    auto timedFrame = *frame;
                    const auto timing = forklift::common::makeMetadataTiming(timedFrame.utcTime, serverReceived);
                    timedFrame.serverReceivedUtc = timing.server_received_utc;
                    timedFrame.deltaMs = timing.delta_ms;
                    std::cout << "[수신] type=aruco camera_utc=" << timedFrame.utcTime
                              << " server_received_utc=" << timedFrame.serverReceivedUtc
                              << " delta_ms=" << timedFrame.deltaMs << "\n";
                    printArucoFrame(timedFrame);
                    if (state->arucoLogger) state->arucoLogger->logFrame(timedFrame);
                    state->lastAruco = timedFrame;

                    // 이 프레임으로 "지게차가 지금 어느 카메라에 보이는지"를 갱신한다.
                    // 반환값이 있을 때 = 액티브 채널이 실제로 바뀐 순간뿐이다
                    // (안 바뀌면 nullopt라 여기서 아무 일도 일어나지 않는다).
                    if (auto newChannel = state->markerTracker.onArucoFrame(timedFrame)) {
                        applyActiveCameraChange(*state, *newChannel);
                    }
                    break;
                }
                case MetadataType::Unknown:
                    std::cerr << "[경고] 지원하지 않거나 손상된 ONVIF 메타데이터, 프레임 건너뜀\n";
                    break;
                }
            }
        } else {
            std::cerr << "[경고] RTP 헤더 파싱 실패, 패킷 건너뜀 (size=" << map.size << ")\n";
        }
        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

// 파이프라인을 한 번 만들어서 실행하고, 끝난 이유를 돌려줌.
// ClosedCleanly: EOS(정상) 또는 Ctrl+C로 끝남 -> 더 이상 재시도 안 해도 됨
// NeedsRetry:    ERROR로 끊김(Ctrl+C가 원인이 아님) -> 재연결 시도해볼 만함
enum class RunResult { ClosedCleanly, NeedsRetry };

static RunResult runPipelineOnce(const std::string& rtspUrl, AppState& state) {
    // 메타데이터 트랙(media=application)만 받음. queue는 appsink의 콘솔출력/CSV
    // flush(동기 I/O)가 잠깐 지연돼도 rtspsrc 내부 스레드가 막히지 않도록 분리하는 역할.
    // max-buffers를 1 -> 5로 늘려서, 콘솔 출력/CSV 쓰기가 잠깐 지연돼도 메타데이터
    // 조각이 바로바로 드롭되지 않도록 여유를 줌
    std::string pipelineDesc =
        "rtspsrc location=\"" + rtspUrl + "\" "
        "latency=100 protocols=tcp name=src "
        "src. ! application/x-rtp,media=application ! queue ! "
        "appsink name=metasink emit-signals=true sync=false max-buffers=5 drop=true";

    GError* error = nullptr;
    GstElement* pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
    if (error) {
        std::cerr << "[오류] 파이프라인 생성 실패: " << error->message << "\n";
        g_error_free(error);
        return RunResult::NeedsRetry;
    }
    if (!pipeline) {
        std::cerr << "[오류] 파이프라인 생성 실패 (원인 불명)\n";
        return RunResult::NeedsRetry;
    }

    GstElement* appsink = gst_bin_get_by_name(GST_BIN(pipeline), "metasink");
    if (!appsink) {
        std::cerr << "[오류] metasink 엘리먼트를 찾을 수 없음. 파이프라인 문자열 확인 필요.\n";
        gst_object_unref(pipeline);
        return RunResult::NeedsRetry;
    }
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, &state, nullptr);
    gst_object_unref(appsink);

    g_pipeline = pipeline;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    // 메인 루프: 버스 메시지(에러/EOS/상태변화) 대기 + Ctrl+C 플래그 처리.
    // 실제 gst_element_send_event() 호출은 여기, 메인 스레드에서만 함 (시그널 핸들러 X).
    GstBus* bus = gst_element_get_bus(pipeline);
    const GstClockTime pollTimeout = 200 * GST_MSECOND;
    const auto forceKillAfter = std::chrono::seconds(30);
    // [연결 타임아웃] Ctrl+C 여부와 무관하게, PLAYING에 아예 못 들어간 채로
    // 너무 오래(예: giolibproxy 문제로 RTSP 요청이 계속 안 끝나는 경우) 있으면
    // 재시도 대상으로 처리함. 예전엔 Ctrl+C를 눌러야만 타임아웃이 작동해서,
    // 아무 입력 없이 진짜 무한정 멈추는 케이스를 못 잡는 구멍이 있었음.
    const auto connectTimeout = std::chrono::seconds(45);
    auto pipelineStartedAt = std::chrono::steady_clock::now();
    bool reachedPlaying = false;
    RunResult result = RunResult::NeedsRetry;

    while (true) {
        // Ctrl+C가 눌렸는데 아직 EOS를 안 보냈다면, 지금(메인 스레드, 안전한 시점) 보냄.
        if (g_sigintReceived && !g_eosRequested) {
            std::cout << "\n[종료 요청] EOS 전송 중... 파일을 안전하게 마무리합니다. 잠시만 기다려주세요.\n";
            gst_element_send_event(pipeline, gst_event_new_eos());
            g_eosRequested = true;
            g_eosRequestedAt = std::chrono::steady_clock::now();
        }

        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus, pollTimeout,
            static_cast<GstMessageType>(
                GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));

        if (msg) {
            GstMessageType type = GST_MESSAGE_TYPE(msg);

            if (type == GST_MESSAGE_STATE_CHANGED) {
                // 파이프라인 자신의 상태변화만 출력하고, rtspsrc/appsink/queue 등
                // 내부 엘리먼트의 상태변화는 무시하고 루프 계속 (여기서 잘못 break하면
                // 파이프라인이 시작도 하기 전에 조용히 종료돼버리는 버그가 있었음).
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline)) {
                    GstState oldState, newState, pending;
                    gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
                    std::cout << "[상태] " << gst_element_state_get_name(oldState)
                              << " -> " << gst_element_state_get_name(newState) << "\n";
                    if (newState == GST_STATE_PLAYING) {
                        reachedPlaying = true;
                    }
                }
                gst_message_unref(msg);
                continue;
            }

            if (type == GST_MESSAGE_ERROR) {
                GError* err;
                gchar* debug;
                gst_message_parse_error(msg, &err, &debug);
                std::cerr << "[파이프라인 오류] " << err->message << "\n";
                if (debug) {
                    std::cerr << "[상세 정보] " << debug << "\n";
                }
                g_error_free(err);
                g_free(debug);
                gst_message_unref(msg);
                // Ctrl+C가 원인이 아니라 순수 ERROR라면 재시도 대상
                result = g_sigintReceived ? RunResult::ClosedCleanly : RunResult::NeedsRetry;
                break;
            }

            // EOS (정상 종료든 Ctrl+C에 의한 EOS든)
            gst_message_unref(msg);
            result = RunResult::ClosedCleanly;
            break;
        }

        // 메시지가 안 왔어도, EOS를 요청한 지 오래됐으면 포기하고 강제 종료
        if (g_eosRequested) {
            auto elapsed = std::chrono::steady_clock::now() - g_eosRequestedAt;
            if (elapsed > forceKillAfter) {
                std::cerr << "[경고] EOS 응답 없음(" << forceKillAfter.count()
                          << "초 초과) — 파이프라인을 강제로 정리합니다.\n";
                result = RunResult::ClosedCleanly;
                break;
            }
        }

        // Ctrl+C 안 눌렀어도, PLAYING에 너무 오래 못 들어가면 재시도 대상으로 처리
        if (!reachedPlaying && !g_eosRequested && !g_sigintReceived) {
            auto elapsed = std::chrono::steady_clock::now() - pipelineStartedAt;
            if (elapsed > connectTimeout) {
                std::cerr << "[경고] " << connectTimeout.count()
                          << "초 동안 연결이 PLAYING 상태에 도달하지 못했습니다 — 재시도합니다.\n";
                result = RunResult::NeedsRetry;
                break;
            }
        }
    }

    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_pipeline = nullptr;
    return result;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // 한글/유니코드 콘솔 출력 깨짐 방지 (아래 streambuf와 함께 사용)
    // std::cout/std::cerr을 WriteConsoleW 기반 버퍼로 교체 — 이게 실제로 깨짐을 막는 핵심.
    // static이라 프로그램 종료까지 살아있음 (streambuf는 스트림보다 먼저 파괴되면 안 됨).
    static Utf8ConsoleStreambuf coutBuf(GetStdHandle(STD_OUTPUT_HANDLE));
    static Utf8ConsoleStreambuf cerrBuf(GetStdHandle(STD_ERROR_HANDLE));
    std::cout.rdbuf(&coutBuf);
    std::cerr.rdbuf(&cerrBuf);
#endif

    if (argc < 2) {
        std::cerr << "사용법: " << argv[0] << " <RTSP URL> [단말 설정 JSON 경로]\n";
        std::cerr << "예: " << argv[0]
                  << " \"rtsp://<user>:<password>@192.168.0.3:554/0/onvif/profile2/media.smp\"\n";
        std::cerr << "설정 경로를 생략하면 " << kDefaultTerminalId
                  << " 기본 파일(config/terminal_" << kDefaultTerminalId << ".json)을 찾습니다.\n";
        return 1;
    }

    gst_init(&argc, &argv);

    // ── 단말 설정 로드 ──────────────────────────────────────────
    // 실패하면 그냥 죽는다. 마커 ID/단말 ID 없이 기본값으로 계속 돌면 "지게차가 아무
    // 카메라에도 안 잡히는" 것처럼 조용히 동작해서 원인을 찾기 어렵다 - 설정 문제는
    // 시작할 때 크게 실패하는 편이 낫다.
    const std::string configPath =
        (argc >= 3) ? argv[2]
                    : forklift::config::resolveTerminalConfigPath(kDefaultTerminalId);
    forklift::config::TerminalConfig terminalConfig;
    try {
        terminalConfig = forklift::config::loadTerminalConfig(configPath);
    } catch (const forklift::config::TerminalConfigError& e) {
        std::cerr << "[오류] 단말 설정 로드 실패: " << e.what() << "\n"
                     "       설정 파일 예시는 server/config/terminal_TERM_01.json 참고.\n"
                     "       (실행 위치에 따라 config/ 상대경로가 달라지므로, 필요하면 "
                     "두 번째 인자로 경로를 직접 지정하세요.)\n";
        return 2;
    }

    std::cout << "단말 설정 로드 완료: " << configPath
              << " (terminal_id=" << terminalConfig.forklift.terminal_id
              << ", forklift_id=" << terminalConfig.forklift.forklift_id
              << ", marker_id=" << terminalConfig.forklift.marker_id
              << ", confirm_frames=" << terminalConfig.handover.confirm_frames
              << ", lost_grace_ms=" << terminalConfig.handover.lost_grace_ms << ")\n";

    AppState state(std::move(terminalConfig));

    // CSV 파일은 딱 한 번만 생성. 재연결이 몇 번 일어나든 같은 파일에 계속 이어붙여짐
    // (CsvLogger는 파일이 이미 있으면 헤더 없이 append하는 구조라 문제없음).
    std::time_t now = std::time(nullptr);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", std::localtime(&now));
    std::string objectCsvPath = std::string("detections_") + timeBuf + ".csv";
    std::string arucoCsvPath = std::string("aruco_markers_") + timeBuf + ".csv";
    CsvLogger objectLogger(objectCsvPath);
    ArucoCsvLogger arucoLogger(arucoCsvPath);
    state.objectLogger = &objectLogger;
    state.arucoLogger = &arucoLogger;
    std::cout << "객체탐지 CSV 저장 경로: " << objectCsvPath << "\n";
    std::cout << "ArUco CSV 저장 경로: " << arucoCsvPath << "\n";
    std::cout << "수신 시작 시각(로컬 PC 기준): " << timeBuf << "\n";
    std::cout << "※ 이 시각은 PC 로컬시간이고, 프레임별 UtcTime은 카메라 기준 시간이니 "
                 "나중에 정밀 동기화할 때 헷갈리지 않도록 주의.\n";

    // 판정 인프라 조립. 옛 danger_judgment_engine_main.cpp와 동일한 순서로 생성·연결한다:
    // ResultPublisher -> EventLogger/LatencyLogger -> ResultDispatcher -> 훅 연결 -> start.
    state.resultPublisher.onStateChange([](risk_transport::LinkState s) {
        const char* name = s == risk_transport::LinkState::CONNECTED     ? "CONNECTED"
                            : s == risk_transport::LinkState::CONNECTING ? "CONNECTING"
                                                                         : "DISCONNECTED";
        std::cerr << "[publisher] link state -> " << name << "\n";
    });
    state.resultPublisher.start();

    state.cameraAssignmentServer.start();

    state.eventLogger.start();
    state.latencyLogger.start();

    state.resultDispatcher.onStateChangeEvent(
        [&state](const JudgmentResult& r, int prev_risk) { state.eventLogger.log(r, prev_risk); });
    state.resultDispatcher.onLatencyEvent(
        [&state](const LatencyStamps& stamps) { state.latencyLogger.log(stamps); });

    // 첫 ObjectDetection 프레임이 오기 전에도 risk_event 하트비트가 돌게 한다.
    // submit()은 이 아래 appsink 콜백의 ObjectDetection 분기에서만 불리므로, 카메라가
    // 객체 메타데이터를 한 번도 안 올리면(ArUco만 오거나 사람이 안 잡히는 동안) 채널이
    // 통째로 조용해져서 단말이 "서버 다운"과 "안전 상태"를 구분할 수 없었다.
    // terminal_id는 기동 시점에 이미 확정된 값이라 idle 자리표시에도 채워 보낸다
    // (camera_id/zone/거리는 아직 근거가 없으므로 미확정 규약대로 null로 나간다).
    {
        JudgmentResult idle = risk_transport::ResultDispatcher::idleResult();
        idle.terminal_id = state.config.forklift.terminal_id;
        state.resultDispatcher.primeIdle(idle);
    }
    state.resultDispatcher.start();

    std::signal(SIGINT, handleSigint);

    const int maxRetries = 5;
    const auto retryDelay = std::chrono::seconds(10);

    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        if (attempt > 1) {
            std::cout << "\n[연결 시도 " << attempt << "/" << maxRetries << "] 재연결 중...\n";
        } else {
            std::cout << "RTSP 연결 시도 중... 이 카메라/환경에서는 연결에 시간이 걸릴 수 있습니다"
                         "(GIO_USE_PROXY_RESOLVER=dummy 설정 시 보통 수 초 이내). "
                         "아래 [상태] 로그가 계속 바뀌면 진행 중인 것이니 기다려주세요.\n"
                         "Ctrl+C로 종료(안전하게 파일 마무리 후 종료됨).\n";
        }

        RunResult result = runPipelineOnce(argv[1], state);

        if (result == RunResult::ClosedCleanly) {
            break;  // EOS든 Ctrl+C든 정상적으로 끝났으니 재시도 불필요
        }
        if (g_sigintReceived) {
            break;  // 에러로 끊겼어도 사용자가 이미 종료를 원했다면 재시도 안 함
        }
        if (attempt < maxRetries) {
            std::cerr << "[재연결] " << retryDelay.count() << "초 후 다시 시도합니다. ("
                      << attempt << "/" << maxRetries << " 실패, Ctrl+C로 언제든 중단 가능)\n";
            for (int i = 0; i < retryDelay.count() && !g_sigintReceived; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        } else {
            std::cerr << "[포기] 최대 재시도 횟수(" << maxRetries << "회)를 초과했습니다. "
                         "카메라 쪽 상태(웹뷰어 재접속 등)를 확인해보는 걸 권장합니다.\n";
        }
    }

    // 판정 인프라 정리. 옛 danger_judgment_engine_main.cpp와 동일한 순서:
    // dispatcher -> (flush 대기) -> publisher -> 로거(event/latency) 순.
    state.resultDispatcher.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    state.resultPublisher.stop();
    state.cameraAssignmentServer.stop();
    state.eventLogger.stop();
    state.latencyLogger.stop();

    std::cout << "종료됨. 파일 저장 완료(또는 위 경고 참고).\n";
    return 0;
}
