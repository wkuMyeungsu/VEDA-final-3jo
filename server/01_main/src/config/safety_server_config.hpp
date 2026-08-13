#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>

namespace forklift::config {

// 이 헤더는 설정의 형태만 정의한다. 운영값은 기본값으로 둔갑하지 않고
// safety_server_config.json에서 모두 읽어야 하며, 누락된 항목은 로드 단계에서 실패한다.
struct DangerJudgmentConfig {
    double caution_threshold_mm{};
    double danger_threshold_mm{};
    double emergency_threshold_mm{};
    double emergency_release_margin_mm{};
    double tof_caution_mm{};
    double tof_danger_mm{};
    double impact_accel_threshold_g{};
    double forklift_collision_radius_mm{};
};

struct ForkliftDetectionConfig { int marker_id{}; };

struct HomographyConfig {
    // 키는 지게차 번호가 아니라 카메라 채널 식별자다.
    std::map<int, std::string> files;
    int image_width_px{};
    int image_height_px{};
};

struct HandoverConfig {
    int confirm_frames{};
    int lost_grace_ms{};
    std::chrono::milliseconds lostGrace() const { return std::chrono::milliseconds(lost_grace_ms); }
};

struct TrackingConfig {
    double iou_threshold{};
    double world_distance_threshold_mm{};
    int max_missed_frames{};
};

struct SensorConfig {
    double stub_tof_distance_mm{};
    int stale_timeout_ms{};
};

struct NetworkConfig {
    std::string mqtt_host;
    uint16_t mqtt_port{};
    std::string camera_assignment_bind_host;
    uint16_t camera_assignment_port{};
    int result_heartbeat_ms{};
    bool tls_enabled{};
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
};

struct StreamConfig {
    int rtsp_latency_ms{};
    int appsink_max_buffers{};
    int eos_force_timeout_s{};
    int connect_timeout_s{};
    int max_retries{};
    int retry_delay_s{};
};

struct SafetyServerConfig {
    DangerJudgmentConfig danger_judgment;
    ForkliftDetectionConfig forklift_detection;
    HomographyConfig homography;
    HandoverConfig handover;
    TrackingConfig tracking;
    SensorConfig sensor;
    NetworkConfig network;
    StreamConfig stream;
    std::string source_path;
};

class SafetyServerConfigError : public std::runtime_error {
public:
    enum class Code { FileNotFound, ParseFailed, SchemaInvalid };
    SafetyServerConfigError(Code code, std::string path, const std::string& detail);
    Code code() const noexcept { return code_; }
    const std::string& path() const noexcept { return path_; }
private:
    Code code_;
    std::string path_;
};

std::string toString(SafetyServerConfigError::Code code);
SafetyServerConfig loadSafetyServerConfig(const std::string& path);
std::string resolveSafetyServerConfigPath();
// 상대 H 경로는 실행 디렉터리가 아니라 공통 설정 파일의 디렉터리를 기준으로 한다.
std::string resolveConfigRelativePath(const SafetyServerConfig& config, const std::string& path);

}  // namespace forklift::config
