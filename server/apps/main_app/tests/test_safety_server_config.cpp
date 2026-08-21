#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>

#include "config_loader/safety_server_config.hpp"

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "실패: " << message << '\n'; ++failures; }
}

}

int main() {
    using namespace forklift::config;
    // 모델 파일이 채널 수를 결정하고, 서로 다른 CCTV의 같은 채널 번호도
    // 전역 stream_id로 분리되는지 확인한다.
    const auto multi_dir = std::filesystem::temp_directory_path() / "forklift_multi_config_test";
    std::filesystem::remove_all(multi_dir);
    std::filesystem::create_directories(multi_dir / "homography/CAM_01");
    std::filesystem::create_directories(multi_dir / "homography/CAM_02");
    const auto write = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream(path) << text;
    };
    const std::string h = R"({"world_unit":"mm","image_size":{"width":640,"height":480},"H_pixel_to_world":[[1,0,0],[0,1,0],[0,0,1]]})";
    write(multi_dir / "homography/CAM_01/homography_result_cam01_ch01_mm.json", h);
    write(multi_dir / "homography/CAM_02/homography_result_cam02_ch01_mm.json", h);
    write(multi_dir / "homography/CAM_02/homography_result_cam02_ch02_mm.json", h);
    write(multi_dir / "homography/CAM_02/homography_result_cam02_ch03_mm.json", h);
    write(multi_dir / "homography/CAM_02/homography_result_cam02_ch04_mm.json", h);
    write(multi_dir / "camera_model.json", R"({"models":[{"model":"PNO-A9081RG","channel_count":1},{"model":"PNM-C16083RVQ","channel_count":4}]})");
    write(multi_dir / "camera_list.json", R"({"cameras":[
      {"camera_id":"CAM_01","model":"PNO-A9081RG","channels":[{"channel":1,"rtsp_url":"rtsp://cam01","homography_file":"homography/CAM_01/homography_result_cam01_ch01_mm.json","image_width_px":640,"image_height_px":480}]},
      {"camera_id":"CAM_02","model":"PNM-C16083RVQ","channels":[{"channel":1,"rtsp_url":"rtsp://cam02","homography_file":"homography/CAM_02/homography_result_cam02_ch01_mm.json","image_width_px":640,"image_height_px":480},{"channel":2,"rtsp_url":"rtsp://cam02/2","homography_file":"homography/CAM_02/homography_result_cam02_ch02_mm.json","image_width_px":640,"image_height_px":480},{"channel":3,"rtsp_url":"rtsp://cam02/3","homography_file":"homography/CAM_02/homography_result_cam02_ch03_mm.json","image_width_px":640,"image_height_px":480},{"channel":4,"rtsp_url":"rtsp://cam02/4","homography_file":"homography/CAM_02/homography_result_cam02_ch04_mm.json","image_width_px":640,"image_height_px":480}]}
    ]})");
    write(multi_dir / "forklift_device_config.json", R"({"forklifts":[{"terminal_id":"TERM_01","marker_id":10,"collision_radius_mm":500},{"terminal_id":"TERM_02","marker_id":11,"collision_radius_mm":600}]})");
    write(multi_dir / "danger_judgment_config.json", R"({"units":{"world":"mm","distance":"mm"},"danger_judgment":{"caution_threshold_mm":3000,"danger_threshold_mm":1500,"emergency_threshold_mm":400,"emergency_release_margin_mm":100,"tof_caution_mm":1000,"tof_danger_mm":500,"impact_accel_threshold_g":2}})");
    write(multi_dir / "system_config.json", R"({"network":{"mqtt_host":"127.0.0.1","mqtt_port":1883,"result_heartbeat_ms":200,"tls_enabled":false},"handover":{"confirm_frames":2,"lost_grace_ms":500},"tracking":{"iou_threshold":0.3,"world_distance_threshold_mm":1000,"max_missed_frames":5},"sensor":{"stub_tof_distance_mm":5000,"stale_timeout_ms":1200},"stream":{"rtsp_latency_ms":100,"appsink_max_buffers":5,"eos_force_timeout_s":30,"connect_timeout_s":45,"max_retries":2,"retry_delay_s":1},"output_storage":{"server_log":"runtime/server.log","object_csv":"storage/objects.csv","aruco_csv":"storage/aruco.csv","event_db":"storage/events.db","latency_csv":"storage/latency.csv"}})");
    try {
        const auto multi = loadMultiCameraServerConfig(multi_dir.string());
        const auto endsWith = [](const std::string& value, const std::string& suffix) {
            return value.size() >= suffix.size() &&
                   value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        check(multi.streams.size() == 5, "모델별 채널 수에 맞춰 스트림을 읽음");
        check(multi.streams.size() >= 2 &&
                  multi.streams[0].stream_id == "CAM_01_CH_01" &&
                  multi.streams[1].stream_id == "CAM_02_CH_01",
              "서로 다른 CCTV의 같은 채널을 다른 stream_id로 분리함");
        check(multi.streams.size() == 5 &&
                  endsWith(multi.streams[1].homography_file, "homography_result_cam02_ch01_mm.json") &&
                  endsWith(multi.streams[2].homography_file, "homography_result_cam02_ch02_mm.json") &&
                  endsWith(multi.streams[3].homography_file, "homography_result_cam02_ch03_mm.json") &&
                  endsWith(multi.streams[4].homography_file, "homography_result_cam02_ch04_mm.json"),
              "CAM_02 각 채널이 서로 다른 호모그래피 파일을 사용함");
        check(multi.forklifts.size() == 2 &&
                  multi.forklifts[0].marker_id == 10 && multi.forklifts[1].marker_id == 11 &&
                  multi.forklifts[0].collision_radius_mm == 500 &&
                  multi.forklifts[1].collision_radius_mm == 600,
              "TERM별 marker와 충돌 반경을 목록으로 읽음");
        check(multi.output_storage.object_csv ==
                  (multi_dir.parent_path() / "var/main_app/storage/objects.csv").lexically_normal().string(),
              "런타임 출력은 config와 분리된 var/main_app에 저장함");
        check(multi.output_storage.server_log ==
                  (multi_dir.parent_path() / "var/main_app/runtime/server.log").lexically_normal().string(),
              "server.log를 event_db와 분리된 runtime 경로로 읽음");
        check(multi.output_storage.runtime_status ==
                  (multi_dir.parent_path() / "var/main_app/runtime/runtime-status.json").lexically_normal().string(),
              "runtime 상태 snapshot 경로에 안전한 기본값을 사용함");
        check(multi.stream.metadata_queue_capacity == 256,
              "기존 설정은 메타데이터 큐 상한 기본값을 사용함");
    } catch (const std::exception& error) {
        check(false, error.what());
    }

    // main 전용 정책 설정과 server/config 공통 카메라 설정을 서로 다른
    // 디렉터리에서 읽어도 같은 스트림 목록을 만드는지 확인한다.
    const auto split_root = std::filesystem::temp_directory_path() / "forklift_split_config_test";
    const auto split_app = split_root / "safety";
    const auto split_common = split_root / "common";
    std::filesystem::create_directories(split_app);
    std::filesystem::create_directories(split_common / "homography/CAM_01");
    std::filesystem::create_directories(split_common / "homography/CAM_02");
    for (const char* name : {"camera_model.json", "camera_list.json"})
        std::filesystem::copy_file(multi_dir / name, split_common / name,
                                   std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(multi_dir / "homography/CAM_01/homography_result_cam01_ch01_mm.json",
                               split_common / "homography/CAM_01/homography_result_cam01_ch01_mm.json",
                               std::filesystem::copy_options::overwrite_existing);
    for (const char* name : {"homography_result_cam02_ch01_mm.json",
                             "homography_result_cam02_ch02_mm.json",
                             "homography_result_cam02_ch03_mm.json",
                             "homography_result_cam02_ch04_mm.json"})
        std::filesystem::copy_file(multi_dir / "homography/CAM_02" / name,
                                   split_common / "homography/CAM_02" / name,
                                   std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(multi_dir / "forklift_device_config.json",
                               split_common / "forklift_device_config.json",
                               std::filesystem::copy_options::overwrite_existing);
    for (const char* name : {"danger_judgment_config.json", "system_config.json"})
        std::filesystem::copy_file(multi_dir / name, split_app / name,
                                   std::filesystem::copy_options::overwrite_existing);
    try {
        const auto split = loadMultiCameraServerConfig(split_app.string(), split_common.string());
        check(split.streams.size() == 5 && split.forklifts.size() == 2,
              "공통 camera·단말 설정과 main 전용 정책을 분리해 읽음");
    } catch (const std::exception& error) {
        check(false, error.what());
    }
    std::filesystem::remove_all(split_root);

    // 일부 채널의 H가 아직 없거나 손상돼도 중앙 서버 전체를 막지 않고,
    // 보정이 준비된 스트림만 남겨서 기동할 수 있어야 한다.
    std::filesystem::remove(multi_dir / "homography/CAM_02/homography_result_cam02_ch04_mm.json");
    try {
        const auto partial = loadMultiCameraServerConfig(multi_dir.string());
        check(partial.streams.size() == 4,
              "H가 없는 한 채널만 제외하고 사용 가능한 스트림은 유지함");
        const auto hasStream = [&partial](const std::string& stream_id) {
            for (const auto& stream : partial.streams)
                if (stream.stream_id == stream_id) return true;
            return false;
        };
        check(hasStream("CAM_01_CH_01") && hasStream("CAM_02_CH_01") &&
                  hasStream("CAM_02_CH_02") && hasStream("CAM_02_CH_03") &&
                  !hasStream("CAM_02_CH_04"),
              "CAM_02_CH_04만 누락되고 나머지 채널은 계속 사용함");
    } catch (const std::exception& error) {
        check(false, error.what());
    }

    // 모든 H가 사라지면 online 상태로 기동하지 않고 설정 오류로 실패해야 한다.
    std::filesystem::remove_all(multi_dir / "homography");
    try {
        (void)loadMultiCameraServerConfig(multi_dir.string());
        check(false, "활성 스트림이 0개인 설정을 거부함");
    } catch (const SafetyServerConfigError& error) {
        check(error.code() == SafetyServerConfigError::Code::SchemaInvalid,
              "활성 스트림 0개를 스키마 오류로 보고함");
    }
    std::filesystem::remove_all(multi_dir);
    return failures == 0 ? 0 : 1;
}
