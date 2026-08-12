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
#include <stdexcept>
#include <set>

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

namespace {

std::vector<cv::Point2f> transform_points(const std::vector<cv::Point2f>& points,
                                          const cv::Mat& h) {
    std::vector<cv::Point2f> output;
    cv::perspectiveTransform(points, output, h);
    return output;
}

std::vector<cv::Point2f> nearest_square(const std::vector<cv::Point2f>& points,
                                        double side_mm) {
    cv::Point2d center;
    for (const auto& point : points) center += cv::Point2d(point.x, point.y);
    center *= 0.25;
    const double half = side_mm * 0.5;
    const std::vector<cv::Point2d> canonical = {
        {-half, -half}, {half, -half}, {half, half}, {-half, half}};
    double dot = 0.0, cross = 0.0;
    for (int i = 0; i < 4; ++i) {
        const cv::Point2d observed(points[i].x - center.x, points[i].y - center.y);
        dot += canonical[i].dot(observed);
        cross += canonical[i].x * observed.y - canonical[i].y * observed.x;
    }
    const double angle = std::atan2(cross, dot);
    const double c = std::cos(angle), s = std::sin(angle);
    std::vector<cv::Point2f> fitted;
    for (const auto& point : canonical)
        fitted.emplace_back(static_cast<float>(center.x + c * point.x - s * point.y),
                            static_cast<float>(center.y + s * point.x + c * point.y));
    return fitted;
}

}  // namespace

ManualSolveResult solve_square_markers(
        const std::vector<SquareMarkerObservation>& observations,
        double side_mm, int reference_marker_id,
        const std::vector<int>& excluded_ids) {
    if (side_mm <= 0.0) throw std::runtime_error("marker_size_mm must be positive");
    const std::set<int> excluded(excluded_ids.begin(), excluded_ids.end());
    const SquareMarkerObservation* reference = nullptr;
    std::vector<const SquareMarkerObservation*> used;
    ManualSolveResult result;
    result.reference_marker_id = reference_marker_id;
    result.excluded_ids = excluded_ids;
    for (const auto& marker : observations) {
        result.detected_ids.push_back(marker.id);
        if (marker.id == reference_marker_id) reference = &marker;
        if (!excluded.count(marker.id) && marker.corners.size() == 4) used.push_back(&marker);
    }
    result.detected = static_cast<int>(observations.size());
    if (!reference || reference->corners.size() != 4)
        throw std::runtime_error("reference marker was not detected");
    if (excluded.count(reference_marker_id))
        throw std::runtime_error("reference marker cannot be excluded");
    if (used.empty()) throw std::runtime_error("at least one marker is required");

    const std::vector<cv::Point2f> reference_world = {
        {0, 0}, {static_cast<float>(side_mm), 0},
        {static_cast<float>(side_mm), static_cast<float>(side_mm)},
        {0, static_cast<float>(side_mm)}};
    cv::Mat h = cv::getPerspectiveTransform(reference->corners, reference_world);
    for (int iteration = 0; iteration < 50; ++iteration) {
        std::vector<cv::Point2f> pixels, targets;
        for (const auto* marker : used) {
            const auto world = transform_points(marker->corners, h);
            const auto fitted = marker->id == reference_marker_id
                ? reference_world : nearest_square(world, side_mm);
            pixels.insert(pixels.end(), marker->corners.begin(), marker->corners.end());
            targets.insert(targets.end(), fitted.begin(), fitted.end());
        }
        cv::Mat next = cv::findHomography(pixels, targets, 0);
        if (next.empty()) throw std::runtime_error("findHomography failed");
        next /= next.at<double>(2, 2);
        const double change = cv::norm(next - h, cv::NORM_INF);
        h = next;
        result.iterations = iteration + 1;
        if (change < 1e-9) break;
    }
    result.h_pixel_to_world = h;
    result.h_world_to_pixel = h.inv();
    result.used = static_cast<int>(used.size());
    result.inliers = result.used * 4;
    double total = 0.0;
    for (const auto* marker : used) {
        result.used_ids.push_back(marker->id);
        const auto world = transform_points(marker->corners, h);
        const auto fitted = marker->id == reference_marker_id
            ? reference_world : nearest_square(world, side_mm);
        double sum = 0.0;
        for (int i = 0; i < 4; ++i) sum += cv::norm(world[i] - fitted[i]) * cv::norm(world[i] - fitted[i]);
        const double error = std::sqrt(sum / 4.0);
        total += sum;
        const cv::Point2f edge = fitted[1] - fitted[0];
        result.markers.push_back({marker->id, fitted[0].x, fitted[0].y,
            std::atan2(edge.y, edge.x) * 180.0 / CV_PI, error});
    }
    result.rmse_mm = std::sqrt(total / std::max(1, result.inliers));
    const double suspect_gate = std::max(2.0, side_mm * 0.03);
    for (const auto& marker : result.markers)
        if (marker.square_error_mm > suspect_gate) result.suspicious_ids.push_back(marker.id);
    return result;
}

