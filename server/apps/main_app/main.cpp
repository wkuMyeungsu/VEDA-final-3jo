#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common/bounded_queue.hpp"
#include "common/metadata_timing.hpp"
#include "common/platform.hpp"
#include "config_loader/safety_server_config.hpp"
#include "input/aruco_metadata_parser.hpp"
#include "input/metadata_router.hpp"
#include "input/onvif_metadata_parser.hpp"
#include "input/rtp_metadata_receiver.hpp"
#include "logic/judgment/announcement_gate.hpp"
#include "logic/pipeline/safety_frame_pipeline.hpp"
#include "logging/aruco_csv_logger.hpp"
#include "logging/csv_logger.hpp"
#include "logging/event_logger.hpp"
#include "logging/latency_logger.hpp"
#include "logging/localization_log_gate.hpp"
#include "logging/logger.hpp"
#include "network/assignment_publisher.hpp"
#include "network/network_sensor_reader.hpp"
#include "network/result_dispatcher.hpp"
#include "network/result_publisher.hpp"
#include "network/sensor_uplink_receiver.hpp"
#include "runtime/server_command_line_options.hpp"

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
    return result.terminal_id.empty() ? std::string("지게차") : result.terminal_id;
}

std::string alertContext(const JudgmentResult& result) {
    std::string context;
    if (result.distance_mm >= 0.0) {
        context = " · " + std::to_string(static_cast<int>(result.distance_mm)) + "mm";
    }
    const std::string stream = result.stream_id.empty()
                                   ? (result.source_camera_id.empty() ? result.camera_id
                                                                      : result.source_camera_id)
                                   : result.stream_id;
    if (!stream.empty()) context += " · " + stream;
    return context;
}

const char* exceptionLogExtra(ExceptionState state) {
    switch (state) {
        case ExceptionState::SENSOR_FAULT: return " · 센서 끊김";
        case ExceptionState::DEAD_RECKONING: return " · 위치 추정";
        case ExceptionState::EMERGENCY_IMPACT: return " · 충돌 충격";
        case ExceptionState::UNCONFIRMED_PROXIMITY: return " · 미확인 근접";
        case ExceptionState::NONE: return "";
    }
    return "";
}

const char* sensorModeLogLabel(forklift::runtime::SensorMode mode) {
    return mode == forklift::runtime::SensorMode::Disabled ? "입력 제외(테스트)" : "네트워크";
}

