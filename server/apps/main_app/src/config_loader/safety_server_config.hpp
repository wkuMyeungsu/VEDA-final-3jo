#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <vector>
#include <stdexcept>
#include <string>
#include <utility>

namespace forklift::config {

// 설정 파일에서 읽은 값을 서버 전체가 공유하는 자료 구조다.
// 운영값이 빠졌을 때 임의의 기본값으로 계속 실행하지 않고 기동 단계에서 실패한다.
struct DangerJudgmentConfig {
    double caution_threshold_mm{};
    double danger_threshold_mm{};
    double emergency_threshold_mm{};
    double emergency_release_margin_mm{};
    double tof_caution_mm{};
    double tof_danger_mm{};
    double impact_accel_threshold_g{};
};

struct HomographyConfig {
    // 모든 카메라의 모든 채널은 전역 stream_id로 식별한다.
    std::map<std::string, std::string> stream_files;
    std::map<std::string, std::pair<int, int>> stream_image_sizes;
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

struct CameraStreamConfig {
    std::string stream_id;          // 서버가 입력 스트림을 구분하는 키
    std::string camera_id;          // 물리 CCTV 장비 이름
    std::string camera_model;       // camera_model.json의 모델 이름
    int camera_channel_count{};     // 모델이 허용하는 전체 채널 수
    int channel{};                  // 해당 CCTV 안의 채널 번호
    std::string rtsp_url;           // 이 채널의 메타데이터 RTSP 주소
    std::string homography_file;    // 픽셀을 mm 월드 좌표로 바꾸는 H 파일
    int image_width_px{};           // H 보정 당시 영상 너비
    int image_height_px{};          // H 보정 당시 영상 높이
};

struct ForkliftDevice {
    std::string terminal_id;        // 위험 결과를 받을 운전자 단말
    int marker_id{};                // 이 지게차를 식별하는 ArUco ID
    double collision_radius_mm{};   // 지게차 외곽을 고려해 거리에서 뺄 반경
};

struct OutputStorageConfig {
    bool enable_raw_csv_logging = false; // 객체/ArUco 원시 CSV 로깅 활성화 여부 (디버깅 전용, 기본 false)
    std::string object_csv;         // 객체 메타데이터 CSV
    std::string aruco_csv;          // ArUco 메타데이터 CSV
    std::string event_db;           // 위험 상태 변화 SQLite
    std::string latency_csv;        // 서버 처리 지연 CSV
};

struct SafetyServerConfig {
    DangerJudgmentConfig danger_judgment;
    HomographyConfig homography;
    HandoverConfig handover;
    TrackingConfig tracking;
    SensorConfig sensor;
    NetworkConfig network;
    StreamConfig stream;
    std::string source_path;

    // 카메라 N대 × 스트림 M채널 × 단말 K대 운영 모델의 유일한 입력 목록이다.
    std::vector<CameraStreamConfig> streams;
    std::vector<ForkliftDevice> forklifts;
    OutputStorageConfig output_storage;
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

// main 전용 정책 설정과 server/config 공통 카메라 설정을 읽고,
// 서로 연결되는 값까지 한 번에 검증한다.
SafetyServerConfig loadMultiCameraServerConfig(
    const std::string& config_dir, const std::string& common_config_dir = {});
std::string resolveConfigDirectory();
std::string resolveCommonConfigDirectory(const std::string& config_dir);

}  // namespace forklift::config
