#include "logic/homography/homography_transformer.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace forklift::logic {
namespace {
using nlohmann::json;

std::pair<std::string, bool> findIntrinsicsFile(const std::string& stream_id,
                                                const std::filesystem::path& homography_root) {
    const std::string per_stream =
        (homography_root / ("camera_intrinsics_" + stream_id + ".json")).string();
    if (std::filesystem::exists(per_stream)) return {per_stream, true};
    const std::string common = (homography_root / "camera_intrinsics.json").string();
    if (std::filesystem::exists(common)) return {common, false};
    return {};
}

bool loadIntrinsics(const std::string& path, HomographyTransformer::StreamHomography::Intrinsics& out) {
    std::ifstream input(path);
    if (!input) return false;
    try {
        const json value = json::parse(input);
        out.fx = value.at("camera_matrix").at(0).at(0).get<double>();
        out.fy = value.at("camera_matrix").at(1).at(1).get<double>();
        out.cx = value.at("camera_matrix").at(0).at(2).get<double>();
        out.cy = value.at("camera_matrix").at(1).at(2).get<double>();
        const auto& coefficients = value.at("dist_coeffs");
        out.k1 = coefficients.at(0).get<double>();
        out.k2 = coefficients.at(1).get<double>();
        out.p1 = coefficients.size() > 2 ? coefficients.at(2).get<double>() : 0.0;
        out.p2 = coefficients.size() > 3 ? coefficients.at(3).get<double>() : 0.0;
        out.k3 = coefficients.size() > 4 ? coefficients.at(4).get<double>() : 0.0;
        out.valid = true;
    } catch (const std::exception&) {
        out.valid = false;
    }
    return out.valid;
}

HomographyTransformer::StreamHomography loadOne(const std::string& path,
                                                int expected_width, int expected_height,
                                                const std::filesystem::path& homography_root,
                                                const std::string& stream_id) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("파일을 찾을 수 없음");
    json root;
    input >> root;
    if (root.at("map_unit").get<std::string>() != "mm") throw std::runtime_error("map_unit은 mm여야 함");
    const auto& size = root.at("image_size");
    const int width = size.at("width").get<int>();
    const int height = size.at("height").get<int>();
    if (width != expected_width || height != expected_height) throw std::runtime_error("보정 해상도와 설정 해상도가 다름");
    const auto& matrix = root.at("H_camera_pixels_to_shared_map");
    if (!matrix.is_array() || matrix.size() != 3) throw std::runtime_error("H_camera_pixels_to_shared_map는 3x3 행렬이어야 함");
    HomographyTransformer::StreamHomography result;
    result.image_width_px = width;
    result.image_height_px = height;
    result.lens_undistorted = root.value("lens_undistorted", false);
    if (result.lens_undistorted) {
        // 스트림 전용 산출물을 우선하고 공용 파일로 폴백한다.
        const auto [intrinsics_path, is_per_stream] = findIntrinsicsFile(stream_id, homography_root);
        result.intrinsics.valid = !intrinsics_path.empty() && loadIntrinsics(intrinsics_path, result.intrinsics);
    }
    for (int row = 0; row < 3; ++row) {
        if (!matrix.at(row).is_array() || matrix.at(row).size() != 3) throw std::runtime_error("H_camera_pixels_to_shared_map는 3x3 행렬이어야 함");
        for (int col = 0; col < 3; ++col) {
            const double v = matrix.at(row).at(col).get<double>();
            if (!std::isfinite(v)) throw std::runtime_error("H_camera_pixels_to_shared_map에 NaN 또는 Inf가 포함됨");
            result.h[row * 3 + col] = v;
        }
    }
    const auto& h = result.h;
    const double determinant =
        h[0] * (h[4] * h[8] - h[5] * h[7]) -
        h[1] * (h[3] * h[8] - h[5] * h[6]) +
        h[2] * (h[3] * h[7] - h[4] * h[6]);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        throw std::runtime_error("H 역행렬을 계산할 수 없음");
    result.h_inv = {(h[4] * h[8] - h[5] * h[7]) / determinant,
                    (h[2] * h[7] - h[1] * h[8]) / determinant,
                    (h[1] * h[5] - h[2] * h[4]) / determinant,
                    (h[5] * h[6] - h[3] * h[8]) / determinant,
                    (h[0] * h[8] - h[2] * h[6]) / determinant,
                    (h[2] * h[3] - h[0] * h[5]) / determinant,
                    (h[3] * h[7] - h[4] * h[6]) / determinant,
                    (h[1] * h[6] - h[0] * h[7]) / determinant,
                    (h[0] * h[4] - h[1] * h[3]) / determinant};
    return result;
}
}  // namespace

