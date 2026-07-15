// main.cpp
//
// 입력값: 카메라 RTSP URL
// 처리과정:
//   1) GStreamer로 RTSP 세션을 열고 media=application 트랙만 골라 appsink로 흘려보냄
//   2) appsink 콜백에서 패킷 하나(raw RTP)를 받을 때마다 RTP 헤더 파싱 -> payload 추출
//   3) OnvifMetadataReassembler에 순서대로 넣어서 marker bit 뜰 때까지 이어붙임
//      (시퀀스 번호 연속성도 같이 검사해서, 패킷이 중간에 빠지면 깨진 프레임을 버림)
//   4) 완성된 XML이 나오면 parseOnvifMetadata()로 파싱
//   5) 콘솔에 출력 + CSV 파일에 누적 저장 (재연결돼도 같은 CSV 파일에 계속 이어붙여짐)
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

#include "rtp_metadata_receiver.hpp"
#include "onvif_metadata_parser.hpp"
#include "csv_logger.hpp"

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

// appsink 콜백에서 공유할 상태 (재조립기 + 로거)
struct AppState {
    OnvifMetadataReassembler reassembler;
    CsvLogger* logger = nullptr;
};

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
                MetadataFrame frame = parseOnvifMetadata(*completedXml);
                printFrame(frame);
                if (state->logger) {
                    state->logger->logFrame(frame);
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
        std::cerr << "사용법: " << argv[0] << " <RTSP URL>\n";
        std::cerr << "예: " << argv[0]
                  << " \"rtsp://<user>:<password>@192.168.0.3:554/0/onvif/profile2/media.smp\"\n";
        return 1;
    }

    gst_init(&argc, &argv);
    AppState state;

    // CSV 파일은 딱 한 번만 생성. 재연결이 몇 번 일어나든 같은 파일에 계속 이어붙여짐
    // (CsvLogger는 파일이 이미 있으면 헤더 없이 append하는 구조라 문제없음).
    std::time_t now = std::time(nullptr);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", std::localtime(&now));
    std::string csvPath = std::string("detections_") + timeBuf + ".csv";
    CsvLogger logger(csvPath);
    state.logger = &logger;
    std::cout << "CSV 저장 경로: " << csvPath << "\n";
    std::cout << "수신 시작 시각(로컬 PC 기준): " << timeBuf << "\n";
    std::cout << "※ 이 시각은 PC 로컬시간이고, 프레임별 UtcTime은 카메라 기준 시간이니 "
                 "나중에 정밀 동기화할 때 헷갈리지 않도록 주의.\n";

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

    std::cout << "종료됨. 파일 저장 완료(또는 위 경고 참고).\n";
    return 0;
}