std::string jsonString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char c : value) {
        if (c == '\\' || c == '"') escaped.push_back('\\');
        if (c == '\n') { escaped += "\\n"; continue; }
        if (c == '\r') { escaped += "\\r"; continue; }
        if (c == '\t') { escaped += "\\t"; continue; }
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

const char* localizationLogLabel(const std::string& status) {
    if (status == "LOCALIZED") return "위치 확인";
    if (status == "MARKER_NOT_DETECTED") return "마커 미검출";
    if (status == "HOMOGRAPHY_UNAVAILABLE") return "좌표 변환 불가";
    return "ArUco 대기";
}

std::string formatLocalizationMessage(
    const std::string& terminal_id,
    const forklift::logic::SafetyFramePipeline::LocalizationStatus& localization) {
    std::ostringstream message;
    message << terminal_id << " " << localizationLogLabel(localization.status)
            << " · 마커 " << localization.configured_marker_id;
    const std::string stream = !localization.active_stream_id.empty()
                                   ? localization.active_stream_id
                                   : (!localization.last_target_marker_stream_id.empty()
                                          ? localization.last_target_marker_stream_id
                                          : localization.last_aruco_frame_stream_id);
    if (!stream.empty() && localization.status != "WAITING_FOR_ARUCO")
        message << " · " << stream;
    return message.str();
}

const char* linkStateName(risk_transport::LinkState state) {
    switch (state) {
        case risk_transport::LinkState::CONNECTED: return "connected";
        case risk_transport::LinkState::CONNECTING: return "connecting";
        case risk_transport::LinkState::DISCONNECTED: return "disconnected";
    }
    return "unknown";
}

void logAlertTransition(const JudgmentResult& previous, const JudgmentResult& current,
                        int sensor_stale_timeout_ms) {
    const bool risk_changed = previous.final_risk != current.final_risk;
    const bool exception_changed = previous.exception != current.exception;
    if (!risk_changed && !exception_changed) return;

    const std::string target = alertTarget(current);
    const std::string context = alertContext(current);
    if (risk_changed) {
        const int old_level = static_cast<int>(previous.final_risk);
        const int new_level = static_cast<int>(current.final_risk);
        const std::string transition = toString(previous.final_risk) + " → " +
                                       toString(current.final_risk);
        std::string extra;
        if (exception_changed) extra = exceptionLogExtra(current.exception);
        const std::string message = target + " " + transition + context + extra;
        if (new_level > old_level && current.final_risk == RiskLevel::EMERGENCY) {
            LOG_ERROR("ALERT", target + " 비상 정지 " + transition + context + extra);
        } else if (new_level > old_level) {
            LOG_WARN("ALERT", message);
        } else {
            LOG_INFO("ALERT", message);
        }
        return;
    }

    switch (current.exception) {
        case ExceptionState::SENSOR_FAULT:
            LOG_WARN("ALERT", target + " 센서 끊김 · " +
                               std::to_string(sensor_stale_timeout_ms) + "ms 초과");
            break;
        case ExceptionState::DEAD_RECKONING:
            LOG_WARN("ALERT", target + " 위치 추정 · 마커 미검출");
            break;
        case ExceptionState::EMERGENCY_IMPACT:
            LOG_ERROR("ALERT", target + " 충돌 충격");
            break;
        case ExceptionState::UNCONFIRMED_PROXIMITY:
            LOG_WARN("ALERT", target + " 미확인 근접 · 센서만 감지");
            break;
        case ExceptionState::NONE:
            LOG_INFO("ALERT", target + " 예외 해제");
            break;
    }
}

// 한 worker가 만든 완성 프레임을 중앙 처리 루프로 넘길 때 사용하는 이벤트다.
struct MetadataEvent {
    enum class Type { Object, Aruco, Tick } type;
    MetadataFrame object;
    ArucoFrame aruco;
};

struct TerminalContext;

// TERM 하나의 상태. 마커·센서·판정 히스테리시스·결과 발행기를 TERM별로 분리한다.
struct TerminalContext {
    forklift::config::ForkliftDevice device;
    NetworkSensorReader sensor_reader;
    forklift::logic::SafetyFramePipeline pipeline;
    risk_transport::ResultPublisher publisher;
    risk_transport::ResultDispatcher dispatcher;
    forklift::logging::LocalizationLogGate localization_log_gate;
    AnnouncementGate announcement_gate;
    std::string last_assignment_stream;

    TerminalContext(forklift::config::SafetyServerConfig config,
                    forklift::config::ForkliftDevice forklift,
                    risk_transport::SensorUplinkReceiver& receiver,
                    forklift::runtime::SensorMode sensor_mode)
        : device(std::move(forklift)),
          sensor_reader(receiver, device.terminal_id, config.sensor.stale_timeout_ms),
          pipeline(config, device, sensor_reader,
                   sensor_mode == forklift::runtime::SensorMode::Disabled),
          publisher(device.terminal_id, config.network.mqtt_host, config.network.mqtt_port,
                    risk_transport::MqttTlsOptions{config.network.tls_enabled, config.network.ca_cert_path,
                                                   config.network.client_cert_path, config.network.client_key_path},
                    risk_transport::ResultPublisherRole::RiskResult),
          dispatcher([this](const std::string& json) { publisher.publish(json); },
                     std::chrono::milliseconds(config.network.result_heartbeat_ms)) {}
};

void logLocalizationIfNeeded(TerminalContext& terminal) {
    const auto localization = terminal.pipeline.localizationStatus();
    if (!terminal.localization_log_gate.shouldLog(localization.status,
                                                  std::chrono::steady_clock::now())) {
        return;
    }
    const std::string message = formatLocalizationMessage(terminal.device.terminal_id, localization);
    if (localization.status == "LOCALIZED")
        LOG_INFO("CCTV", message);
    else
        LOG_WARN("CCTV", message);
}

// 중앙 서버. RTSP worker는 여기로 프레임만 넣고, 위험 판정은 이 클래스의 한 스레드에서만 한다.
class CentralServer {
public:
    CentralServer(forklift::config::SafetyServerConfig config,
                  forklift::runtime::SensorMode sensor_mode)
        : config_(std::move(config)),
          sensor_mode_(sensor_mode),
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
                         config_.output_storage.enable_object_csv_logging),
          aruco_logger_(config_.output_storage.aruco_csv,
                        config_.output_storage.enable_aruco_csv_logging),
          events_(static_cast<std::size_t>(config_.stream.metadata_queue_capacity)),
          server_started_utc_(nowIso8601Ms()) {
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
        const auto result = events_.push(std::move(event));
        if (result.dropped_oldest &&
            (result.dropped_total == 1 || result.dropped_total % 100 == 0)) {
            LOG_WARN("SERVER", "메타데이터 처리 대기열 초과 (오래된 프레임 건너뜀, 누적: " +
                                   std::to_string(result.dropped_total) + ")");
        }
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
        MetadataEvent event;
        while (events_.waitPop(event, running_)) {
            process(event);
        }
    }

    void statusLoop();
    void writeRuntimeStatus(const std::string& state);

    // 중앙 큐에서 꺼낸 프레임을 객체/ArUco로 나누고, 각 TERM에 독립적으로 전달한다.
    void process(const MetadataEvent& event);

    forklift::config::SafetyServerConfig config_;
    forklift::runtime::SensorMode sensor_mode_;
    risk_transport::SensorUplinkReceiver sensor_receiver_;
    risk_transport::ResultPublisher server_status_;
    risk_transport::AssignmentPublisher assignment_publisher_;
    risk_log::EventLogger event_logger_;
    risk_log::LatencyLogger latency_logger_;
    CsvLogger object_logger_;
    ArucoCsvLogger aruco_logger_;
    std::vector<std::unique_ptr<TerminalContext>> terminals_;
    std::vector<std::unique_ptr<StreamWorker>> workers_;
    forklift::common::BoundedQueue<MetadataEvent> events_;
    const std::string server_started_utc_;
    std::atomic<bool> running_{false};
    std::thread process_thread_;
    std::thread status_thread_;
};

std::unique_ptr<TerminalContext> CentralServer::makeTerminal(
    const forklift::config::ForkliftDevice& device) {
    return std::make_unique<TerminalContext>(config_, device, sensor_receiver_, sensor_mode_);
}

