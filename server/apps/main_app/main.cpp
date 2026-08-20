#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/metadata_timing.hpp"
#include "config_loader/safety_server_config.hpp"
#include "input/aruco_metadata_parser.hpp"
#include "input/metadata_router.hpp"
#include "input/onvif_metadata_parser.hpp"
#include "input/rtp_metadata_receiver.hpp"
#include "logic/pipeline/safety_frame_pipeline.hpp"
#include "logging/aruco_csv_logger.hpp"
#include "logging/csv_logger.hpp"
#include "logging/event_logger.hpp"
#include "logging/latency_logger.hpp"
#include "logging/logger.hpp"
#include "network/assignment_publisher.hpp"
#include "network/network_sensor_reader.hpp"
#include "network/result_dispatcher.hpp"
#include "network/result_publisher.hpp"
#include "network/sensor_uplink_receiver.hpp"

namespace {

std::atomic<bool> stop_requested{false};

void onSignal(int) { stop_requested = true; }

std::vector<std::string> configuredTerminalIds(
    const std::vector<forklift::config::ForkliftDevice>& devices) {
    std::vector<std::string> ids;
    ids.reserve(devices.size());
    for (const auto& device : devices) ids.push_back(device.terminal_id);
    return ids;
}

std::string alertTarget(const JudgmentResult& result) {
    return result.terminal_id.empty() ? std::string("지게차")
                                     : "[" + result.terminal_id + "]";
}

std::string alertContext(const JudgmentResult& result) {
    std::string context;
    if (result.distance_mm >= 0.0) {
        context = "거리: " + std::to_string(static_cast<int>(result.distance_mm)) + "mm";
    }
    const std::string stream = result.stream_id.empty()
                                   ? (result.source_camera_id.empty() ? result.camera_id
                                                                      : result.source_camera_id)
                                   : result.stream_id;
    if (!stream.empty()) {
        if (!context.empty()) context += ", ";
        context += stream;
    }
    return context.empty() ? std::string() : " (" + context + ")";
}

void logAlertTransition(const JudgmentResult& previous, const JudgmentResult& current,
                        int sensor_stale_timeout_ms) {
    const std::string target = alertTarget(current);
    if (previous.final_risk != current.final_risk) {
        const int old_level = static_cast<int>(previous.final_risk);
        const int new_level = static_cast<int>(current.final_risk);
        const std::string transition = toString(previous.final_risk) + " -> " +
                                       toString(current.final_risk);
        const std::string context = alertContext(current);
        if (new_level > old_level && current.final_risk == RiskLevel::EMERGENCY) {
            LOG_ERROR("ALERT", target + " 비상 정지 발령: " + transition + context);
        } else if (new_level > old_level) {
            LOG_WARN("ALERT", target + " 위험도 상승: " + transition + context);
        } else if (current.final_risk == RiskLevel::SAFE) {
            LOG_INFO("ALERT", target + " 위험 해제: " + transition + context);
        } else {
            LOG_INFO("ALERT", target + " 위험도 하락: " + transition + context);
        }
    }

    if (previous.exception == current.exception) return;
    switch (current.exception) {
        case ExceptionState::SENSOR_FAULT:
            LOG_WARN("ALERT", target + " 센서 신호 끊김 (" +
                               std::to_string(sensor_stale_timeout_ms) +
                               "ms 초과 -> 최소 주의 유지)");
            break;
        case ExceptionState::DEAD_RECKONING:
            LOG_WARN("ALERT", target + " 지게차 위치 추적 불가 (마커 미검출 -> 추정 위치 사용)");
            break;
        case ExceptionState::EMERGENCY_IMPACT:
            LOG_ERROR("ALERT", target + " 충돌 충격 감지 (비상 대응 유지)");
            break;
        case ExceptionState::UNCONFIRMED_PROXIMITY:
            LOG_WARN("ALERT", target + " 미확인 근접 감지 (센서 감지, CCTV 미확인)");
            break;
        case ExceptionState::NONE:
            LOG_INFO("ALERT", target + " 예외 상태 해제 (정상 판정 복귀)");
            break;
    }
}

// 한 worker가 만든 완성 프레임을 중앙 처리 루프로 넘길 때 사용하는 이벤트다.
struct MetadataEvent {
    enum class Type { Object, Aruco } type;
    MetadataFrame object;
    ArucoFrame aruco;
};

struct TerminalContext;

// TERM 하나의 상태. 마커·센서·판정 히스테리시스·결과 발행기를 TERM별로 분리한다.
struct TerminalContext {
    forklift::config::ForkliftDevice device;
    StubSensorReader stub_sensor;
    NetworkSensorReader sensor_reader;
    forklift::logic::SafetyFramePipeline pipeline;
    risk_transport::ResultPublisher publisher;
    risk_transport::ResultDispatcher dispatcher;