HomographyTransformer::HomographyTransformer(const config::SafetyServerConfig& config) {
    for (const auto& [stream_id, configured_path] : config.homography.stream_files) {
        const auto homography_root =
            std::filesystem::path(configured_path).parent_path().parent_path();
        const auto size_it = config.homography.stream_image_sizes.find(stream_id);
        if (size_it == config.homography.stream_image_sizes.end()) {
            stream_load_errors_[stream_id] = "이미지 해상도 설정 누락";
            continue;
        }
        try {
            streams_.emplace(stream_id, loadOne(configured_path, size_it->second.first,
                                                size_it->second.second, homography_root,
                                                stream_id));
        } catch (const std::exception& e) {
            stream_load_errors_[stream_id] = configured_path + ": " + e.what();
        }
    }
}

bool HomographyTransformer::hasStream(const std::string& stream_id) const {
    return streams_.count(stream_id) != 0;
}

std::optional<common::WorldPoint> HomographyTransformer::pixelToWorld(
    const std::string& stream_id, const common::PixelPoint& pixel) const {
    const auto it = streams_.find(stream_id);
    if (it == streams_.end() || !std::isfinite(pixel.x) || !std::isfinite(pixel.y)) return std::nullopt;
    double px = pixel.x, py = pixel.y;
    // 무왜곡 계약 H: 렌즈 왜곡 역산(뉴턴 반복) 후 변환한다. 플래그 없는 구버전 H는 통과.
    const auto& intrinsics = it->second.intrinsics;
    if (it->second.lens_undistorted && intrinsics.valid) {
        double xn = (px - intrinsics.cx) / intrinsics.fx;
        double yn = (py - intrinsics.cy) / intrinsics.fy;
        for (int iteration = 0; iteration < 10; ++iteration) {
            const double r2 = xn * xn + yn * yn;
            const double radial = 1.0 + intrinsics.k1 * r2 + intrinsics.k2 * r2 * r2 +
                                  intrinsics.k3 * r2 * r2 * r2;
            const double dx = 2.0 * intrinsics.p1 * xn * yn +
                              intrinsics.p2 * (r2 + 2.0 * xn * xn);
            const double dy = intrinsics.p1 * (r2 + 2.0 * yn * yn) +
                              2.0 * intrinsics.p2 * xn * yn;
            xn = (xn - dx) / radial;
            yn = (yn - dy) / radial;
        }
        px = xn * intrinsics.fx + intrinsics.cx;
        py = yn * intrinsics.fy + intrinsics.cy;
    }
    const auto& h = it->second.h;
    const double denominator = h[6] * px + h[7] * py + h[8];
    if (!std::isfinite(denominator) || std::abs(denominator) < 1e-12) return std::nullopt;
    const double x = (h[0] * px + h[1] * py + h[2]) / denominator;
    const double y = (h[3] * px + h[4] * py + h[5]) / denominator;
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return common::WorldPoint{x, y};
}

std::optional<common::PixelPoint> HomographyTransformer::worldToPixel(
    const std::string& stream_id, const common::WorldPoint& world) const {
    const auto it = streams_.find(stream_id);
    if (it == streams_.end()) return std::nullopt;
    const auto& hi = it->second.h_inv;
    const double denominator = hi[6] * world.x + hi[7] * world.y + hi[8];
    if (!std::isfinite(denominator) || std::abs(denominator) < 1e-12) return std::nullopt;
    const double x = (hi[0] * world.x + hi[1] * world.y + hi[2]) / denominator;
    const double y = (hi[3] * world.x + hi[4] * world.y + hi[5]) / denominator;
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return common::PixelPoint{static_cast<float>(x), static_cast<float>(y)};
}

std::optional<std::pair<int, int>> HomographyTransformer::imageSize(
    const std::string& stream_id) const {
    const auto it = streams_.find(stream_id);
    if (it == streams_.end()) return std::nullopt;
    return std::make_pair(it->second.image_width_px, it->second.image_height_px);
}

}  // namespace forklift::logic
