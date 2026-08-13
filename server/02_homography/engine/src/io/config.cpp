#include "homography/cli.hpp"
#include "homography/config.hpp"

#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace homography {

using json = nlohmann::json;

void print_usage() {
    std::cout << R"(homography_tool <command> [options]
commands:
  detect-markers --config CONFIG --input IMAGE --output JSON [--overlay IMAGE]
  gen-marker --config CONFIG --id ID --output FILE
             [--size-mm MM] [--margin-mm MM] [--label TEXT] [--dpi DPI]
  calibrate  --config CONFIG --input IMAGE --output JSON [--channel N]
             [--max-rmse-mm MM]
  solve-manual --config CONFIG --input IMAGE --layout JSON --output JSON
               [--overlay IMAGE]
  view       --config CONFIG --homography JSON --input IMAGE
             [--output-dir DIR] [--live]
)";
}

std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback) {
    for (int i = 2; i < argc; ++i)
        if (argv[i] == name && i + 1 < argc) return argv[i + 1];
    return fallback;
}

bool has_argument(int argc, char** argv, const std::string& name) {
    for (int i = 2; i < argc; ++i) if (argv[i] == name) return true;
    return false;
}

int parse_int(const std::string& value, const char* label) {
    try { return std::stoi(value); }
    catch (...) { throw std::runtime_error(std::string("invalid ") + label); }
}

double parse_double(const std::string& value, const char* label) {
    try { return std::stod(value); }
    catch (...) { throw std::runtime_error(std::string("invalid ") + label); }
}

Config read_config(const std::string& path) {
    // JSON 설정 파일을 읽고 필수 항목과 값의 범위를 검증함.
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open config: " + path);
    const json value = json::parse(input);
    Config config;
    config.dictionary = value.at("dictionary").get<std::string>();
    config.cols = value.at("cols").get<int>();
    config.rows = value.at("rows").get<int>();
    config.marker_len_mm = value.at("marker_len_mm").get<double>();
    config.gap_mm = value.at("gap_mm").get<double>();
    config.id_offset = value.at("id_offset").get<int>();
    config.origin_corner = value.at("origin_corner").get<std::string>();
    const auto& marker_output = value.at("marker_output");
    config.marker_output.size_mm = marker_output.at("size_mm").get<double>();
    config.marker_output.margin_mm = marker_output.at("margin_mm").get<double>();
    config.marker_output.dpi = marker_output.at("dpi").get<double>();
    config.marker_output.label = marker_output.at("label").get<std::string>();
    const auto& calibration = value.at("calibration");
    config.calibration.max_rmse_mm = calibration.at("max_rmse_mm").get<double>();
    config.calibration.ransac_threshold_mm = calibration.at("ransac_threshold_mm").get<double>();
    config.calibration.channel = calibration.at("channel").get<int>();
    const auto& manual_solve = value.at("manual_solve");
    config.manual_solve.marker_size_mm = manual_solve.at("marker_size_mm").get<double>();
    config.manual_solve.ransac_threshold_mm = manual_solve.at("ransac_threshold_mm").get<double>();
    const auto& preview = value.at("preview");
    config.preview.scale = preview.at("scale").get<int>();
    config.preview.good_error_mm = preview.at("good_error_mm").get<double>();
    config.preview.warning_error_mm = preview.at("warning_error_mm").get<double>();
    if (config.cols <= 0 || config.rows <= 0 || config.marker_len_mm <= 0 ||
        config.gap_mm < 0 || config.marker_output.size_mm <= 0 ||
        config.marker_output.margin_mm < 0 || config.marker_output.dpi <= 0 ||
        config.calibration.max_rmse_mm <= 0 ||
        config.calibration.ransac_threshold_mm <= 0 ||
        config.manual_solve.marker_size_mm <= 0 ||
        config.manual_solve.ransac_threshold_mm <= 0 || config.preview.scale <= 0 ||
        config.preview.good_error_mm < 0 || config.preview.warning_error_mm < 0)
        throw std::runtime_error("invalid grid dimensions or lengths");
    if (config.origin_corner != "TL")
        throw std::runtime_error("only origin_corner=TL is supported");
    return config;
}

cv::Ptr<cv::aruco::Dictionary> dictionary(const Config& config) {
    static const std::map<std::string, cv::aruco::PREDEFINED_DICTIONARY_NAME> names = {
        {"DICT_4X4_50", cv::aruco::DICT_4X4_50},
        {"DICT_4X4_100", cv::aruco::DICT_4X4_100},
        {"DICT_5X5_50", cv::aruco::DICT_5X5_50},
        {"DICT_6X6_50", cv::aruco::DICT_6X6_50},
        {"DICT_7X7_50", cv::aruco::DICT_7X7_50},
    };
    const auto it = names.find(config.dictionary);
    if (it == names.end())
        throw std::runtime_error("unsupported dictionary: " + config.dictionary);
    return cv::aruco::getPredefinedDictionary(it->second);
}

std::vector<cv::Point2f> world_corners(const Config& config, int id) {
    const int index = id - config.id_offset;
    if (index < 0 || index >= config.rows * config.cols) return {};
    const int row = index / config.cols;
    const int col = index % config.cols;
    // ArUco ID는 행 우선(row-major) 배치함. 원점은 좌상단(TL).
    const double pitch = config.marker_len_mm + config.gap_mm;
    const double x = col * pitch;
    const double y = row * pitch;
    const double length = config.marker_len_mm;
    return {{static_cast<float>(x), static_cast<float>(y)},
            {static_cast<float>(x + length), static_cast<float>(y)},
            {static_cast<float>(x + length), static_cast<float>(y + length)},
            {static_cast<float>(x), static_cast<float>(y + length)}};
}

double grid_width_mm(const Config& config) {
    return config.cols * config.marker_len_mm +
           (config.cols - 1) * config.gap_mm;
}

double grid_height_mm(const Config& config) {
    return config.rows * config.marker_len_mm +
           (config.rows - 1) * config.gap_mm;
}

}  // namespace homography