    TerminalContext(forklift::config::SafetyServerConfig config,
                    forklift::config::ForkliftDevice forklift,
                    risk_transport::SensorUplinkReceiver& receiver)
        : device(std::move(forklift)),
          stub_sensor(config.sensor.stub_tof_distance_mm),
          sensor_reader(receiver, device.terminal_id, config.sensor.stale_timeout_ms),
          pipeline(config, device, sensor_reader),
          publisher(device.terminal_id, config.network.mqtt_host, config.network.mqtt_port,
                    risk_transport::MqttTlsOptions{config.network.tls_enabled, config.network.ca_cert_path,
                                                   config.network.client_cert_path, config.network.client_key_path},
                    risk_transport::ResultPublisherRole::RiskResult),
          dispatcher([this](const std::string& json) { publisher.publish(json); },
                     std::chrono::milliseconds(config.network.result_heartbeat_ms)) {}
};

// 중앙 서버. RTSP worker는 여기로 프레임만 넣고, 위험 판정은 이 클래스의 한 스레드에서만 한다.
class CentralServer {
public:
    explicit CentralServer(forklift::config::SafetyServerConfig config)
        : config_(std::move(config)),
          sensor_receiver_(configuredTerminalIds(config_.forklifts),
                           config_.network.mqtt_host, config_.network.mqtt_port,
                           tlsOptions()),
          server_status_("SERVER", config_.network.mqtt_host, config_.network.mqtt_port,
                         tlsOptions(), risk_transport::ResultPublisherRole::ServerStatus),
          assignment_publisher_(config_.network.mqtt_host, config_.network.mqtt_port,
                                tlsOptions()),
          event_logger_(config_.output_storage.event_db),
          latency_logger_(config_.output_storage.latency_csv),
          object_logger_(config_.output_storage.object_csv,
                         config_.output_storage.enable_raw_csv_logging),
          aruco_logger_(config_.output_storage.aruco_csv,
                        config_.output_storage.enable_raw_csv_logging) {
        for (const auto& device : config_.forklifts)
            terminals_.push_back(makeTerminal(device));
    }

    ~CentralServer();

    void start();
    void startWorkers();
    void stop();

    // worker callback은 여기서 큐에만 넣는다. 여러 GStreamer 스레드가 동시에
    // 들어와도 판정 객체를 직접 건드리지 않으므로 TERM 상태가 서로 섞이지 않는다.
    void enqueue(MetadataEvent event) {
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            events_.push(std::move(event));
        }
        event_cv_.notify_one();
    }

    const forklift::config::SafetyServerConfig& config() const { return config_; }

private:
    struct StreamWorker;

    std::unique_ptr<TerminalContext> makeTerminal(const forklift::config::ForkliftDevice& device);
    risk_transport::MqttTlsOptions tlsOptions() const {
        return {config_.network.tls_enabled, config_.network.ca_cert_path,
                config_.network.client_cert_path, config_.network.client_key_path};
    }

    void processLoop() {
        for (;;) {
            MetadataEvent event;
            {
                std::unique_lock<std::mutex> lock(event_mutex_);
                event_cv_.wait(lock, [this] { return !running_ || !events_.empty(); });
                if (!running_ && events_.empty()) break;
                event = std::move(events_.front());
                events_.pop();
            }
            process(event);
        }
    }

