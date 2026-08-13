#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "config/safety_server_config.hpp"

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "실패: " << message << '\n'; ++failures; }
}

class TempFile {
public:
    TempFile(const char* name, const std::string& content) : path_(name) { std::ofstream(path_) << content; }
    ~TempFile() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

std::string validJson() {
    return R"({
      "units":{"world":"mm","distance":"mm"},
      "danger_judgment":{"caution_threshold_mm":3000,"danger_threshold_mm":1500,"emergency_threshold_mm":400,"emergency_release_margin_mm":100,"tof_caution_mm":1000,"tof_danger_mm":500,"impact_accel_threshold_g":2,"forklift_collision_radius_mm":0},
      "forklift_detection":{"marker_id":0},
      "homography":{"files":{"1":"homography/h.json"},"image_width_px":2592,"image_height_px":1520},
      "handover":{"confirm_frames":3,"lost_grace_ms":500},
      "tracking":{"iou_threshold":0.3,"world_distance_threshold_mm":1000,"max_missed_frames":5},
      "sensor":{"stub_tof_distance_mm":5000,"stale_timeout_ms":1200},
      "network":{"mqtt_host":"127.0.0.1","mqtt_port":1883,"camera_assignment_bind_host":"0.0.0.0","camera_assignment_port":9001,"result_heartbeat_ms":200},
      "stream":{"rtsp_latency_ms":100,"appsink_max_buffers":5,"eos_force_timeout_s":30,"connect_timeout_s":45,"max_retries":5,"retry_delay_s":10}
    })";
}

void expectSchemaInvalid(const char* name, const std::string& json, const char* description) {
    TempFile file(name, json);
    try {
        forklift::config::loadSafetyServerConfig(file.path());
        check(false, description);
    } catch (const forklift::config::SafetyServerConfigError& error) {
        check(error.code() == forklift::config::SafetyServerConfigError::Code::SchemaInvalid,
              description);
    }
}
}

int main() {
    using namespace forklift::config;
    TempFile valid("test_safety_server_valid.json", validJson());
    const auto config = loadSafetyServerConfig(valid.path());
    check(config.forklift_detection.marker_id == 0, "지게차 마커 ID를 읽음");
    check(config.danger_judgment.danger_threshold_mm == 1500.0, "위험 거리 임계값을 mm로 읽음");
    check(config.homography.files.at(1) == "homography/h.json", "채널별 호모그래피 경로를 읽음");
    check(resolveConfigRelativePath(config, "homography/h.json") == "homography/h.json",
          "상대 경로는 설정 파일 위치를 기준으로 해석함");
    try { loadSafetyServerConfig("missing_safety_server_config.json"); check(false, "없는 파일을 거부함"); }
    catch (const SafetyServerConfigError& e) {
        check(e.code() == SafetyServerConfigError::Code::FileNotFound, "없는 파일 오류 코드를 구분함");
    }
    std::string bad = validJson();
    bad.replace(bad.find("\"mm\""), 4, "\"cm\"");
    expectSchemaInvalid("test_safety_server_invalid_unit.json", bad, "mm가 아닌 단위를 거부함");

    bad = validJson();
    const std::string required = "\"danger_threshold_mm\":1500,";
    bad.erase(bad.find(required), required.size());
    expectSchemaInvalid("test_safety_server_missing_value.json", bad, "필수 판정값 누락을 거부함");

    bad = validJson();
    bad.replace(bad.find("\"danger_threshold_mm\":1500"), 26,
                "\"danger_threshold_mm\":3500");
    expectSchemaInvalid("test_safety_server_threshold_order.json", bad,
                        "위험 거리 임계값 순서 오류를 거부함");

    bad = validJson();
    bad.replace(bad.find("\"marker_id\":0"), 13, "\"marker_id\":-1");
    expectSchemaInvalid("test_safety_server_marker_id.json", bad, "음수 마커 ID를 거부함");
    return failures == 0 ? 0 : 1;
}
