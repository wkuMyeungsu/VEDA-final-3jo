#include "config/safety_server_config.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace forklift::config {
namespace {
using nlohmann::json;

[[noreturn]] void schema(const std::string& path, const std::string& detail) {
    throw SafetyServerConfigError(SafetyServerConfigError::Code::SchemaInvalid, path, detail);
}

const json& object(const json& parent, const char* key, const std::string& path) {
    auto it = parent.find(key);
    if (it == parent.end() || !it->is_object()) schema(path, std::string("\"") + key + "\" 항목은 객체여야 함");
    return *it;
}

template <typename T>
T value(const json& parent, const char* section, const char* key, const std::string& path) {
    auto it = parent.find(key);
    if (it == parent.end()) schema(path, std::string("필수 항목 누락: \"") + section + "." + key + "\"");
    try { return it->get<T>(); }
    catch (...) { schema(path, std::string("항목 형식 오류: \"") + section + "." + key + "\""); }
}

void positive(double v, const char* key, const std::string& path, bool zero_ok = false) {
    if (!std::isfinite(v) || v < 0.0 || (!zero_ok && v == 0.0))
        schema(path, std::string("\"") + key + "\" 값이 허용 범위를 벗어남");
}
}  // namespace

SafetyServerConfigError::SafetyServerConfigError(Code code, std::string path, const std::string& detail)
    : std::runtime_error("[safety_server_config] " + toString(code) + " (" + path + "): " + detail),
      code_(code), path_(std::move(path)) {}

std::string toString(SafetyServerConfigError::Code code) {
    switch (code) {
        case SafetyServerConfigError::Code::FileNotFound: return "FILE_NOT_FOUND";
        case SafetyServerConfigError::Code::ParseFailed: return "PARSE_FAILED";
        case SafetyServerConfigError::Code::SchemaInvalid: return "SCHEMA_INVALID";
    }
    return "UNKNOWN";
}