ManualSolveResult solve_manual_image(const Config& config, const cv::Mat& image,
                                     const json& layout, cv::Mat* overlay) {
    const double side_mm = layout.value("marker_size_mm", config.manual_solve.marker_size_mm);
    ManualSolveResult result;
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners, rejected;
    cv::aruco::detectMarkers(image, dictionary(config), corners, ids);
    std::vector<SquareMarkerObservation> observations;
    for (size_t i = 0; i < ids.size(); ++i) {
        observations.push_back({ids[i], corners[i]});
    }
    const int reference_id = layout.at("reference_marker_id").get<int>();
    result = solve_square_markers(observations, side_mm, reference_id,
                                  layout.value("excluded_ids", std::vector<int>{}));
    cv::Mat annotated;
    if (overlay) {
        if (image.channels() == 1) cv::cvtColor(image, annotated, cv::COLOR_GRAY2BGR);
        else annotated = image.clone();
        for (size_t i = 0; i < ids.size(); ++i) {
            const bool excluded = std::find(result.excluded_ids.begin(), result.excluded_ids.end(), ids[i]) != result.excluded_ids.end();
            const bool suspicious = std::find(result.suspicious_ids.begin(), result.suspicious_ids.end(), ids[i]) != result.suspicious_ids.end();
            std::vector<cv::Point> outline;
            for (const auto& point : corners[i]) outline.emplace_back(cvRound(point.x), cvRound(point.y));
            cv::polylines(annotated, outline, true,
                excluded ? cv::Scalar(120, 120, 120) : suspicious ? cv::Scalar(0, 165, 255) : cv::Scalar(0, 200, 0), 3);
            cv::putText(annotated, "ID " + std::to_string(ids[i]), corners[i][0],
                cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 80, 0), 2, cv::LINE_AA);
        }
        cv::putText(annotated, "RMSE=" + std::to_string(result.rmse_mm) + " mm",
                    {12, 28}, cv::FONT_HERSHEY_SIMPLEX, 0.8,
                    cv::Scalar(20, 20, 20), 2, cv::LINE_AA);
        *overlay = annotated;
    }
    return result;
}

cv::Mat align_marker_images(const Config& config, const cv::Mat& source,
                            const cv::Mat& destination,
                            std::vector<int>* common_ids, double* rmse_px) {
    std::vector<int> source_ids, destination_ids;
    std::vector<std::vector<cv::Point2f>> source_corners, destination_corners;
    cv::aruco::detectMarkers(source, dictionary(config), source_corners, source_ids);
    cv::aruco::detectMarkers(destination, dictionary(config), destination_corners, destination_ids);
    std::vector<cv::Point2f> from, to;
    if (common_ids) common_ids->clear();
    for (size_t i = 0; i < source_ids.size(); ++i) {
        const auto found = std::find(destination_ids.begin(), destination_ids.end(), source_ids[i]);
        if (found == destination_ids.end()) continue;
        const size_t j = static_cast<size_t>(found - destination_ids.begin());
        if (source_corners[i].size() != 4 || destination_corners[j].size() != 4) continue;
        from.insert(from.end(), source_corners[i].begin(), source_corners[i].end());
        to.insert(to.end(), destination_corners[j].begin(), destination_corners[j].end());
        if (common_ids) common_ids->push_back(source_ids[i]);
    }
    if (from.size() < 4) throw std::runtime_error("no common ArUco marker for RTSP alignment");
    cv::Mat mask;
    cv::Mat h = cv::findHomography(from, to, cv::RANSAC, 3.0, mask);
    if (h.empty()) throw std::runtime_error("RTSP alignment failed");
    h /= h.at<double>(2, 2);
    double sum = 0.0; int count = 0;
    const auto projected = transform_points(from, h);
    for (size_t i = 0; i < projected.size(); ++i) {
        if (!mask.empty() && !mask.at<uchar>(static_cast<int>(i))) continue;
        const double error = cv::norm(projected[i] - to[i]);
        sum += error * error; ++count;
    }
    if (rmse_px) *rmse_px = count ? std::sqrt(sum / count) : std::numeric_limits<double>::infinity();
    return h;
}

}  // namespace homography
