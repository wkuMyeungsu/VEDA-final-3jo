#include "logic/homography/homography_transformer.hpp"

#include <cmath>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace forklift::logic {
namespace {
using nlohmann::json;

HomographyTransformer::ChannelHomography loadOne(const std::string& path, int expected_width, int expected_height) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("파일을 찾을 수 없음");
    json root;
    input >> root;
    if (root.at("world_unit").get<std::string>() != "mm") throw std::runtime_error("world_unit은 mm여야 함");
    const auto& size = root.at("image_size");
    const int width = size.at("width").get<int>();
    const int height = size.at("height").get<int>();
    if (width != expected_width || height != expected_height) throw std::runtime_error("보정 해상도와 설정 해상도가 다름");
    const auto& matrix = root.at("H_pixel_to_world");
    if (!matrix.is_array() || matrix.size() != 3) throw std::runtime_error("H_pixel_to_world는 3x3 행렬이어야 함");
    HomographyTransformer::ChannelHomography result;
    result.image_width_px = width;
    result.image_height_px = height;
    for (int row = 0; row < 3; ++row) {
        if (!matrix.at(row).is_array() || matrix.at(row).size() != 3) throw std::runtime_error("H_pixel_to_world는 3x3 행렬이어야 함");
        for (int col = 0; col < 3; ++col) {
            const double v = matrix.at(row).at(col).get<double>();
            if (!std::isfinite(v)) throw std::runtime_error("H_pixel_to_world에 NaN 또는 Inf가 포함됨");
            result.h[row * 3 + col] = v;
        }
    }
    return result;
}
}  // namespace

HomographyTransformer::HomographyTransformer(const config::SafetyServerConfig& config) {
    for (const auto& [channel, configured_path] : config.homography.files) {
        const std::string path = config::resolveConfigRelativePath(config, configured_path);
        try { channels_.emplace(channel, loadOne(path, config.homography.image_width_px, config.homography.image_height_px)); }
        catch (const std::exception& e) { load_errors_[channel] = path + ": " + e.what(); }
    }
}

bool HomographyTransformer::hasChannel(int channel) const { return channels_.count(channel) != 0; }

std::optional<common::WorldPoint> HomographyTransformer::pixelToWorld(int channel, const common::PixelPoint& pixel) const {
    const auto it = channels_.find(channel);
    if (it == channels_.end() || !std::isfinite(pixel.x) || !std::isfinite(pixel.y)) return std::nullopt;
    const auto& h = it->second.h;
    const double denominator = h[6] * pixel.x + h[7] * pixel.y + h[8];
    if (!std::isfinite(denominator) || std::abs(denominator) < 1e-12) return std::nullopt;
    const double x = (h[0] * pixel.x + h[1] * pixel.y + h[2]) / denominator;
    const double y = (h[3] * pixel.x + h[4] * pixel.y + h[5]) / denominator;
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return common::WorldPoint{x, y};
}

}  // namespace forklift::logic
