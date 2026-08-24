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
  solve-manual --config CONFIG --input IMAGE --layout JSON --output JSON
               [--overlay IMAGE]
  align-markers --config CONFIG --source IMAGE --destination IMAGE
                --output JSON
  calibrate-intrinsics --config CONFIG --images DIR --output JSON
)";
}

std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback) {
    for (int i = 2; i < argc; ++i)
        if (argv[i] == name && i + 1 < argc) return argv[i + 1];
    return fallback;
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
    const auto& marker_output = value.at("marker_output");
    config.marker_output.size_mm = marker_output.at("size_mm").get<double>();
    config.marker_output.margin_mm = marker_output.at("margin_mm").get<double>();
    config.marker_output.dpi = marker_output.at("dpi").get<double>();
    config.marker_output.label = marker_output.at("label").get<std::string>();
    const auto& manual_solve = value.at("manual_solve");
    config.manual_solve.marker_size_mm = manual_solve.at("marker_size_mm").get<double>();
    if (config.marker_output.size_mm <= 0 ||
        config.marker_output.margin_mm < 0 || config.marker_output.dpi <= 0 ||
        config.manual_solve.marker_size_mm <= 0)
        throw std::runtime_error("invalid marker output or manual solve settings");
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

}  // namespace homography