    // 중앙 큐에서 꺼낸 프레임을 객체/ArUco로 나누고, 각 TERM에 독립적으로 전달한다.
    void process(const MetadataEvent& event);

    forklift::config::SafetyServerConfig config_;
    risk_transport::SensorUplinkReceiver sensor_receiver_;
    risk_transport::ResultPublisher server_status_;
    risk_transport::AssignmentPublisher assignment_publisher_;
    risk_log::EventLogger event_logger_;
    risk_log::LatencyLogger latency_logger_;
    CsvLogger object_logger_;
    ArucoCsvLogger aruco_logger_;
    std::vector<std::unique_ptr<TerminalContext>> terminals_;
    std::vector<std::unique_ptr<StreamWorker>> workers_;
    std::queue<MetadataEvent> events_;
    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::atomic<bool> running_{false};
    std::thread process_thread_;
};

std::unique_ptr<TerminalContext> CentralServer::makeTerminal(
    const forklift::config::ForkliftDevice& device) {
    return std::make_unique<TerminalContext>(config_, device, sensor_receiver_);
}

void CentralServer::process(const MetadataEvent& event) {
    if (event.type == MetadataEvent::Type::Object) {
        if (config_.output_storage.enable_raw_csv_logging) {
            object_logger_.logFrame(event.object);
        }
        for (auto& terminal : terminals_) {
            const double now_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto output = terminal->pipeline.processObjectFrame(event.object, now_s);
            terminal->dispatcher.submit(output.judgment.result);
        }
        return;
    }

    if (config_.output_storage.enable_raw_csv_logging) {
        aruco_logger_.logFrame(event.aruco);
    }
    for (auto& terminal : terminals_) {
        const auto changed = terminal->pipeline.processArucoStreamFrame(event.aruco);
        if (!changed) continue;
        assignment_publisher_.publish(terminal->device.terminal_id, *changed,
                                      event.aruco.camera_id, event.aruco.channel,
                                      nowIso8601Ms());
        LOG_INFO("HANDOVER", terminal->device.terminal_id + " 관제 채널 자동 전환 -> [" + *changed + "]");
    }
}

struct CentralServer::StreamWorker {
    CentralServer& server;
    forklift::config::CameraStreamConfig stream;
    // 재조립기는 스트림마다 하나씩 둔다. 한 카메라의 RTP 유실이 다른 카메라의
    // XML 조립 상태를 망가뜨리지 않는다.
    OnvifMetadataReassembler reassembler;
    std::atomic<bool> running{false};
    std::thread thread;
    GstElement* pipeline = nullptr;

    StreamWorker(CentralServer& owner, forklift::config::CameraStreamConfig setting)
        : server(owner), stream(std::move(setting)) {}
    ~StreamWorker() { stop(); }

    void start() {
        if (running.exchange(true)) return;
        thread = std::thread(&StreamWorker::run, this);
    }
    void stop() {
        if (!running.exchange(false)) return;
        if (pipeline) gst_element_set_state(pipeline, GST_STATE_NULL);
        if (thread.joinable()) thread.join();
    }

