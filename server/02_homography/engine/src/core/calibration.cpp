#include "homography/calibration.hpp"

#include "homography/config.hpp"
#include "homography/json.hpp"

#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <stdexcept>

namespace homography {

DetectionResult calibrate_image(const Config& config, const cv::Mat& image) {
    DetectionResult result;
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners, rejected;
    // OpenCV가 반환하는 네 코너 순서를 월드 코너 생성 순서와 일치시켜
    // 픽셀 좌표와 실제 좌표를 올바르게 대응시킴.
    cv::aruco::detectMarkers(image, dictionary(config), corners, ids);
    std::vector<cv::Point2f> pixels, worlds;
    std::vector<int> valid_ids;
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto world = world_corners(config, ids[i]);
        if (world.empty() || corners[i].size() != 4) continue;
        for (int corner = 0; corner < 4; ++corner) {
            pixels.push_back(corners[i][corner]);
            worlds.push_back(world[corner]);
        }
        valid_ids.push_back(ids[i]);
    }
    if (pixels.size() < 8)
        throw std::runtime_error("fewer than two valid markers detected");
    cv::Mat mask;
    // RANSAC으로 인쇄 오차나 부분 가림에 따른 이상 코너 제외함.
    // 픽셀 좌표를 실제 보드 cm 좌표로 변환하는 행렬 생성함.
    result.h_pixel_to_world = cv::findHomography(
        pixels, worlds, cv::RANSAC, config.calibration.ransac_threshold_cm, mask);
    if (result.h_pixel_to_world.empty())
        throw std::runtime_error("findHomography failed");
    result.h_pixel_to_world /= result.h_pixel_to_world.at<double>(2, 2);
    result.h_world_to_pixel = result.h_pixel_to_world.inv();
    // 채택된 코너의 재투영 유클리드 거리 계산함.
    // 결과 단위는 world 좌표와 같은 cm.
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < static_cast<int>(pixels.size()); ++i) {
        if (!mask.at<uchar>(i)) continue;
        std::vector<cv::Point2f> projected;
        const std::vector<cv::Point2f> source{pixels[i]};
        cv::perspectiveTransform(source, projected, result.h_pixel_to_world);
        const double dx = projected[0].x - worlds[i].x;
        const double dy = projected[0].y - worlds[i].y;
        sum += dx * dx + dy * dy;
        ++count;
    }
    result.inliers = count;
    result.rmse_cm = count ? std::sqrt(sum / count)
                           : std::numeric_limits<double>::infinity();
    result.ids = valid_ids;
    result.pixels = pixels;
    result.worlds = worlds;
    return result;
}

void write_calibration(const std::string& path, const Config& config,
                       const DetectionResult& detection, const cv::Size& size,
                       int channel, double gate) {
    const json value = {
        {"schema_version", 1}, {"channel", channel},
        {"H_pixel_to_world", matrix_to_json(detection.h_pixel_to_world)},
        {"H_world_to_pixel", matrix_to_json(detection.h_world_to_pixel)},
        {"image_size", {{"width", size.width}, {"height", size.height}}},
        {"dictionary", config.dictionary}, {"grid", config_to_json(config)},
        {"inliers", detection.inliers}, {"reproj_rmse_cm", detection.rmse_cm},
        {"rmse_gate_cm", gate}, {"created_utc", utc_now()}};
    std::ofstream output(path);
    if (!output) throw std::runtime_error("cannot write output: " + path);
    output << std::setw(2) << value << '\n';
}