void CentralServer::process(const MetadataEvent& event) {
    if (event.type == MetadataEvent::Type::Object) {
        if (config_.output_storage.enable_object_csv_logging) {
            object_logger_.logFrame(event.object);
        }
        for (auto& terminal : terminals_) {
            const double now_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            // 카메라별 프레임은 관측 버퍼만 갱신한다. 최종 위험 판정/발행은
            // 아래 주기 Tick에서 모든 스트림의 최신 관측을 합친 뒤 한 번 수행한다.
            terminal->pipeline.updateObjectFrame(event.object, now_s);
            logLocalizationIfNeeded(*terminal);
        }
        return;
    }

    if (event.type == MetadataEvent::Type::Tick) {
        // 카메라의 객체 앱은 사람이 없을 때 VideoAnalytics 프레임을 보내지 않을 수
        // 있다. 위험 판정을 객체 이벤트에만 묶으면 센서와 ArUco 위치가 정상이어도
        // 영원히 '판정 대기'가 되므로, 최근 추적 상태를 100ms 주기로 평가한다.
        const double now_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        for (auto& terminal : terminals_) {
            // 한 카메라의 빈/지연 프레임이 다른 카메라의 결과를 덮어쓰지 않도록
            // 단말별로 누적된 전체 스트림 스냅숏만 판정한다.
            auto output = terminal->pipeline.processAggregatedFrame(now_s);
            terminal->dispatcher.submit(terminal->announcement_gate.apply(output.judgment.result));
            logLocalizationIfNeeded(*terminal);
        }
        return;
    }

    if (config_.output_storage.enable_aruco_csv_logging) {
        aruco_logger_.logFrame(event.aruco);
    }
    for (auto& terminal : terminals_) {
        const auto assigned = terminal->pipeline.processArucoStreamFrame(event.aruco);
        logLocalizationIfNeeded(*terminal);
        if (!assigned) continue;
        assignment_publisher_.publish(terminal->device.terminal_id, *assigned,
                                      terminal->pipeline.activeCameraId(),
                                      terminal->pipeline.activeChannel(),
                                      nowIso8601Ms());
        if (terminal->last_assignment_stream != *assigned) {
            LOG_INFO("HANDOVER", terminal->device.terminal_id + " 채널 전환 → " + *assigned);
            terminal->announcement_gate.reset();
            terminal->last_assignment_stream = *assigned;
        }
    }
}

