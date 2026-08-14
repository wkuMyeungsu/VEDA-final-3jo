#include <cstdio>
#include <fstream>
#include <filesystem>
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
      "network":{"mqtt_host":"127.0.0.1","mqtt_port":1883,"result_heartbeat_ms":200},
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

    // 모델 파일이 채널 수를 결정하고, 서로 다른 CCTV의 같은 채널 번호도
    // 전역 stream_id로 분리되는지 확인한다.
    const auto multi_dir = std::filesystem::temp_directory_path() / "forklift_multi_config_test";
    std::filesystem::remove_all(multi_dir);
    std::filesystem::create_directories(multi_dir / "homography");
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream(path) << text;
    };
    const std::string h = R"({"world_unit":"mm","image_size":{"width":640,"height":480},"H_pixel_to_world":[[1,0,0],[0,1,0],[0,0,1]]})";
    write(multi_dir / "homography/cam01.json", h);
    write(multi_dir / "homography/cam02_1.json", h);
    write(multi_dir / "camera_model.json", R"({"models":[{"model":"PNO-A9081RG","channel_count":1},{"model":"PNM-C16083RVQ","channel_count":4}]})");
    write(multi_dir / "camera_list.json", R"({"cameras":[
      {"camera_id":"CAM_01","model":"PNO-A9081RG","channels":[{"channel":1,"rtsp_url":"rtsp://cam01","homography_file":"homography/cam01.json","image_width_px":640,"image_height_px":480}]},
      {"camera_id":"CAM_02","model":"PNM-C16083RVQ","channels":[{"channel":1,"rtsp_url":"rtsp://cam02","homography_file":"homography/cam02_1.json","image_width_px":640,"image_height_px":480},{"channel":2,"rtsp_url":"rtsp://cam02/2","homography_file":"homography/cam02_1.json","image_width_px":640,"image_height_px":480},{"channel":3,"rtsp_url":"rtsp://cam02/3","homography_file":"homography/cam02_1.json","image_width_px":640,"image_height_px":480},{"channel":4,"rtsp_url":"rtsp://cam02/4","homography_file":"homography/cam02_1.json","image_width_px":640,"image_height_px":480}]}
    ]})");
    write(multi_dir / "forklift_device_config.json", R"({"forklifts":[{"terminal_id":"TERM_01","marker_id":10,"collision_radius_mm":500},{"terminal_id":"TERM_02","marker_id":11,"collision_radius_mm":600}]})");
    write(multi_dir / "danger_judgment_config.json", R"({"units":{"world":"mm","distance":"mm"},"danger_judgment":{"caution_threshold_mm":3000,"danger_threshold_mm":1500,"emergency_threshold_mm":400,"emergency_release_margin_mm":100,"tof_caution_mm":1000,"tof_danger_mm":500,"impact_accel_threshold_g":2}})");
    write(multi_dir / "system_config.json", R"({"network":{"mqtt_host":"127.0.0.1","mqtt_port":1883,"result_heartbeat_ms":200,"tls_enabled":false},"handover":{"confirm_frames":2,"lost_grace_ms":500},"tracking":{"iou_threshold":0.3,"world_distance_threshold_mm":1000,"max_missed_frames":5},"sensor":{"stub_tof_distance_mm":5000,"stale_timeout_ms":1200},"stream":{"rtsp_latency_ms":100,"appsink_max_buffers":5,"eos_force_timeout_s":30,"connect_timeout_s":45,"max_retries":2,"retry_delay_s":1},"output_storage":{"object_csv":"storage/objects.csv","aruco_csv":"storage/aruco.csv","event_db":"storage/events.db","latency_csv":"storage/latency.csv"}})");
    try {
        const auto multi = loadMultiCameraServerConfig(multi_dir.string());
        check(multi.streams.size() == 5, "모델별 채널 수에 맞춰 스트림을 읽음");
        check(multi.streams[0].stream_id == "CAM_01_CH_01" && multi.streams[1].stream_id == "CAM_02_CH_01",
              "서로 다른 CCTV의 같은 채널을 다른 stream_id로 분리함");
        check(multi.forklifts.size() == 2 && multi.forklifts[1].collision_radius_mm == 600,
              "TERM별 marker와 충돌 반경을 읽음");
    } catch (const std::exception& error) {
        check(false, error.what());
    }

    // main 전용 정책 설정과 server/config 공통 카메라 설정을 서로 다른
    // 디렉터리에서 읽어도 같은 스트림 목록을 만드는지 확인한다.
    const auto split_root = std::filesystem::temp_directory_path() / "forklift_split_config_test";
    const auto split_app = split_root / "01_main";
    const auto split_common = split_root / "common";
    std::filesystem::create_directories(split_app);
    std::filesystem::create_directories(split_common / "homography");
    for (const char* name : {"camera_model.json", "camera_list.json"})
        std::filesystem::copy_file(multi_dir / name, split_common / name,
                                   std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(multi_dir / "homography/cam01.json",
                               split_common / "homography/cam01.json",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(multi_dir / "homography/cam02_1.json",
                               split_common / "homography/cam02_1.json",
                               std::filesystem::copy_options::overwrite_existing);
    for (const char* name : {"forklift_device_config.json", "danger_judgment_config.json", "system_config.json"})
        std::filesystem::copy_file(multi_dir / name, split_app / name,
                                   std::filesystem::copy_options::overwrite_existing);
    try {
        const auto split = loadMultiCameraServerConfig(split_app.string(), split_common.string());
        check(split.streams.size() == 5, "공통 camera_list와 main 전용 설정을 분리해 읽음");
    } catch (const std::exception& error) {
        check(false, error.what());
    }
    std::filesystem::remove_all(split_root);

    // 일부 채널의 H가 아직 없거나 손상돼도 중앙 서버 전체를 막지 않고,
    // 보정이 준비된 스트림만 남겨서 기동할 수 있어야 한다.
    std::filesystem::remove(multi_dir / "homography/cam02_1.json");
    try {
        const auto partial = loadMultiCameraServerConfig(multi_dir.string());
        check(partial.streams.size() == 1,
              "H가 없는 카메라 채널은 제외하고 사용 가능한 스트림만 유지함");
        check(partial.streams.front().stream_id == "CAM_01_CH_01",
              "사용 가능한 다른 카메라 스트림은 계속 유지함");
    } catch (const std::exception& error) {
        check(false, error.what());
    }
    std::filesystem::remove_all(multi_dir);
    return failures == 0 ? 0 : 1;
}
