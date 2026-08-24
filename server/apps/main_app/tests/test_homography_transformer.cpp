#include <cstdio>
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
    return failures == 0 ? 0 : 1;
}