    static GstFlowReturn onSample(GstAppSink* sink, gpointer user_data) {
        auto* worker = static_cast<StreamWorker*>(user_data);
        GstSample* sample = gst_app_sink_pull_sample(sink);
        if (!sample) return GST_FLOW_ERROR;
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map{};
        if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            RtpHeaderInfo header;
            if (parseRtpHeader(map.data, map.size, header)) {
                auto xml = worker->reassembler.feed(map.data + header.headerLength,
                                                    map.size - header.headerLength,
                                                    header.sequenceNumber, header.marker);
                if (xml) worker->handleXml(*xml);
            }
            gst_buffer_unmap(buffer, &map);
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    void handleXml(const std::string& xml) {
        const MetadataType type = classifyMetadata(xml);
        if (type == MetadataType::ObjectDetection) {
            auto frame = parseOnvifMetadata(xml);
            const auto timing = forklift::common::makeMetadataTiming(
                frame.utcTime, std::chrono::system_clock::now());
            frame.serverReceivedUtc = timing.server_received_utc;
            frame.deltaMs = timing.delta_ms;
            frame.stream_id = stream.stream_id;
            frame.camera_id = stream.camera_id;
            frame.channel = stream.channel;
            server.enqueue({MetadataEvent::Type::Object, std::move(frame), {}});
            return;
        }
        if (type != MetadataType::ArucoDetection) return;
        auto parsed = parseArucoMetadata(xml);
        if (!parsed) return;
        if (parsed->channel != stream.channel) {
            LOG_WARN("CCTV", stream.stream_id + " ArUco 채널 불일치 (메타데이터: " +
                               std::to_string(parsed->channel) + ", 설정: " +
                               std::to_string(stream.channel) + " -> 프레임 제외)");
            return;
        }
        auto frame = *parsed;
        const auto timing = forklift::common::makeMetadataTiming(
            frame.utcTime, std::chrono::system_clock::now());
        frame.serverReceivedUtc = timing.server_received_utc;
        frame.deltaMs = timing.delta_ms;
        frame.stream_id = stream.stream_id;
        frame.camera_id = stream.camera_id;
        server.enqueue({MetadataEvent::Type::Aruco, {}, std::move(frame)});
    }

    void run() {
        int failures = 0;
        bool has_connected_before = false;
        const int max_retries = server.config().stream.max_retries;
        while (running && !stop_requested) {
            reassembler.reset();
            // application 트랙만 연결한다. 영상 트랙은 받지 않아 Pi의 메모리와
            // CPU를 메타데이터 처리에 집중시킨다.
            const std::string description =
                "rtspsrc location=\"" + stream.rtsp_url + "\" protocols=tcp do-rtsp-keep-alive=true tcp-timeout=30000000 latency=" +
                std::to_string(server.config().stream.rtsp_latency_ms) +
                " name=source source. ! application/x-rtp,media=application ! queue ! "
                "appsink name=metadata emit-signals=true sync=false max-buffers=" +
                std::to_string(server.config().stream.appsink_max_buffers) + " drop=true";
            GError* error = nullptr;
            GstElement* graph = gst_parse_launch(description.c_str(), &error);
            if (!graph) {
                if (error) {
                    LOG_ERROR("CCTV", stream.stream_id + " 스트림 생성 실패 (사유: " +
                                         std::string(error->message) + ")");
                    g_error_free(error);
                }
                if (++failures >= max_retries) {
                    LOG_ERROR("CCTV", stream.stream_id + " 제외 (사유: 재연결 " +
                                         std::to_string(max_retries) + "회 실패)");
                    break;
                }
                retry();
                continue;
            }
            pipeline = graph;
            GstElement* sink = gst_bin_get_by_name(GST_BIN(graph), "metadata");
            GstAppSinkCallbacks callbacks{};
            callbacks.new_sample = &StreamWorker::onSample;
            gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);
            gst_object_unref(sink);
            gst_element_set_state(graph, GST_STATE_PLAYING);
            GstBus* bus = gst_element_get_bus(graph);
            bool retry_needed = false;
            bool reached_playing = false;
            auto logConnected = [&]() {
                if (!has_connected_before) {
                    LOG_INFO("CCTV", stream.stream_id + " 카메라 연결 성공 (최초)");
                    has_connected_before = true;
                } else {
                    LOG_INFO("CCTV", stream.stream_id + " 카메라 재연결 성공 (정상 복구)");
                }
                reached_playing = true;
                failures = 0;
            };
            const auto connected_at = std::chrono::steady_clock::now();
            while (running && !stop_requested) {
                GstMessage* message = gst_bus_timed_pop_filtered(
                    bus, 200 * GST_MSECOND,
                    static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
                if (!reached_playing) {
                    GstState cur_state = GST_STATE_NULL;
                    gst_element_get_state(graph, &cur_state, nullptr, 0);
                    if (cur_state == GST_STATE_PLAYING) {
                        logConnected();
                    }
                }
                if (!message) {
                    const auto elapsed = std::chrono::steady_clock::now() - connected_at;
                    if (!reached_playing && elapsed > std::chrono::seconds(server.config().stream.connect_timeout_s)) {
                        ++failures;
                        LOG_WARN("CCTV", stream.stream_id + " 카메라 응답 없음 (" +
                                             std::to_string(server.config().stream.connect_timeout_s) +
                                             "초 타임아웃 -> 재연결 " + std::to_string(failures) +
                                             "/" + std::to_string(max_retries) + ")");
                        retry_needed = true;
                        break;
                    }
                    continue;
                }
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    GError* detail = nullptr; gchar* debug = nullptr;
                    gst_message_parse_error(message, &detail, &debug);
                    ++failures;
                    std::string reason = detail ? detail->message : "네트워크 오류";
                    LOG_WARN("CCTV", stream.stream_id + " 카메라 연결 끊김 (사유: " + reason +
                                         " -> 재연결 " + std::to_string(failures) + "/" +
                                         std::to_string(max_retries) + ")");
                    if (detail) g_error_free(detail);
                    if (debug) g_free(debug);
                    retry_needed = true;
                } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                    LOG_INFO("CCTV", stream.stream_id + " 카메라 연결 재설정 (사유: 세션 만료 -> 자동 갱신)");
                    retry_needed = true;
                } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_STATE_CHANGED &&
                           GST_MESSAGE_SRC(message) == GST_OBJECT(graph)) {
                    GstState old_state, new_state, pending;
                    gst_message_parse_state_changed(message, &old_state, &new_state, &pending);
                    if (new_state == GST_STATE_PLAYING) {
                        if (!reached_playing) {
                            logConnected();
                        }
                    }
                }
                gst_message_unref(message);
                if (retry_needed) break;
            }
            gst_object_unref(bus);
            gst_element_set_state(graph, GST_STATE_NULL);
            gst_object_unref(graph);
            pipeline = nullptr;
            if (!retry_needed || stop_requested) break;
            if (failures >= max_retries) {
                LOG_ERROR("CCTV", stream.stream_id + " 제외 (사유: 재연결 " +
                                     std::to_string(max_retries) + "회 실패)");
                break;
            }
            retry();
        }
    }

    void retry() {
        const int seconds = server.config().stream.retry_delay_s;
        for (int i = 0; i < seconds && running && !stop_requested; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
};

CentralServer::~CentralServer() { stop(); }

void CentralServer::start() {
    sensor_receiver_.start();
    server_status_.start();
    assignment_publisher_.start();
    event_logger_.start();
    if (config_.output_storage.enable_raw_csv_logging) {
        latency_logger_.start();
    }
    for (auto& terminal : terminals_) {
        terminal->publisher.start();
        terminal->dispatcher.onStateChangeEvent(
            [this](const JudgmentResult& result, int previous) {
                event_logger_.log(result, previous);
            });
        terminal->dispatcher.onAlert(
            [this](const JudgmentResult& previous, const JudgmentResult& current) {
                logAlertTransition(previous, current, config_.sensor.stale_timeout_ms);
            });
        if (config_.output_storage.enable_raw_csv_logging) {
            terminal->dispatcher.onLatencyEvent(
                [this](const LatencyStamps& stamps) { latency_logger_.log(stamps); });
        }
        JudgmentResult idle = risk_transport::ResultDispatcher::idleResult();
        idle.terminal_id = terminal->device.terminal_id;
        terminal->dispatcher.primeIdle(idle);
        terminal->dispatcher.start();
    }
    running_ = true;
    process_thread_ = std::thread(&CentralServer::processLoop, this);
}

void CentralServer::startWorkers() {
    for (const auto& stream : config_.streams)
        workers_.push_back(std::make_unique<StreamWorker>(*this, stream));
    for (auto& worker : workers_) worker->start();
}

void CentralServer::stop() {
    if (!running_.exchange(false)) return;
    for (auto& worker : workers_) worker->stop();
    event_cv_.notify_all();
    if (process_thread_.joinable()) process_thread_.join();
    for (auto& terminal : terminals_) terminal->dispatcher.stop();
    for (auto& terminal : terminals_) terminal->publisher.stop();
    assignment_publisher_.stop();
    server_status_.stop();
    sensor_receiver_.stop();
    event_logger_.stop();
    if (config_.output_storage.enable_raw_csv_logging) {
        latency_logger_.stop();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    g_setenv("GIO_USE_PROXY_RESOLVER", "dummy", TRUE);
    gst_init(&argc, &argv);
    std::string config_dir = forklift::config::resolveConfigDirectory();
    std::string common_config_dir;
    bool enable_debug_csv_flag = false;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--config-dir" && index + 1 < argc) {
            config_dir = argv[++index];
        } else if (option == "--common-config-dir" && index + 1 < argc) {
            common_config_dir = argv[++index];
        } else if (option == "--enable-debug-csv" || option == "--debug") {
            enable_debug_csv_flag = true;
        } else if (option == "--help" || option == "-h") {
            std::cout << "사용법: " << argv[0] << " [옵션]\n\n"
                      << "옵션:\n"
                      << "  --config-dir PATH         안전 설정 디렉터리 경로 (기본값: 자동 감지)\n"
                      << "  --common-config-dir PATH  공통 설정 디렉터리 경로 (기본값: 자동 감지)\n"
                      << "  --debug, --enable-debug-csv 디버그 원시 CSV 로깅 활성화\n"
                      << "  --help, -h                도움말 출력\n";
            return 0;
        } else {
            std::cerr << "사용법: " << argv[0]
                      << " [--config-dir PATH] [--common-config-dir PATH] [--debug] [--help]\n";
            return 1;
        }
    }
    if (common_config_dir.empty())
        common_config_dir = forklift::config::resolveCommonConfigDirectory(config_dir);

    forklift::config::SafetyServerConfig config;
    try {
        config = forklift::config::loadMultiCameraServerConfig(config_dir, common_config_dir);
    } catch (const forklift::config::SafetyServerConfigError& error) {
        LOG_ERROR("CONFIG", std::string("서버 기동 실패 (사유: ") + error.what() + ")");
        return 2;
    }
    if (enable_debug_csv_flag) {
        config.output_storage.enable_raw_csv_logging = true;
    }
    forklift::logging::Logger::instance().setDebugEnabled(
        config.output_storage.enable_raw_csv_logging ||
        risk_transport::ResultDispatcher::sendLogEnabled());

    const auto storage_parent = std::filesystem::path(config.output_storage.event_db).parent_path();
    if (!storage_parent.empty()) {
        std::filesystem::create_directories(storage_parent);
        const auto server_log_path = (storage_parent / "server.log").string();
        if (!forklift::logging::Logger::instance().setLogFile(server_log_path)) {
            LOG_WARN("STORAGE", "서버 로그 파일 열기 실패 (경로: " + server_log_path +
                                  " -> 콘솔 출력 유지)");
        }
    }

    for (const auto* path : {&config.output_storage.object_csv, &config.output_storage.aruco_csv,
                             &config.output_storage.event_db, &config.output_storage.latency_csv}) {
        const auto parent = std::filesystem::path(*path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    CentralServer server(std::move(config));
    server.start();
    server.startWorkers();
    LOG_INFO("SERVER", "중앙 안전 서버 기동 완료 (CCTV 스트림: " +
                           std::to_string(server.config().streams.size()) + "개, 지게차 단말: " +
                           std::to_string(server.config().forklifts.size()) + "대)");
    while (!stop_requested) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    LOG_INFO("SERVER", "서버 종료 신호 감지 (안전 종료 진행)");
    server.stop();
    LOG_INFO("SERVER", "중앙 안전 서버 정상 종료 완료");
    return 0;
}