void CentralServer::writeRuntimeStatus(const std::string& state) {
    const std::filesystem::path status_path = config_.output_storage.runtime_status;
    if (status_path.empty()) return;
    const double now_s = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    std::error_code error;
    const auto parent = status_path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    if (error) {
        LOG_WARN("STORAGE", "runtime 상태 디렉터리 생성 실패 (경로: " + parent.string() +
                             ", 사유: " + error.message() + ")");
        return;
    }

    std::ostringstream json;
    json << "{\n"
         << "  \"schema_version\": 3,\n"
         << "  \"state\": " << jsonString(state) << ",\n"
         << "  \"sensor_mode\": " << jsonString(forklift::runtime::sensorModeName(sensor_mode_)) << ",\n"
         << "  \"server_run_id\": " << jsonString(forklift::logging::Logger::instance().runId()) << ",\n"
         << "  \"server_started_utc\": " << jsonString(server_started_utc_) << ",\n"
         << "  \"checked_utc\": " << jsonString(nowIso8601Ms()) << ",\n"
         << "  \"queue\": {\"depth\": " << events_.size()
         << ", \"capacity\": " << config_.stream.metadata_queue_capacity
         << ", \"dropped\": " << events_.droppedTotal() << "},\n"
         << "  \"sensor\": {\"connected\": " << (sensor_receiver_.isConnected() ? "true" : "false")
         << ", \"received\": " << sensor_receiver_.receivedCount()
         << ", \"parse_failures\": " << sensor_receiver_.parseFailureCount() << "},\n"
         << "  \"storage\": {\"events_written\": " << event_logger_.writtenCount()
         << ", \"events_dropped\": " << event_logger_.droppedCount()
         << ", \"events_write_failures\": " << event_logger_.writeFailureCount()
         << ", \"latency_written\": " << latency_logger_.writtenCount()
         << ", \"latency_dropped\": " << latency_logger_.droppedCount() << "},\n";
    // 빈 문자열/미유효 숫자는 JSON null로, 값이 있으면 기록한다.
    auto strOrNull = [&](const std::string& value) {
        if (!value.empty()) json << jsonString(value);
        else json << "null";
    };
    auto numOrNull = [&](bool valid, long long value) {
        if (valid) json << value;
        else json << "null";
    };
    auto writePolygon = [&](const std::vector<forklift::config::SiteMapPoint>& polygon) {
        json << '[';
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            if (index) json << ',';
            json << '[' << polygon[index].x_mm << ',' << polygon[index].y_mm << ']';
        }
        json << ']';
    };
    json << "  \"site_map\": ";
    if (config_.site_map.configured()) {
        json << "{\"unit\": \"mm\", \"name\": " << jsonString(config_.site_map.name)
             << ", \"boundary\": ";
        writePolygon(config_.site_map.boundary);
        json << ", \"zones\": [";
        for (std::size_t index = 0; index < config_.site_map.zones.size(); ++index) {
            const auto& zone = config_.site_map.zones[index];
            if (index) json << ',';
            json << "{\"id\": " << jsonString(zone.id)
                 << ", \"label\": " << jsonString(zone.label)
                 << ", \"kind\": " << jsonString(zone.kind)
                 << ", \"polygon\": ";
            writePolygon(zone.polygon);
            json << '}';
        }
        json << "]}";
    } else {
        json << "null";
    }
    json << ",\n  \"terminals\": [";
    for (std::size_t index = 0; index < terminals_.size(); ++index) {
        const auto& terminal = *terminals_[index];
        const auto sensor = sensor_receiver_.terminalStatus(
            terminal.device.terminal_id, config_.sensor.stale_timeout_ms);
        const auto risk = terminal.dispatcher.runtimeSnapshot();
        const auto localization = terminal.pipeline.localizationStatus();
        const auto people = terminal.pipeline.peopleStatus(now_s);
        if (index) json << ',';
        json << "{\"terminal_id\": " << jsonString(terminal.device.terminal_id)
             << ", \"collision_radius_mm\": " << terminal.device.collision_radius_mm
             << ", \"marker_height_mm\": " << terminal.device.marker_height_mm
             << ", \"risk_link\": " << jsonString(linkStateName(terminal.publisher.state()))
             << ", \"risk_connect_count\": " << terminal.publisher.connectedCount()
             << ", \"risk_queue_dropped\": " << terminal.publisher.droppedCount()
             << ", \"risk_send_failures\": " << terminal.publisher.sendFailureCount()
             << ", \"change_sends\": " << terminal.dispatcher.changeSendCount()
             << ", \"heartbeat_sends\": " << terminal.dispatcher.heartbeatSendCount()
             << ", \"sensor\": {\"has_sample\": " << (sensor.has_sample ? "true" : "false")
             << ", \"stale\": " << (sensor.stale ? "true" : "false")
             << ", \"age_ms\": " << sensor.age_ms
             << ", \"received\": " << sensor.received
             << ", \"parse_failures\": " << sensor.parse_failures << "}"
             << ", \"risk\": {\"has_result\": " << (risk.has_real_result ? "true" : "false")
             << ", \"state\": ";
        if (risk.has_real_result) strOrNull(toString(risk.result.final_risk)); else json << "null";
        json << ", \"exception\": ";
        if (risk.has_real_result) strOrNull(toString(risk.result.exception)); else json << "null";
        json << ", \"distance_mm\": ";
        if (risk.has_real_result && risk.result.distance_mm >= 0.0) json << risk.result.distance_mm;
        else json << "null";
        json << ", \"camera_id\": ";
        strOrNull(!risk.has_real_result ? std::string()
                  : !risk.result.source_camera_id.empty() ? risk.result.source_camera_id
                                                          : risk.result.camera_id);
        json << ", \"stream_id\": ";
        if (risk.has_real_result) strOrNull(risk.result.stream_id); else json << "null";
        json << ", \"channel\": ";
        numOrNull(risk.has_real_result && risk.result.channel >= 0, risk.result.channel);
        json << ", \"last_change_utc\": ";
        strOrNull(risk.last_change_utc);
        json << "}"
             << ", " << "\"localization\": {\"status\": "
             << jsonString(localization.status)
             << ", \"configured_marker_id\": " << localization.configured_marker_id
             << ", \"localized\": " << (localization.localized ? "true" : "false")
             << ", \"position\": ";
        if (localization.has_position) {
            json << "{\"x_mm\": " << localization.position.x
                 << ", \"y_mm\": " << localization.position.y << '}';
        } else {
            json << "null";
        }
        json
             << ", \"active_stream_id\": ";
        strOrNull(localization.active_stream_id);
        json << ", \"active_camera_id\": ";
        strOrNull(localization.active_camera_id);
        json << ", \"active_channel\": ";
        numOrNull(localization.active_channel >= 1, localization.active_channel);
        json << ", \"last_aruco_frame_utc\": ";
        strOrNull(localization.last_aruco_frame_utc);
        json << ", \"last_aruco_frame_stream_id\": ";
        strOrNull(localization.last_aruco_frame_stream_id);
        json << ", \"last_aruco_frame_channel\": ";
        numOrNull(localization.last_aruco_frame_channel >= 1, localization.last_aruco_frame_channel);
        json << ", \"last_target_marker_seen_utc\": ";
        strOrNull(localization.last_target_marker_seen_utc);
        json << ", \"last_target_marker_stream_id\": ";
        strOrNull(localization.last_target_marker_stream_id);
        json << ", \"last_target_marker_channel\": ";
        numOrNull(localization.last_target_marker_channel >= 1, localization.last_target_marker_channel);
        json << ", \"last_observed_markers_utc\": ";
        strOrNull(localization.last_observed_markers_utc);
        json << ", \"last_observed_markers_stream_id\": ";
        strOrNull(localization.last_observed_markers_stream_id);
        json << ", \"last_observed_markers_channel\": ";
        numOrNull(localization.last_observed_markers_channel >= 1,
                  localization.last_observed_markers_channel);
        json << ", \"last_observed_marker_ids\": [";
        for (std::size_t marker_index = 0;
             marker_index < localization.last_observed_marker_ids.size(); ++marker_index) {
            if (marker_index) json << ',';
            json << localization.last_observed_marker_ids[marker_index];
        }
        json << "], \"aruco_streams\": [";
        for (std::size_t stream_index = 0;
             stream_index < localization.aruco_streams.size(); ++stream_index) {
            const auto& stream = localization.aruco_streams[stream_index];
            if (stream_index) json << ',';
            json << "{\"stream_id\": ";
            strOrNull(stream.stream_id);
            json << ", \"camera_id\": ";
            strOrNull(stream.camera_id);
            json << ", \"channel\": ";
            numOrNull(stream.channel >= 1, stream.channel);
            json << ", \"last_frame_utc\": ";
            strOrNull(stream.last_frame_utc);
            json << ", \"last_target_marker_seen_utc\": ";
            strOrNull(stream.last_target_marker_seen_utc);
            json << ", \"target_marker_visible\": "
                 << (stream.target_marker_visible ? "true" : "false")
                 << ", \"marker_ids\": [";
            for (std::size_t marker_index = 0;
                 marker_index < stream.marker_ids.size(); ++marker_index) {
                if (marker_index) json << ',';
                json << stream.marker_ids[marker_index];
            }
            json << "]}";
        }
        json << "]}"
             << ", \"people\": {\"count\": " << people.tracks.size()
             << ", \"last_update_utc\": ";
        if (!people.last_update_utc.empty()) json << jsonString(people.last_update_utc);
        else json << "null";
        json << ", \"last_update_age_ms\": ";
        if (people.last_update_s >= 0.0) {
            const double age_ms = (now_s > people.last_update_s)
                                      ? (now_s - people.last_update_s) * 1000.0
                                      : 0.0;
            json << age_ms;
        } else {
            json << "null";
        }
        json << ", \"tracks\": [";
        for (std::size_t person_index = 0; person_index < people.tracks.size(); ++person_index) {
            const auto& person = people.tracks[person_index];
            if (person_index) json << ',';
            const double age_ms = (now_s > person.last_seen_s)
                                      ? (now_s - person.last_seen_s) * 1000.0
                                      : 0.0;
            json << "{\"track_id\": " << person.track_id
                 << ", \"position\": {\"x_mm\": " << person.position.x
                 << ", \"y_mm\": " << person.position.y << "}"
                 << ", \"distance_mm\": ";
            if (person.distance_mm >= 0.0) json << person.distance_mm;
            else json << "null";
            json << ", \"stream_id\": ";
            if (!person.stream_id.empty()) json << jsonString(person.stream_id);
            else json << "null";
            json << ", \"camera_id\": ";
            if (!person.camera_id.empty()) json << jsonString(person.camera_id);
            else json << "null";
            json << ", \"channel\": ";
            if (person.channel >= 1) json << person.channel;
            else json << "null";
            json << ", \"age_ms\": " << age_ms
                 << ", \"missed_frames\": " << person.missed_frames
                 << ", \"observed_utc\": ";
            if (!person.observed_utc.empty()) json << jsonString(person.observed_utc);
            else json << "null";
            json << "}";
        }
        json << "]}"
             << ", \"events\": {\"state_changes\": " << terminal.dispatcher.changeSendCount()
             << ", \"last_change_utc\": ";
        if (!risk.last_change_utc.empty()) json << jsonString(risk.last_change_utc);
        else json << "null";
        json << "}}";
    }
    json << "]\n}\n";

    const std::filesystem::path temporary = status_path.string() + ".tmp";
    {
        std::ofstream file(temporary, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            LOG_WARN("STORAGE", "runtime 상태 파일 열기 실패 (경로: " + temporary.string() + ")");
            return;
        }
        file << json.str();
        file.flush();
        if (!file.good()) {
            LOG_WARN("STORAGE", "runtime 상태 파일 쓰기 실패 (경로: " + temporary.string() + ")");
            return;
        }
    }
    forklift::platform::replaceFile(temporary, status_path, error);
    if (error) {
        LOG_WARN("STORAGE", "runtime 상태 snapshot 교체 실패 (경로: " + status_path.string() +
                             ", 사유: " + error.message() + ")");
    }
}

