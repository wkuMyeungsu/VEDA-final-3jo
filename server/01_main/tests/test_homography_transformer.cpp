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
    config.homography.image_width_px = 2592;
    config.homography.image_height_px = 1520;
    config.homography.files[1] = path;
    return config;
}

forklift::config::SafetyServerConfig streamConfigFor(const std::string& path) {
    auto config = configFor(path);
    config.homography.files.clear();
    config.homography.stream_files["CAM_02_CH_01"] = path;
    config.homography.stream_image_sizes["CAM_02_CH_01"] = {2592, 1520};
    return config;
}
}  // namespace

int main() {
    TempFile valid("test_homography_mm.json",
        R"({"world_unit":"mm","H_pixel_to_world":[[2,0,10],[0,3,20],[0,0,1]],"image_size":{"width":2592,"height":1520}})");
    forklift::logic::HomographyTransformer transformer(configFor(valid.path()));
    const auto point = transformer.pixelToWorld(1, {5.0, 7.0});
    check(point && point->x == 20.0 && point->y == 41.0, "정상 H로 좌표를 변환함");
    check(!transformer.pixelToWorld(2, {0.0, 0.0}), "설정되지 않은 채널을 거부함");
    check(!transformer.pixelToWorld(1, {std::numeric_limits<double>::infinity(), 0.0}),
          "무한대 입력을 거부함");

    TempFile wrongUnit("test_homography_cm.json",
        R"({"world_unit":"cm","H_pixel_to_world":[[1,0,0],[0,1,0],[0,0,1]],"image_size":{"width":2592,"height":1520}})");
    forklift::logic::HomographyTransformer unitRejected(configFor(wrongUnit.path()));
    check(!unitRejected.hasChannel(1) && !unitRejected.loadErrors().empty(), "mm가 아닌 H를 거부함");

    TempFile wrongSize("test_homography_size.json",
        R"({"world_unit":"mm","H_pixel_to_world":[[1,0,0],[0,1,0],[0,0,1]],"image_size":{"width":800,"height":600}})");
    forklift::logic::HomographyTransformer sizeRejected(configFor(wrongSize.path()));
    check(!sizeRejected.hasChannel(1), "보정 해상도가 다른 H를 거부함");

    TempFile singular("test_homography_denominator.json",
        R"({"world_unit":"mm","H_pixel_to_world":[[1,0,0],[0,1,0],[0,0,0]],"image_size":{"width":2592,"height":1520}})");
    forklift::logic::HomographyTransformer denominatorGuard(configFor(singular.path()));
    check(!denominatorGuard.pixelToWorld(1, {5.0, 7.0}), "동차좌표 분모가 0인 변환을 거부함");

    forklift::logic::HomographyTransformer streamTransformer(streamConfigFor(valid.path()));
    const auto streamPoint = streamTransformer.pixelToWorld("CAM_02_CH_01", {5.0, 7.0});
    check(streamPoint && streamPoint->x == 20.0 && streamPoint->y == 41.0,
          "stream_id별 H로 서로 다른 CCTV의 같은 채널을 변환함");
    check(!streamTransformer.pixelToWorld("CAM_01_CH_01", {5.0, 7.0}),
          "등록되지 않은 stream_id를 거부함");
    return failures == 0 ? 0 : 1;
}