ManualSolveResult solve_manual_image(const Config& config, const cv::Mat& image,
                                     const json& layout, cv::Mat* overlay) {
    const double side_mm = layout.value("marker_size_mm", config.manual_solve.marker_size_mm);
    if (side_mm <= 0.0) throw std::runtime_error("marker_size_mm must be positive");
    if (!layout.contains("markers") || !layout.at("markers").is_array())
        throw std::runtime_error("layout.markers must be an array");
    std::map<int, std::pair<double, double>> positions;
    // 수동 레이아웃은 각 마커의 좌상단 위치를 기준으로 함.
    // 아래 네 점은 ArUco 검출 코너와 같은 시계 방향 순서.
    for (const auto& item : layout.at("markers"))
        positions[item.at("id").get<int>()] = {
            item.at("x_mm").get<double>(), item.at("y_mm").get<double>()};

    ManualSolveResult result;
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners, rejected;
    cv::aruco::detectMarkers(image, dictionary(config), corners, ids);
    result.detected = static_cast<int>(ids.size());
    result.detected_ids = ids;
    std::vector<cv::Point2f> pixels, worlds;
    std::vector<int> used_ids;
    for (size_t i = 0; i < ids.size(); ++i) {
        const auto it = positions.find(ids[i]);
        if (it == positions.end() || corners[i].size() != 4) continue;
        const double x = it->second.first;
        const double y = it->second.second;
        const std::vector<cv::Point2f> world = {
            {static_cast<float>(x), static_cast<float>(y)},
            {static_cast<float>(x + side_mm), static_cast<float>(y)},
            {static_cast<float>(x + side_mm), static_cast<float>(y + side_mm)},
            {static_cast<float>(x), static_cast<float>(y + side_mm)}};
        for (int corner = 0; corner < 4; ++corner) {
            pixels.push_back(corners[i][corner]);
            worlds.push_back(world[corner]);
        }
        used_ids.push_back(ids[i]);
    }
    result.used = static_cast<int>(used_ids.size());
    result.used_ids = used_ids;
    for (const auto& position : positions)
        if (std::find(ids.begin(), ids.end(), position.first) == ids.end())
            result.missing_ids.push_back(position.first);
    if (pixels.size() < 4)
        throw std::runtime_error("at least one valid marker is required");

    cv::Mat mask;
    // 수동 산출은 픽셀→월드 변환을 구하며, world 좌표 단위는 mm.
    result.h_pixel_to_world = cv::findHomography(
        pixels, worlds, cv::RANSAC, config.manual_solve.ransac_threshold_mm, mask);
    if (result.h_pixel_to_world.empty())
        throw std::runtime_error("findHomography failed");
    result.h_pixel_to_world /= result.h_pixel_to_world.at<double>(2, 2);
    result.h_world_to_pixel = result.h_pixel_to_world.inv();
    // overlay 요청 시 검출된 각 코너 주변에 오차 품질을 색으로 표시함.
    double sum = 0.0;
    int count = 0;
    cv::Mat annotated;
    if (overlay) {
        if (image.channels() == 1) cv::cvtColor(image, annotated, cv::COLOR_GRAY2BGR);
        else annotated = image.clone();
    }
    for (int i = 0; i < static_cast<int>(pixels.size()); ++i) {
        std::vector<cv::Point2f> projected;
        const std::vector<cv::Point2f> source{pixels[i]};
        cv::perspectiveTransform(source, projected, result.h_pixel_to_world);
        const double dx = projected[0].x - worlds[i].x;
        const double dy = projected[0].y - worlds[i].y;
        const double error = std::sqrt(dx * dx + dy * dy);
        if (mask.empty() || mask.at<uchar>(i)) {
            sum += error * error;
            ++count;
        }
        if (overlay)
            cv::circle(annotated, pixels[i], 5,
                       error <= 2.0 ? cv::Scalar(0, 200, 0) : cv::Scalar(0, 0, 255),
                       2);
    }
    result.inliers = count;
    result.rmse_mm = count ? std::sqrt(sum / count)
                           : std::numeric_limits<double>::infinity();
    if (overlay) {
        for (size_t i = 0; i < ids.size(); ++i)
            if (positions.count(ids[i]) && corners[i].size() == 4)
                cv::putText(annotated, "ID " + std::to_string(ids[i]),
                    corners[i][0] + cv::Point2f(4, -6), cv::FONT_HERSHEY_SIMPLEX,
                    0.6, cv::Scalar(255, 80, 0), 2, cv::LINE_AA);
        cv::putText(annotated, "RMSE=" + std::to_string(result.rmse_mm) + " mm",
                    {12, 28}, cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(20, 20, 20), 2, cv::LINE_AA);
        *overlay = annotated;
    }
    return result;
}

}  // namespace homography
