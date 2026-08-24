#include "homography/calibration.hpp"

#include "homography/config.hpp"
#include "homography/json.hpp"

#include <opencv2/aruco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <set>

namespace homography {

// 모든 ArUco 픽셀 입력이 이 경로를 통과한다. camera_matrix/dist_coeffs는
// 후속 렌즈 보정에서 undistortPoints를 삽입하기 위한 자리이며 현재는 비워 둔다.
void detect_marker_corners(const Config& config, const cv::Mat& image,
                           std::vector<std::vector<cv::Point2f>>& corners,
                           std::vector<int>& ids,
                           const cv::Mat& camera_matrix,
                           const cv::Mat& dist_coeffs) {
    (void)camera_matrix;
    (void)dist_coeffs;
    auto parameters = cv::aruco::DetectorParameters::create();
    parameters->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
    parameters->cornerRefinementWinSize = 5;
    parameters->cornerRefinementMaxIterations = 50;
    parameters->cornerRefinementMinAccuracy = 0.01;
    cv::aruco::detectMarkers(image, dictionary(config), corners, ids, parameters);
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
    // 관측 코너를 이상적 정사각형에 맞출 때 회전뿐 아니라 반사(거울)까지 후보로 둔다.
    // 사용자가 X/Y 축을 어떻게 긋느냐에 따라 픽셀→월드 변환이 좌수(거울) 좌표계가 될 수
    // 있는데, 그러면 관측 코너의 winding이 뒤집혀 회전만으로는 canonical에 맞지 않는다.
    // 정사각형은 뒤집어도 정사각형이므로, 회전 전용 해와 반사 해를 모두 구해 잔차가 작은
    // 쪽을 택함으로써 좌표계 손잡이(handedness)와 무관하게 최소 잔차로 맞춘다.
    auto fit = [&](bool reflect) {
        double dot = 0.0, cross = 0.0;
        for (int i = 0; i < 4; ++i) {
            const double cx = canonical[i].x;
            const double cy = reflect ? -canonical[i].y : canonical[i].y;
            const cv::Point2d observed(points[i].x - center.x, points[i].y - center.y);
            dot += cx * observed.x + cy * observed.y;
            cross += cx * observed.y - cy * observed.x;
        }
        const double angle = std::atan2(cross, dot);
        const double c = std::cos(angle), s = std::sin(angle);
        std::vector<cv::Point2f> fitted;
        double error = 0.0;
        for (int i = 0; i < 4; ++i) {
            const double cx = canonical[i].x;
            const double cy = reflect ? -canonical[i].y : canonical[i].y;
            const double fx = center.x + c * cx - s * cy;
            const double fy = center.y + s * cx + c * cy;
            fitted.emplace_back(static_cast<float>(fx), static_cast<float>(fy));
            const double dx = points[i].x - fx, dy = points[i].y - fy;
            error += dx * dx + dy * dy;
        }
        return std::make_pair(error, fitted);
    };
    const auto rotation = fit(false);
    const auto reflection = fit(true);
    return reflection.first < rotation.first ? reflection.second : rotation.second;
}

cv::Mat params_to_h(const cv::Mat& params) {
    return (cv::Mat_<double>(3, 3) <<
        params.at<double>(0), params.at<double>(1), params.at<double>(2),
        params.at<double>(3), params.at<double>(4), params.at<double>(5),
        params.at<double>(6), params.at<double>(7), 1.0);
}

cv::Mat h_to_params(const cv::Mat& input) {
    cv::Mat h;
    input.convertTo(h, CV_64F);
    h /= h.at<double>(2, 2);
    return (cv::Mat_<double>(8, 1) << h.at<double>(0, 0), h.at<double>(0, 1),
        h.at<double>(0, 2), h.at<double>(1, 0), h.at<double>(1, 1),
        h.at<double>(1, 2), h.at<double>(2, 0), h.at<double>(2, 1));
}

cv::Point2d transform_point(const cv::Point2f& point, const cv::Mat& h) {
    const double denominator = h.at<double>(2, 0) * point.x +
        h.at<double>(2, 1) * point.y + 1.0;
    if (std::abs(denominator) < 1e-10)
        return {std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
    return {(h.at<double>(0, 0) * point.x + h.at<double>(0, 1) * point.y +
             h.at<double>(0, 2)) / denominator,
            (h.at<double>(1, 0) * point.x + h.at<double>(1, 1) * point.y +
             h.at<double>(1, 2)) / denominator};
}

std::vector<double> nonlinear_residuals(
        const cv::Mat& params,
        const std::vector<const SquareMarkerObservation*>& markers,
        double side_mm, const SquareMarkerObservation* reference,
        const ManualSolveOptions& options) {
    (void)reference;
    const cv::Mat h = params_to_h(params);
    std::vector<double> residuals;
    const double diagonal = side_mm * std::sqrt(2.0);
    for (const auto* marker : markers) {
        std::vector<cv::Point2d> points;
        for (const auto& corner : marker->corners) points.push_back(transform_point(corner, h));
        bool finite = std::all_of(points.begin(), points.end(), [](const auto& point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        });
        if (!finite) {
            residuals.insert(residuals.end(), 10, 1e6);
            continue;
        }
        for (int edge = 0; edge < 4; ++edge)
            residuals.push_back(cv::norm(points[(edge + 1) % 4] - points[edge]) - side_mm);
        residuals.push_back((cv::norm(points[2] - points[0]) - diagonal) * 0.7);
        residuals.push_back((cv::norm(points[3] - points[1]) - diagonal) * 0.7);
        for (int corner = 0; corner < 4; ++corner) {
            const cv::Point2d before = points[(corner + 3) % 4] - points[corner];
            const cv::Point2d after = points[(corner + 1) % 4] - points[corner];
            residuals.push_back(before.dot(after) / side_mm * 0.5);
        }
    }
    if (options.axes.enabled) {
        const std::vector<std::pair<cv::Point2f, cv::Point2d>> axes = {
            {options.axes.origin_px, {0.0, 0.0}},
            {options.axes.x_end_px, {options.axes.x_length_mm, 0.0}},
            {options.axes.y_end_px, {0.0, options.axes.y_length_mm}}};
        for (const auto& item : axes) {
            const auto actual = transform_point(item.first, h);
            residuals.push_back((actual.x - item.second.x) * 300.0);
            residuals.push_back((actual.y - item.second.y) * 300.0);
        }
    }
    for (const auto& measurement : options.measurements) {
        const auto origin = transform_point(measurement.origin_px, h);
        const auto target = transform_point(measurement.target_px, h);
        residuals.push_back((cv::norm(target - origin) - measurement.distance_mm) * 30.0);
    }
    return residuals;
}

double residual_cost(const std::vector<double>& residuals) {
    double result = 0.0;
    for (const double value : residuals) result += value * value;
    return result;
}

cv::Mat optimize_homography(
        const cv::Mat& initial,
        const std::vector<const SquareMarkerObservation*>& markers,
        double side_mm, const SquareMarkerObservation* reference,
        const ManualSolveOptions& options, int* iterations) {
    if (options.axes.enabled) {
        // 세 축 대응점을 정확히 고정하는 호모그래피의 남은 자유도는 2개다.
        // G(g,h)는 (0,0), (Lx,0), (0,Ly)를 항상 자기 자신으로 보내므로
        // G * initial을 최적화하면 축은 수치 가중치와 무관하게 보존된다.
        auto candidate_h = [&](const cv::Mat& values) {
            const double g = values.at<double>(0) / options.axes.x_length_mm;
            const double k = values.at<double>(1) / options.axes.y_length_mm;
            const cv::Mat fixed_points = (cv::Mat_<double>(3, 3) <<
                1.0 + g * options.axes.x_length_mm, 0, 0,
                0, 1.0 + k * options.axes.y_length_mm, 0,
                g, k, 1.0);
            cv::Mat value = fixed_points * initial;
            value /= value.at<double>(2, 2);
            return value;
        };
        auto values_for = [&](const cv::Mat& values) {
            return nonlinear_residuals(h_to_params(candidate_h(values)), markers,
                                       side_mm, reference, options);
        };
        cv::Mat values = cv::Mat::zeros(2, 1, CV_64F);
        std::vector<double> residuals = values_for(values);
        double cost = residual_cost(residuals), lambda = 1e-3;
        // 선형 초기해가 잘못된 projective basin에 있을 수 있으므로 축을
        // 보존하는 2차원 공간만 저비용으로 탐색해 LM 시작점을 고른다.
        for (int x = -9; x <= 9; ++x) {
            for (int y = -9; y <= 9; ++y) {
                cv::Mat candidate = (cv::Mat_<double>(2, 1) << x * 0.1, y * 0.1);
                const auto candidate_residuals = values_for(candidate);
                const double candidate_cost = residual_cost(candidate_residuals);
                if (std::isfinite(candidate_cost) && candidate_cost < cost) {
                    values = candidate;
                    residuals = candidate_residuals;
                    cost = candidate_cost;
                }
            }
        }
        int completed = 0;
        for (int iteration = 0; iteration < 120; ++iteration) {
            cv::Mat jacobian(static_cast<int>(residuals.size()), 2, CV_64F);
            for (int parameter = 0; parameter < 2; ++parameter) {
                cv::Mat shifted = values.clone();
                shifted.at<double>(parameter) += 1e-6;
                const auto shifted_residuals = values_for(shifted);
                for (int row = 0; row < static_cast<int>(residuals.size()); ++row)
                    jacobian.at<double>(row, parameter) =
                        (shifted_residuals[row] - residuals[row]) / 1e-6;
            }
            cv::Mat residual_matrix(static_cast<int>(residuals.size()), 1, CV_64F);
            for (int row = 0; row < residual_matrix.rows; ++row)
                residual_matrix.at<double>(row) = residuals[row];
            cv::Mat normal = jacobian.t() * jacobian;
            for (int diagonal_index = 0; diagonal_index < 2; ++diagonal_index)
                normal.at<double>(diagonal_index, diagonal_index) +=
                    lambda * std::max(1.0, normal.at<double>(diagonal_index, diagonal_index));
            cv::Mat delta;
            if (!cv::solve(normal, -jacobian.t() * residual_matrix, delta, cv::DECOMP_SVD)) break;
            const cv::Mat candidate_values = values + delta;
            const auto candidate_residuals = values_for(candidate_values);
            const double candidate_cost = residual_cost(candidate_residuals);
            ++completed;
            if (std::isfinite(candidate_cost) && candidate_cost < cost) {
                values = candidate_values;
                residuals = candidate_residuals;
                const double improvement = cost - candidate_cost;
                cost = candidate_cost;
                lambda = std::max(1e-12, lambda * 0.3);
                if (cv::norm(delta) < 1e-11 || improvement < 1e-9) break;
            } else {
                lambda = std::min(1e12, lambda * 10.0);
            }
        }
        if (iterations) *iterations = completed;
        return candidate_h(values);
    }
    cv::Mat params = h_to_params(initial);
    std::vector<double> residuals = nonlinear_residuals(
        params, markers, side_mm, reference, options);
    double cost = residual_cost(residuals), lambda = 1e-3;
    const double steps[8] = {1e-6, 1e-6, 1e-3, 1e-6, 1e-6, 1e-3, 1e-9, 1e-9};
    int completed = 0;
    for (int iteration = 0; iteration < 120; ++iteration) {
        cv::Mat jacobian(static_cast<int>(residuals.size()), 8, CV_64F);
        for (int parameter = 0; parameter < 8; ++parameter) {
            cv::Mat shifted = params.clone();
            shifted.at<double>(parameter) += steps[parameter];
            const auto values = nonlinear_residuals(
                shifted, markers, side_mm, reference, options);
            for (int row = 0; row < static_cast<int>(values.size()); ++row)
                jacobian.at<double>(row, parameter) =
                    (values[row] - residuals[row]) / steps[parameter];
        }
        cv::Mat residual_matrix(static_cast<int>(residuals.size()), 1, CV_64F);
        for (int row = 0; row < residual_matrix.rows; ++row)
            residual_matrix.at<double>(row) = residuals[row];
        cv::Mat normal = jacobian.t() * jacobian;
        for (int diagonal_index = 0; diagonal_index < 8; ++diagonal_index)
            normal.at<double>(diagonal_index, diagonal_index) +=
                lambda * std::max(1.0, normal.at<double>(diagonal_index, diagonal_index));
        cv::Mat delta;
        if (!cv::solve(normal, -jacobian.t() * residual_matrix, delta, cv::DECOMP_SVD)) break;
        const cv::Mat candidate = params + delta;
        const auto candidate_residuals = nonlinear_residuals(
            candidate, markers, side_mm, reference, options);
        const double candidate_cost = residual_cost(candidate_residuals);
        ++completed;
        if (std::isfinite(candidate_cost) && candidate_cost < cost) {
            params = candidate;
            residuals = candidate_residuals;
            const double improvement = cost - candidate_cost;
            cost = candidate_cost;
            lambda = std::max(1e-12, lambda * 0.3);
            if (cv::norm(delta) < 1e-9 || improvement < 1e-9) break;
        } else {
            lambda = std::min(1e12, lambda * 10.0);
        }
    }
    if (iterations) *iterations = completed;
    return params_to_h(params);
}

}  // namespace

ManualSolveResult solve_square_markers(
        const std::vector<SquareMarkerObservation>& observations,
        double side_mm, int reference_marker_id,
        const std::vector<int>& excluded_ids,
        const ManualSolveOptions& options) {
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
    if (options.axes.enabled) {
        const auto origin = transform_point(options.axes.origin_px, h);
        const auto x = transform_point(options.axes.x_end_px, h) - origin;
        const auto y = transform_point(options.axes.y_end_px, h) - origin;
        const double determinant = x.x * y.y - y.x * x.y;
        if (std::abs(determinant) < 1e-9)
            throw std::runtime_error("drawn X/Y axes must not be parallel");
        const cv::Mat linear = (cv::Mat_<double>(2, 2) <<
            options.axes.x_length_mm * y.y / determinant,
            -options.axes.x_length_mm * y.x / determinant,
            -options.axes.y_length_mm * x.y / determinant,
            options.axes.y_length_mm * x.x / determinant);
        const cv::Mat coordinates = (cv::Mat_<double>(3, 3) <<
            linear.at<double>(0, 0), linear.at<double>(0, 1),
            -(linear.at<double>(0, 0) * origin.x + linear.at<double>(0, 1) * origin.y),
            linear.at<double>(1, 0), linear.at<double>(1, 1),
            -(linear.at<double>(1, 0) * origin.x + linear.at<double>(1, 1) * origin.y),
            0, 0, 1);
        h = coordinates * h;
        h /= h.at<double>(2, 2);
    }
    int nonlinear_iterations = 0;
    h = optimize_homography(h, used, side_mm, reference, options,
                            &nonlinear_iterations);
    result.iterations += nonlinear_iterations;
    result.h_camera_pixels_to_channel_map = h;
    result.h_channel_map_to_camera_pixels = h.inv();
    result.used = static_cast<int>(used.size());
    result.inliers = result.used * 4;
    double total = 0.0;
    for (const auto* marker : used) {
        result.used_ids.push_back(marker->id);
        const auto world = transform_points(marker->corners, h);
        const auto fitted = marker->id == reference_marker_id && !options.axes.enabled
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
    if (options.axes.enabled) {
        const auto origin = transform_point(options.axes.origin_px, h);
        const auto x = transform_point(options.axes.x_end_px, h);
        const auto y = transform_point(options.axes.y_end_px, h);
        result.axis_max_error_mm = std::max({cv::norm(origin),
            cv::norm(x - cv::Point2d(options.axes.x_length_mm, 0)),
            cv::norm(y - cv::Point2d(0, options.axes.y_length_mm))});
    }
    double measurement_sum = 0.0;
    for (const auto& measurement : options.measurements) {
        const double calculated = cv::norm(transform_point(measurement.target_px, h) -
                                           transform_point(measurement.origin_px, h));
        const double error = calculated - measurement.distance_mm;
        result.measurement_errors_mm.push_back(error);
        measurement_sum += error * error;
    }
    result.measurement_rmse_mm = options.measurements.empty() ? 0.0 :
        std::sqrt(measurement_sum / options.measurements.size());
    const double suspect_gate = std::max(2.0, side_mm * 0.03);
    for (const auto& marker : result.markers)
        if (marker.marker_shape_error_mm > suspect_gate) result.suspicious_ids.push_back(marker.id);
    return result;
}

ManualSolveResult solve_manual_image(const Config& config, const cv::Mat& image,
                                     const json& layout, cv::Mat* overlay) {
    const double side_mm = layout.value("marker_size_mm", config.manual_solve.marker_size_mm);
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners, rejected;
    detect_marker_corners(config, image, corners, ids);
    // 자동 검출이 조금 어긋난 경우 웹 UI에서 사용자가 조정한 꼭짓점을
    // 산출에 그대로 사용한다. 보정값이 없는 마커는 자동 검출값을 유지한다.
    const auto corner_overrides = layout.value("corner_overrides", json::object());
    if (!corner_overrides.is_object())
        throw std::runtime_error("corner_overrides must be an object");
    for (size_t index = 0; index < ids.size(); ++index) {
        const std::string key = std::to_string(ids[index]);
        if (!corner_overrides.contains(key)) continue;
        const auto& override_points = corner_overrides.at(key);
        if (!override_points.is_array() || override_points.size() != 4)
            throw std::runtime_error("corner override must contain four points");
        std::vector<cv::Point2f> corrected;
        for (const auto& point : override_points) {
            if (!point.is_object() || !point.contains("x") || !point.contains("y"))
                throw std::runtime_error("corner override point must contain x and y");
            corrected.emplace_back(point.at("x").get<float>(), point.at("y").get<float>());
        }
        corners[index] = std::move(corrected);
    }
    std::vector<SquareMarkerObservation> observations;
    for (size_t i = 0; i < ids.size(); ++i) {
        observations.push_back({ids[i], corners[i]});
    }
    auto point_from_json = [](const json& value) {
        return cv::Point2f(value.at("x").get<float>(), value.at("y").get<float>());
    };
    ManualSolveOptions options;
    if (layout.contains("axis_origin_px") && layout.contains("axis_x_end_px") &&
        layout.contains("axis_y_end_px") && layout.contains("axis_x_length_mm") &&
        layout.contains("axis_y_length_mm") && !layout.at("axis_origin_px").is_null() &&
        !layout.at("axis_x_length_mm").is_null() && !layout.at("axis_y_length_mm").is_null()) {
        options.axes = {true, point_from_json(layout.at("axis_origin_px")),
            point_from_json(layout.at("axis_x_end_px")),
            point_from_json(layout.at("axis_y_end_px")),
            layout.at("axis_x_length_mm").get<double>(),
            layout.at("axis_y_length_mm").get<double>()};
        if (options.axes.x_length_mm <= 0.0 || options.axes.y_length_mm <= 0.0)
            throw std::runtime_error("axis lengths must be positive");
    }
    if (layout.contains("measurements") && layout.at("measurements").is_array()) {
        for (const auto& item : layout.at("measurements")) {
            if (!item.contains("origin_px") || !item.contains("target_px") ||
                !item.contains("distance_mm") || item.at("distance_mm").is_null()) continue;
            const ManualDistanceConstraint constraint{point_from_json(item.at("origin_px")),
                point_from_json(item.at("target_px")), item.at("distance_mm").get<double>()};
            if (constraint.distance_mm > 0.0 &&
                cv::norm(constraint.target_px - constraint.origin_px) > 1e-3)
                options.measurements.push_back(constraint);
        }
    }
    const int reference_id = layout.at("reference_marker_id").get<int>();
    ManualSolveResult result = solve_square_markers(observations, side_mm, reference_id,
        layout.value("excluded_ids", std::vector<int>{}), options);
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
    detect_marker_corners(config, source, source_corners, source_ids);
    detect_marker_corners(config, destination, destination_corners, destination_ids);
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