void CentralServer::statusLoop() {
    while (running_) {
        for (int tick = 0; tick < 10 && running_; ++tick) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (running_) enqueue({MetadataEvent::Type::Tick, {}, {}});
        }
        if (running_) writeRuntimeStatus("online");
    }
}

struct CentralServer::StreamWorker {
    CentralServer& server;
    forklift::config::CameraStreamConfig stream;
    // 재조립기는 스트림마다 하나씩 둔다. 한 카메라의 RTP 유실이 다른 카메라의
    // XML 조립 상태를 망가뜨리지 않는다.
    OnvifMetadataReassembler reassembler;
    std::size_t rtp_parse_failures_ = 0;
    std::size_t rtp_packets_ = 0;
    bool logged_metadata_ = false;
    bool logged_object_ = false;
    bool logged_rtp_ = false;
    bool logged_unknown_ = false;
    bool saw_application_pad_ = false;
    std::atomic<bool> running{false};
    std::thread thread;
    std::mutex pipeline_mutex;
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
        // pipeline은 worker 스레드가 교체·해제한다. 종료 스레드는 잠금 안에서
        // 참조를 하나 확보한 뒤 상태만 내려 use-after-free 경쟁을 피한다.
        GstElement* active_pipeline = nullptr;
        {
            std::lock_guard<std::mutex> lock(pipeline_mutex);
            if (pipeline) {
                active_pipeline = GST_ELEMENT(gst_object_ref(pipeline));
            }
        }
        if (active_pipeline) {
            gst_element_set_state(active_pipeline, GST_STATE_NULL);
            gst_object_unref(active_pipeline);
        }
        if (thread.joinable()) thread.join();
    }

    // async=false appsink은 new-sample 콜백에서 pull_sample이 NULL을 돌려
    // GST_FLOW_ERROR가 나면 그 이후 메타데이터를 영구히 버린다. 콜백 대신
    // worker 루프에서 try_pull_sample로 직접 비운다.
    void consumeSample(GstSample* sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstMapInfo map{};
        if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ)) return;
        consumePayload(map.data, map.size);
        gst_buffer_unmap(buffer, &map);
    }

    static GstPadProbeReturn onMetadataBuffer(GstPad*, GstPadProbeInfo* info, gpointer user_data) {
        auto* worker = static_cast<StreamWorker*>(user_data);
        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0)
            return GST_PAD_PROBE_OK;
        GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (!buffer) return GST_PAD_PROBE_OK;
        GstMapInfo map{};
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            worker->consumePayload(map.data, map.size);
            gst_buffer_unmap(buffer, &map);
        }
        return GST_PAD_PROBE_OK;
    }

    static void onRtspPadAdded(GstElement*, GstPad* pad, gpointer user_data) {
        auto* worker = static_cast<StreamWorker*>(user_data);
        GstCaps* caps = gst_pad_get_current_caps(pad);
        if (!caps) caps = gst_pad_query_caps(pad, nullptr);
        gchar* caps_str = caps ? gst_caps_to_string(caps) : nullptr;
        const std::string caps_text = caps_str ? caps_str : "";
        const bool application = caps_text.find("media=(string)application") != std::string::npos ||
                                 caps_text.find("media=application") != std::string::npos;
        if (application) {
            worker->saw_application_pad_ = true;
            LOG_INFO("CCTV", worker->stream.stream_id + " 메타데이터 트랙 연결");
        }
        g_free(caps_str);
        if (caps) gst_caps_unref(caps);
    }

    void consumePayload(const uint8_t* data, size_t size) {
        ++rtp_packets_;
        if (!logged_rtp_) logged_rtp_ = true;
        RtpHeaderInfo header;
        if (parseRtpHeader(data, size, header)) {
            auto xml = reassembler.feed(data + header.headerLength,
                                        size - header.headerLength,
                                        header.sequenceNumber, header.marker);
            if (xml) handleXml(*xml);
            return;
        }
        // GStreamer가 RTP 헤더를 이미 벗겨 payload만 주는 경우.
        if (looksLikeXml(data, size)) {
            handleXml(std::string(reinterpret_cast<const char*>(data), size));
            return;
        }
        const std::size_t total = ++rtp_parse_failures_;
        if (total == 1 || total % 100 == 0) {
            LOG_WARN("CCTV", stream.stream_id + " 메타데이터 RTP 파싱 실패 · 누적 " +
                               std::to_string(total));
        }
    }

    static bool looksLikeXml(const uint8_t* data, size_t size) {
        size_t offset = 0;
        while (offset < size &&
               (data[offset] == ' ' || data[offset] == '\n' || data[offset] == '\r' ||
                data[offset] == '\t')) {
            ++offset;
        }
        if (offset >= size || data[offset] != '<') return false;
        return true;
    }

    void drainAppsink(GstAppSink* sink) {
        // pull_sample은 preroll 버퍼를 버리고 시작하므로 먼저 preroll을 꺼낸다.
        if (GstSample* preroll = gst_app_sink_try_pull_preroll(sink, 0)) {
            consumeSample(preroll);
            gst_sample_unref(preroll);
        }
        while (GstSample* sample = gst_app_sink_try_pull_sample(sink, 0)) {
            consumeSample(sample);
            gst_sample_unref(sample);
        }
    }

    void handleXml(const std::string& xml) {
        const MetadataType type = classifyMetadata(xml);
        if (type == MetadataType::ObjectDetection) {
            if (!logged_object_) {
                logged_object_ = true;
                LOG_INFO("CCTV", stream.stream_id + " 객체 검출 수신");
            }
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
        if (type != MetadataType::ArucoDetection) {
            if (type == MetadataType::Unknown && !logged_unknown_) {
                logged_unknown_ = true;
                LOG_DEBUG("CCTV", stream.stream_id + " 기타 메타데이터 문서 · 길이 " +
                                      std::to_string(xml.size()));
            }
            return;
        }
        if (!logged_metadata_) {
            logged_metadata_ = true;
            LOG_INFO("CCTV", stream.stream_id + " 마커 검출 수신");
        }
        auto parsed = parseArucoMetadata(xml);
        if (!parsed) return;
        if (parsed->channel != stream.channel) {
            LOG_WARN("CCTV", stream.stream_id + " ArUco 채널 불일치 · 메타데이터 " +
                               std::to_string(parsed->channel) + " · 설정 " +
                               std::to_string(stream.channel));
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
        // 워커마다 GLib 컨텍스트를 분리한다. 여러 채널이 기본 컨텍스트를 같이
        // 돌리면 rtspsrc pad-added가 유실되고, 영상만 PLAYING 된 채 메타데이터
        // 트랙이 안 붙는다. 지게차 ArUco가 있는 CH_02에서 이 증상이 난다.
        GMainContext* ctx = g_main_context_new();
        g_main_context_push_thread_default(ctx);
        const auto pump = [ctx]() {
            while (g_main_context_pending(ctx))
                g_main_context_iteration(ctx, FALSE);
        };
        int failures = 0;
        bool has_connected_before = false;
        const int max_retries = server.config().stream.max_retries;
        while (running && !stop_requested) {
            reassembler.reset();
            rtp_parse_failures_ = 0;
            rtp_packets_ = 0;
            logged_metadata_ = false;
            logged_object_ = false;
            logged_rtp_ = false;
            logged_unknown_ = false;
            saw_application_pad_ = false;
            // application 트랙을 명시적으로 요청해야 카메라가 메타데이터 SETUP을 한다.
            // 영상 패드는 fakesink로 소비만 하고, 실제 처리는 appsink 프로브가 담당한다.
            const std::string description =
                "rtspsrc location=\"" + stream.rtsp_url + "\" protocols=tcp do-rtsp-keep-alive=true tcp-timeout=30000000 latency=" +
                std::to_string(server.config().stream.rtsp_latency_ms) +
                " name=source "
                "source. ! application/x-rtp,media=application ! queue name=metaq ! "
                "appsink name=metadata sync=false async=false max-buffers=" +
                std::to_string(server.config().stream.appsink_max_buffers) + " drop=true "
                "source. ! application/x-rtp,media=video ! queue name=videoq ! fakesink sync=false async=false";
            GError* error = nullptr;
            GstElement* graph = gst_parse_launch(description.c_str(), &error);
            if (!graph) {
                if (error) {
                    LOG_ERROR("CCTV", stream.stream_id + " 스트림 생성 실패 · " +
                                         std::string(error->message));
                    g_error_free(error);
                }
                if (++failures >= max_retries) {
                    LOG_ERROR("CCTV", stream.stream_id + " 제외 · 재연결 " +
                                         std::to_string(max_retries) + "회 실패");
                    break;
                }
                retry();
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(pipeline_mutex);
                pipeline = graph;
            }
            GstElement* source = gst_bin_get_by_name(GST_BIN(graph), "source");
            if (source) {
                g_signal_connect(source, "pad-added", G_CALLBACK(&StreamWorker::onRtspPadAdded), this);
                gst_object_unref(source);
            }
            GstElement* sink = gst_bin_get_by_name(GST_BIN(graph), "metadata");
            if (sink) {
                GstPad* sinkpad = gst_element_get_static_pad(sink, "sink");
                if (sinkpad) {
                    gst_pad_add_probe(sinkpad, GST_PAD_PROBE_TYPE_BUFFER,
                                      &StreamWorker::onMetadataBuffer, this, nullptr);
                    gst_object_unref(sinkpad);
                }
            }
            GstBus* bus = gst_element_get_bus(graph);
            gst_element_set_state(graph, GST_STATE_PLAYING);
            for (int i = 0; i < 30 && running && !stop_requested; ++i) {
                pump();
                g_usleep(10000);
            }
            bool retry_needed = false;
            bool reached_playing = false;
            std::chrono::steady_clock::time_point playing_at{};
            auto logConnected = [&]() {
                if (!has_connected_before) {
                    LOG_INFO("CCTV", stream.stream_id + " 연결 성공 (최초)");
                    has_connected_before = true;
                } else {
                    LOG_INFO("CCTV", stream.stream_id + " 재연결 성공");
                }
                reached_playing = true;
                playing_at = std::chrono::steady_clock::now();
                failures = 0;
            };
            const auto connected_at = std::chrono::steady_clock::now();
            while (running && !stop_requested) {
                pump();
                GstMessage* message = gst_bus_timed_pop_filtered(
                    bus, 50 * GST_MSECOND,
                    static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
                if (!reached_playing) {
                    GstState cur_state = GST_STATE_NULL;
                    gst_element_get_state(graph, &cur_state, nullptr, 0);
                    if (cur_state == GST_STATE_PLAYING) {
                        logConnected();
                    }
                }
                if (!message) {
                    const auto now = std::chrono::steady_clock::now();
                    const auto elapsed = now - connected_at;
                    if (!reached_playing && elapsed > std::chrono::seconds(server.config().stream.connect_timeout_s)) {
                        ++failures;
                        GstState timeout_state = GST_STATE_NULL;
                        GstState timeout_pending = GST_STATE_VOID_PENDING;
                        gst_element_get_state(graph, &timeout_state, &timeout_pending, 0);
                        LOG_WARN("CCTV", stream.stream_id + " 응답 없음 · " +
                                             std::to_string(server.config().stream.connect_timeout_s) +
                                             "초 타임아웃 · 상태 " +
                                             gst_element_state_get_name(timeout_state) +
                                             (timeout_pending != GST_STATE_VOID_PENDING
                                                  ? std::string("→") + gst_element_state_get_name(timeout_pending)
                                                  : std::string()) +
                                             " · 재연결 " + std::to_string(failures) +
                                             "/" + std::to_string(max_retries));
                        retry_needed = true;
                        break;
                    }
                    // 영상만 PLAYING 되고 application 패드가 안 붙으면 메타데이터가
                    // 영원히 안 온다. 실패로 세지 않고 세션만 다시 연다.
                    if (reached_playing && !logged_object_ && !logged_metadata_ &&
                        now - playing_at > std::chrono::seconds(8)) {
                        LOG_WARN("CCTV", stream.stream_id + " 메타데이터 없음 · 재연결");
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
                    LOG_WARN("CCTV", stream.stream_id + " 연결 끊김 · " + reason +
                                         " · 재연결 " + std::to_string(failures) + "/" +
                                         std::to_string(max_retries));
                    if (detail) g_error_free(detail);
                    if (debug) g_free(debug);
                    retry_needed = true;
                } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
                    LOG_INFO("CCTV", stream.stream_id + " 세션 만료 · 자동 재연결");
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
            if (sink) gst_object_unref(sink);
            gst_object_unref(bus);
            {
                std::lock_guard<std::mutex> lock(pipeline_mutex);
                if (pipeline == graph) pipeline = nullptr;
            }
            gst_element_set_state(graph, GST_STATE_NULL);
            gst_object_unref(graph);
            if (!retry_needed || stop_requested) break;
            if (failures >= max_retries) {
                LOG_ERROR("CCTV", stream.stream_id + " 제외 · 재연결 " +
                                     std::to_string(max_retries) + "회 실패");
                break;
            }
            retry();
        }
        g_main_context_pop_thread_default(ctx);
        g_main_context_unref(ctx);
    }

    void retry() {
        const int seconds = server.config().stream.retry_delay_s;
        for (int i = 0; i < seconds && running && !stop_requested; ++i)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
};

CentralServer::~CentralServer() { stop(); }

void CentralServer::start() {
    if (sensor_mode_ == forklift::runtime::SensorMode::Disabled) {
        LOG_WARN("SENSOR", "센서 제외 모드 · 판정에 센서 입력 미사용");
    } else {
        sensor_receiver_.start();
    }
    server_status_.start();
    assignment_publisher_.start();
    event_logger_.start();
    if (config_.output_storage.enable_latency_csv_logging) {
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
        if (config_.output_storage.enable_latency_csv_logging) {
            terminal->dispatcher.onLatencyEvent(
                [this](const LatencyStamps& stamps) { latency_logger_.log(stamps); });
        }
        JudgmentResult idle = risk_transport::ResultDispatcher::idleResult();
        idle.terminal_id = terminal->device.terminal_id;
        terminal->dispatcher.primeIdle(idle);
        terminal->dispatcher.start();
    }
    running_ = true;
    writeRuntimeStatus("online");
    process_thread_ = std::thread(&CentralServer::processLoop, this);
    status_thread_ = std::thread(&CentralServer::statusLoop, this);
}

void CentralServer::startWorkers() {
    for (const auto& stream : config_.streams)
        workers_.push_back(std::make_unique<StreamWorker>(*this, stream));
    for (std::size_t index = 0; index < workers_.size(); ++index) {
        workers_[index]->start();
        // 일부 멀티채널 RTSP 장비는 여러 SETUP 요청을 동시에 처리하지 못한다.
        // 첫 채널의 세션 협상이 시작된 뒤 다음 채널을 열어 초기 연결 충돌을 피한다.
        if (index + 1 < workers_.size())
            std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

void CentralServer::stop() {
    if (!running_.exchange(false)) return;
    for (auto& worker : workers_) worker->stop();
    events_.notifyAll();
    if (process_thread_.joinable()) process_thread_.join();
    if (status_thread_.joinable()) status_thread_.join();
    writeRuntimeStatus("offline");
    for (auto& terminal : terminals_) terminal->dispatcher.stop();
    for (auto& terminal : terminals_) terminal->publisher.stop();
    assignment_publisher_.stop();
    server_status_.stop();
    if (sensor_mode_ != forklift::runtime::SensorMode::Disabled) sensor_receiver_.stop();
    event_logger_.stop();
    if (config_.output_storage.enable_latency_csv_logging) {
        latency_logger_.stop();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    g_setenv("GIO_USE_PROXY_RESOLVER", "dummy", TRUE);
    gst_init(&argc, &argv);
    auto command_line = forklift::runtime::parseServerCommandLine(
        argc, argv, forklift::config::resolveConfigDirectory());
    if (!command_line.ok()) {
        std::cerr << command_line.error << "\n"
                  << forklift::runtime::serverCommandLineUsage(argv[0]);
        return 1;
    }
    if (command_line.options.show_help) {
        std::cout << forklift::runtime::serverCommandLineUsage(argv[0]);
        return 0;
    }

    auto options = std::move(command_line.options);
    std::string config_dir = options.config_dir;
    std::string common_config_dir = options.common_config_dir;
    if (common_config_dir.empty())
        common_config_dir = forklift::config::resolveCommonConfigDirectory(config_dir);

    forklift::logging::Logger::instance().holdUntilReady();
    forklift::config::SafetyServerConfig config;
    try {
        config = forklift::config::loadMultiCameraServerConfig(config_dir, common_config_dir);
    } catch (const forklift::config::SafetyServerConfigError& error) {
        forklift::logging::Logger::instance().releaseHold();
        LOG_ERROR("CONFIG", std::string("서버 기동 실패 · ") + error.what());
        return 2;
    }
    config.output_storage.enable_object_csv_logging =
        config.output_storage.enable_object_csv_logging || options.enable_object_csv;
    config.output_storage.enable_aruco_csv_logging =
        config.output_storage.enable_aruco_csv_logging || options.enable_aruco_csv;
    config.output_storage.enable_latency_csv_logging =
        config.output_storage.enable_latency_csv_logging || options.enable_latency_csv;
    forklift::logging::Logger::instance().setDebugEnabled(
        config.output_storage.enable_object_csv_logging ||
        config.output_storage.enable_aruco_csv_logging ||
        config.output_storage.enable_latency_csv_logging ||
        risk_transport::ResultDispatcher::sendLogEnabled());

    for (const auto* path : {&config.output_storage.object_csv, &config.output_storage.aruco_csv,
                             &config.output_storage.event_db, &config.output_storage.latency_csv,
                             &config.output_storage.runtime_status}) {
        const auto parent = std::filesystem::path(*path).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    CentralServer server(std::move(config), options.sensor_mode);
    static constexpr const char* kReadyRule =
        "====================================================================";
    forklift::logging::Logger::instance().announceReady(
        std::string(kReadyRule) + "\n서버 기동 완료 · CCTV " +
        std::to_string(server.config().streams.size()) + "개 · 단말 " +
        std::to_string(server.config().forklifts.size()) + "대 · 센서 " +
        sensorModeLogLabel(options.sensor_mode) + " · run_id=" +
        forklift::logging::Logger::instance().runId() + "\n" + kReadyRule);
    server.start();
    server.startWorkers();
    while (!stop_requested) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    LOG_INFO("SERVER", "서버 종료 신호 감지");
    server.stop();
    LOG_INFO("SERVER", "중앙 안전 서버 정상 종료");
    return 0;
}