SafetyServerConfig loadSafetyServerConfig(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw SafetyServerConfigError(SafetyServerConfigError::Code::FileNotFound, path, "파일을 열 수 없음");
    json root;
    try { input >> root; }
    catch (const json::parse_error& e) { throw SafetyServerConfigError(SafetyServerConfigError::Code::ParseFailed, path, e.what()); }
    if (!root.is_object()) schema(path, "최상위 JSON은 객체여야 함");
    const auto& units = object(root, "units", path);
    if (value<std::string>(units, "units", "world", path) != "mm" ||
        value<std::string>(units, "units", "distance", path) != "mm") schema(path, "units.world와 units.distance는 모두 mm여야 함");

    SafetyServerConfig c;
    c.source_path = path;
    const auto& d = object(root, "danger_judgment", path);
    c.danger_judgment.caution_threshold_mm = value<double>(d, "danger_judgment", "caution_threshold_mm", path);
    c.danger_judgment.danger_threshold_mm = value<double>(d, "danger_judgment", "danger_threshold_mm", path);
    c.danger_judgment.emergency_threshold_mm = value<double>(d, "danger_judgment", "emergency_threshold_mm", path);
    c.danger_judgment.emergency_release_margin_mm = value<double>(d, "danger_judgment", "emergency_release_margin_mm", path);
    c.danger_judgment.tof_caution_mm = value<double>(d, "danger_judgment", "tof_caution_mm", path);
    c.danger_judgment.tof_danger_mm = value<double>(d, "danger_judgment", "tof_danger_mm", path);
    c.danger_judgment.impact_accel_threshold_g = value<double>(d, "danger_judgment", "impact_accel_threshold_g", path);
    c.danger_judgment.forklift_collision_radius_mm = value<double>(d, "danger_judgment", "forklift_collision_radius_mm", path);

    const auto& f = object(root, "forklift_detection", path);
    c.forklift_detection.marker_id = value<int>(f, "forklift_detection", "marker_id", path);
    const auto& h = object(root, "homography", path);
    c.homography.image_width_px = value<int>(h, "homography", "image_width_px", path);
    c.homography.image_height_px = value<int>(h, "homography", "image_height_px", path);
    const auto& files = object(h, "files", path);
    for (auto it = files.begin(); it != files.end(); ++it) {
        int channel = 0;
        try { channel = std::stoi(it.key()); } catch (...) { schema(path, "homography.files의 채널 키는 정수여야 함"); }
        if (channel < 1 || !it.value().is_string() || it.value().get<std::string>().empty()) schema(path, "homography.files 항목이 잘못됨");
        c.homography.files[channel] = it.value().get<std::string>();
    }
    if (c.homography.files.empty()) schema(path, "homography.files에는 카메라 채널이 하나 이상 있어야 함");

    const auto& hand = object(root, "handover", path);
    c.handover.confirm_frames = value<int>(hand, "handover", "confirm_frames", path);
    c.handover.lost_grace_ms = value<int>(hand, "handover", "lost_grace_ms", path);
    const auto& tracking = object(root, "tracking", path);
    c.tracking.iou_threshold = value<double>(tracking, "tracking", "iou_threshold", path);
    c.tracking.world_distance_threshold_mm =
        value<double>(tracking, "tracking", "world_distance_threshold_mm", path);
    c.tracking.max_missed_frames = value<int>(tracking, "tracking", "max_missed_frames", path);
    const auto& sensor = object(root, "sensor", path);
    c.sensor.stub_tof_distance_mm = value<double>(sensor, "sensor", "stub_tof_distance_mm", path);
    c.sensor.stale_timeout_ms = value<int>(sensor, "sensor", "stale_timeout_ms", path);
    const auto& network = object(root, "network", path);
    c.network.mqtt_host = value<std::string>(network, "network", "mqtt_host", path);
    const int mqtt_port = value<int>(network, "network", "mqtt_port", path);
    c.network.camera_assignment_bind_host = value<std::string>(network, "network", "camera_assignment_bind_host", path);
    const int camera_assignment_port = value<int>(network, "network", "camera_assignment_port", path);
    c.network.result_heartbeat_ms = value<int>(network, "network", "result_heartbeat_ms", path);
    if (network.contains("tls_enabled")) c.network.tls_enabled = network.at("tls_enabled").get<bool>();
    if (network.contains("ca_cert_path")) c.network.ca_cert_path = network.at("ca_cert_path").get<std::string>();
    if (network.contains("client_cert_path")) c.network.client_cert_path = network.at("client_cert_path").get<std::string>();
    if (network.contains("client_key_path")) c.network.client_key_path = network.at("client_key_path").get<std::string>();
    const auto& stream = object(root, "stream", path);
    c.stream.rtsp_latency_ms = value<int>(stream, "stream", "rtsp_latency_ms", path);
    c.stream.appsink_max_buffers = value<int>(stream, "stream", "appsink_max_buffers", path);
    c.stream.eos_force_timeout_s = value<int>(stream, "stream", "eos_force_timeout_s", path);
    c.stream.connect_timeout_s = value<int>(stream, "stream", "connect_timeout_s", path);
    c.stream.max_retries = value<int>(stream, "stream", "max_retries", path);
    c.stream.retry_delay_s = value<int>(stream, "stream", "retry_delay_s", path);

    positive(c.danger_judgment.caution_threshold_mm, "caution_threshold_mm", path);
    positive(c.danger_judgment.danger_threshold_mm, "danger_threshold_mm", path);
    positive(c.danger_judgment.emergency_threshold_mm, "emergency_threshold_mm", path);
    positive(c.danger_judgment.emergency_release_margin_mm, "emergency_release_margin_mm", path, true);
    positive(c.danger_judgment.tof_caution_mm, "tof_caution_mm", path);
    positive(c.danger_judgment.tof_danger_mm, "tof_danger_mm", path);
    positive(c.danger_judgment.impact_accel_threshold_g, "impact_accel_threshold_g", path);
    positive(c.danger_judgment.forklift_collision_radius_mm, "forklift_collision_radius_mm", path, true);
    positive(c.sensor.stub_tof_distance_mm, "stub_tof_distance_mm", path, true);
    positive(c.tracking.world_distance_threshold_mm, "world_distance_threshold_mm", path);
    if (!std::isfinite(c.tracking.iou_threshold) || c.tracking.iou_threshold < 0.0 ||
        c.tracking.iou_threshold > 1.0)
        schema(path, "tracking.iou_threshold는 0~1 범위여야 함");
    if (!(c.danger_judgment.emergency_threshold_mm < c.danger_judgment.danger_threshold_mm &&
          c.danger_judgment.danger_threshold_mm < c.danger_judgment.caution_threshold_mm)) schema(path, "거리 임계값은 emergency < danger < caution 순서여야 함");
    if (c.danger_judgment.tof_danger_mm > c.danger_judgment.tof_caution_mm)
        schema(path, "ToF 임계값은 danger <= caution 순서여야 함");
    if (mqtt_port < 1 || mqtt_port > 65535 || camera_assignment_port < 1 || camera_assignment_port > 65535)
        schema(path, "네트워크 포트는 1~65535 범위여야 함");
    c.network.mqtt_port = static_cast<uint16_t>(mqtt_port);
    c.network.camera_assignment_port = static_cast<uint16_t>(camera_assignment_port);
    if (c.network.mqtt_host.empty() || c.network.camera_assignment_bind_host.empty())
        schema(path, "네트워크 호스트는 빈 문자열일 수 없음");
    if (c.forklift_detection.marker_id < 0 || c.handover.confirm_frames < 1 || c.handover.lost_grace_ms < 0 ||
        c.sensor.stale_timeout_ms < 1 || c.tracking.max_missed_frames < 0 ||
        c.homography.image_width_px < 1 || c.homography.image_height_px < 1 ||
        c.network.result_heartbeat_ms < 1 ||
        c.stream.rtsp_latency_ms < 0 || c.stream.appsink_max_buffers < 1 || c.stream.eos_force_timeout_s < 1 ||
        c.stream.connect_timeout_s < 1 || c.stream.max_retries < 1 || c.stream.retry_delay_s < 0) schema(path, "정수 설정값이 허용 범위를 벗어남");
    return c;
}

std::string resolveSafetyServerConfigPath() {
    const char* candidates[] = {"config/safety_server_config.json", "../config/safety_server_config.json", "01_main/config/safety_server_config.json", "server/01_main/config/safety_server_config.json"};
    for (const char* candidate : candidates) if (std::filesystem::exists(candidate)) return candidate;
    return candidates[0];
}

std::string resolveConfigRelativePath(const SafetyServerConfig& config, const std::string& path) {
    const std::filesystem::path candidate(path);
    if (candidate.is_absolute()) return candidate.lexically_normal().string();
    return (std::filesystem::path(config.source_path).parent_path() / candidate).lexically_normal().string();
}

}  // namespace forklift::config
