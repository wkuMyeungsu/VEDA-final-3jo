#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

#include "logic/homography/homography_transformer.hpp"

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "실패: " << message << '\n'; ++failures; }
}

class TempFile {
public:
    TempFile(std::string path, const std::string& content) : path_(std::move(path)) {
        std::ofstream(path_) << content;
    }
    ~TempFile() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

forklift::config::SafetyServerConfig configFor(const std::string& path) {
    forklift::config::SafetyServerConfig config;
    config.source_path = "test_config.json";
    config.homography.stream_files["CAM_02_CH_01"] = path;
    config.homography.stream_image_sizes["CAM_02_CH_01"] = {2592, 1520};
    return config;
}
}  // namespace

int main() {
    TempFile valid("test_homography_mm.json",
        R"({"map_unit":"mm","H_camera_pixels_to_shared_map":[[2,0,10],[0,3,20],[0,0,1]],"image_size":{"width":2592,"height":1520}})");
    forklift::logic::HomographyTransformer transformer(configFor(valid.path()));
    const auto point = transformer.pixelToWorld("CAM_02_CH_01", {5.0, 7.0});
    check(point && point->x == 20.0 && point->y == 41.0, "정상 H로 좌표를 변환함");
    check(!transformer.pixelToWorld("CAM_02_CH_02", {0.0, 0.0}), "설정되지 않은 stream_id를 거부함");
    check(!transformer.pixelToWorld("CAM_02_CH_01", {std::numeric_limits<double>::infinity(), 0.0}),
          "무한대 입력을 거부함");

    TempFile wrongUnit("test_homography_cm.json",
        R"({"map_unit":"cm","H_camera_pixels_to_shared_map":[[1,0,0],[0,1,0],[0,0,1]],"image_size":{"width":2592,"height":1520}})");
    forklift::logic::HomographyTransformer unitRejected(configFor(wrongUnit.path()));
    check(!unitRejected.hasStream("CAM_02_CH_01") && !unitRejected.streamLoadErrors().empty(), "mm가 아닌 H를 거부함");

    TempFile wrongSize("test_homography_size.json",
        R"({"map_unit":"mm","H_camera_pixels_to_shared_map":[[1,0,0],[0,1,0],[0,0,1]],"image_size":{"width":800,"height":600}})");
    forklift::logic::HomographyTransformer sizeRejected(configFor(wrongSize.path()));
    check(!sizeRejected.hasStream("CAM_02_CH_01"), "보정 해상도가 다른 H를 거부함");

    TempFile singular("test_homography_denominator.json",
        R"({"map_unit":"mm","H_camera_pixels_to_shared_map":[[1,0,0],[0,1,0],[0,0,0]],"image_size":{"width":2592,"height":1520}})");
    forklift::logic::HomographyTransformer denominatorGuard(configFor(singular.path()));
    check(!denominatorGuard.pixelToWorld("CAM_02_CH_01", {5.0, 7.0}), "동차좌표 분모가 0인 변환을 거부함");

    const auto streamPoint = transformer.pixelToWorld("CAM_02_CH_01", {5.0, 7.0});
    check(streamPoint && streamPoint->x == 20.0 && streamPoint->y == 41.0,
          "stream_id별 H로 좌표를 변환함");
    check(!transformer.pixelToWorld("CAM_01_CH_01", {5.0, 7.0}),
          "등록되지 않은 stream_id를 거부함");

    TempFile siteH("test_homography_site_frame.json",
        R"({"map_unit":"mm","H_camera_pixels_to_shared_map":[[1,0,0],[0,1,0],[0,0,1]],"image_size":{"width":2592,"height":1520}})");
    auto siteConfig = configFor(siteH.path());
    siteConfig.site_map.has_shared_to_site = true;
    siteConfig.site_map.h_shared_to_site = {1, 0, 10, 0, 1, 20, 0, 0, 1};
    forklift::logic::HomographyTransformer siteFrame(siteConfig);
    const auto shifted = siteFrame.pixelToWorld("CAM_02_CH_01", {5.0, 7.0});
    check(shifted && std::fabs(shifted->x - 15.0) < 1e-9 && std::fabs(shifted->y - 27.0) < 1e-9,
          "사이트 프레임 변환을 H에 곱해 월드 좌표를 맞춘다");

    const auto planar_with_height = transformer.pixelToWorld("CAM_02_CH_01", {5.0, 7.0}, 800.0);
    check(planar_with_height && planar_with_height->x == 20.0 && planar_with_height->y == 41.0,
          "K가 없으면 마커 높이가 있어도 지면 H로 폴백한다");

    // 카메라 (0,0,3000)에서 수직 하향, K=fx=fy=1000, cx=cy=500.
    // 지면 (100,200,0)은 (533.333, 433.333), 높이 800 마커는 (545.455, 409.091).
    const auto height_root =
        std::filesystem::temp_directory_path() / "forklift_homography_height_test";
    std::filesystem::remove_all(height_root);
    std::filesystem::create_directories(height_root / "CAM_02");
    const auto height_h = height_root / "CAM_02" / "homography_mm.json";
    const auto height_k = height_root / "camera_intrinsics_CAM_02_CH_01.json";
    {
        std::ofstream(height_h) << R"({
            "map_unit":"mm",
            "lens_undistorted":true,
            "image_size":{"width":2592,"height":1520},
            "H_camera_pixels_to_shared_map":[[3,0,-1500],[0,-3,1500],[0,0,1]]
        })";
        std::ofstream(height_k) << R"({
            "camera_matrix":[[1000,0,500],[0,1000,500],[0,0,1]],
            "dist_coeffs":[0,0,0,0,0]
        })";
    }
    auto heightConfig = configFor(height_h.string());
    forklift::logic::HomographyTransformer heightTf(heightConfig);
    const forklift::common::PixelPoint elevated_px{500.0 + 1000.0 * 100.0 / 2200.0,
                                                   500.0 - 1000.0 * 200.0 / 2200.0};
    const auto ground_hit = heightTf.pixelToWorld("CAM_02_CH_01", elevated_px);
    const auto corrected = heightTf.pixelToWorld("CAM_02_CH_01", elevated_px, 800.0);
    check(ground_hit && std::fabs(ground_hit->x - (100.0 * 3000.0 / 2200.0)) < 1e-6 &&
              std::fabs(ground_hit->y - (200.0 * 3000.0 / 2200.0)) < 1e-6,
          "높은 마커를 지면 H로 투영하면 카메라 바깥으로 밀린다");
    check(corrected && std::fabs(corrected->x - 100.0) < 1e-6 &&
              std::fabs(corrected->y - 200.0) < 1e-6,
          "마커 높이로 광선-평면을 자르면 연직 아래 지면 좌표를 복원한다");
    const auto zero_height = heightTf.pixelToWorld("CAM_02_CH_01", elevated_px, 0.0);
    check(zero_height && ground_hit && std::fabs(zero_height->x - ground_hit->x) < 1e-12 &&
              std::fabs(zero_height->y - ground_hit->y) < 1e-12,
          "높이 0은 기존 지면 호모그래피와 같다");

    heightConfig.site_map.has_shared_to_site = true;
    heightConfig.site_map.h_shared_to_site = {1, 0, 10, 0, 1, 20, 0, 0, 1};
    forklift::logic::HomographyTransformer heightSite(heightConfig);
    const auto site_corrected = heightSite.pixelToWorld("CAM_02_CH_01", elevated_px, 800.0);
    check(site_corrected && std::fabs(site_corrected->x - 110.0) < 1e-6 &&
              std::fabs(site_corrected->y - 220.0) < 1e-6,
          "높이 보정 좌표에 사이트 프레임 변환을 적용한다");
    std::filesystem::remove_all(height_root);
    return failures == 0 ? 0 : 1;
}
