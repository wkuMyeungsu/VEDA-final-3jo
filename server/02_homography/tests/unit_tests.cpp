#include "homography/calibration.hpp"
#include "homography/config.hpp"
#include "homography/json.hpp"
#include "homography/render.hpp"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using homography::Config;

Config test_config() {
    Config config{};
    config.dictionary = "DICT_4X4_50";
    config.cols = 4;
    config.rows = 3;
    config.marker_len_cm = 4.0;
    config.gap_cm = 2.0;
    config.id_offset = 10;
    config.origin_corner = "TL";
    config.marker_output = {100.0, 20.0, 300.0, ""};
    config.calibration = {2.0, 3.0, -1};
    config.manual_solve = {100.0, 3.0};
    config.preview = {20, 0.5, 1.0};
    return config;
}

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void expect_near(double actual, double expected, double tolerance,
                 const std::string& message) {
    expect(std::abs(actual - expected) <= tolerance,
           message + " (actual=" + std::to_string(actual) + ")");
}

void test_grid_coordinates() {
    const Config config = test_config();
    const auto corners = homography::world_corners(config, 11);
    expect(corners.size() == 4, "valid marker must have four world corners");
    expect_near(corners[0].x, 6.0, 1e-6, "second marker x coordinate in cm");
    expect_near(corners[0].y, 0.0, 1e-6, "second marker y coordinate");
    expect_near(corners[2].x, 10.0, 1e-6, "marker right edge in cm");
    expect_near(corners[2].y, 4.0, 1e-6, "marker bottom edge in cm");
    expect(homography::world_corners(config, 9).empty(),
           "marker before id offset must be rejected");
    expect_near(homography::grid_width_mm(config), 220.0, 1e-6,
                "grid width");
    expect_near(homography::grid_height_mm(config), 160.0, 1e-6,
                "grid height");
}

void test_matrix_json_roundtrip() {
    const cv::Mat original = (cv::Mat_<double>(3, 3) <<
        1.0, 2.0, 3.0,
        4.0, 5.0, 6.0,
        7.0, 8.0, 1.0);
    const cv::Mat restored = homography::json_to_matrix(
        homography::matrix_to_json(original));
    expect(cv::norm(original - restored, cv::NORM_INF) < 1e-12,
           "matrix JSON roundtrip");
}

void test_board_rendering() {
    const Config config = test_config();
    const cv::Mat board = homography::render_board(config, 20);
    expect(board.type() == CV_8UC1, "rendered board must be grayscale");
    expect(board.cols == 480 && board.rows == 360,
           "rendered board dimensions");
}

void test_calibration() {
    const Config config = test_config();
    const cv::Mat board = homography::render_board(config, 20);
    const std::vector<cv::Point2f> source = {
        {0, 0}, {static_cast<float>(board.cols), 0},
        {static_cast<float>(board.cols), static_cast<float>(board.rows)},
        {0, static_cast<float>(board.rows)}};
    const std::vector<cv::Point2f> destination = {
        {80, 60}, {700, 35}, {735, 540}, {55, 565}};
    const cv::Mat transform = cv::getPerspectiveTransform(destination, source);
    const cv::Mat camera(620, 800, CV_8UC1, cv::Scalar(255));
    cv::Mat warped;
    cv::warpPerspective(board, warped, transform, camera.size());

    const auto result = homography::calibrate_image(config, warped);
    expect(result.ids.size() == 6, "calibration marker count");
    expect(result.inliers == 24, "calibration inlier corner count");
    expect(result.rmse_cm < 0.25, "calibration reprojection error");
}

std::vector<cv::Point2f> project(const std::vector<cv::Point2f>& points,
                                 const cv::Mat& h) {
    std::vector<cv::Point2f> output;
    cv::perspectiveTransform(points, output, h);
    return output;
}

void test_irregular_square_calibration() {
    const double side = 80.0;
    const cv::Mat world_to_pixel = (cv::Mat_<double>(3, 3) <<
        1.7, 0.18, 430.0, -0.09, 1.35, 210.0, 0.00035, -0.00022, 1.0);
    std::vector<homography::SquareMarkerObservation> observations;
    const std::vector<cv::Point2f> poses = {{0, 0}, {245, 47}, {-90, 230}, {330, 310}};
    const std::vector<double> angles = {0.0, 0.43, -0.71, 1.08};
    for (int marker = 0; marker < 4; ++marker) {
        const double c = std::cos(angles[marker]), s = std::sin(angles[marker]);
        const std::vector<cv::Point2f> local = {{0, 0}, {80, 0}, {80, 80}, {0, 80}};
        std::vector<cv::Point2f> world;
        for (const auto& p : local)
            world.emplace_back(poses[marker].x + c * p.x - s * p.y,
                               poses[marker].y + s * p.x + c * p.y);
        observations.push_back({20 + marker, project(world, world_to_pixel)});
    }
    // 한 코너에 잡음을 넣어도 여러 정사각형의 공통 평면 제약으로 안정적으로 복원해야 함.
    observations[2].corners[2] += cv::Point2f(2.0f, -1.5f);
    const auto result = homography::solve_square_markers(observations, side, 20, {});
    expect(result.used == 4, "all irregular markers should be used");
    expect(result.rmse_mm < 0.8, "square fit should tolerate corner noise");
    for (const auto& marker : result.markers) {
        const auto found = std::find_if(observations.begin(), observations.end(),
            [&](const auto& value) { return value.id == marker.id; });
        const auto recovered = project(found->corners, result.h_pixel_to_world);
        for (int edge = 0; edge < 4; ++edge)
            expect_near(cv::norm(recovered[(edge + 1) % 4] - recovered[edge]), side,
                        1.5, "recovered marker side");
    }
    const auto changed_reference = homography::solve_square_markers(observations, side, 22, {});
    const cv::Point2f a = project(observations[1].corners, result.h_pixel_to_world)[0];
    const cv::Point2f b = project(observations[3].corners, result.h_pixel_to_world)[0];
    const cv::Point2f c = project(observations[1].corners, changed_reference.h_pixel_to_world)[0];
    const cv::Point2f d = project(observations[3].corners, changed_reference.h_pixel_to_world)[0];
    expect_near(cv::norm(a - b), cv::norm(c - d), 1.0,
                "changing reference must preserve relative distance");
    const auto excluded = homography::solve_square_markers(observations, side, 20, {22});
    expect(excluded.used == 3 && excluded.excluded_ids[0] == 22,
           "explicit marker exclusion");
    auto corrupted = observations;
    corrupted[3].corners[2] += cv::Point2f(35.0f, -28.0f);
    const auto suspicious = homography::solve_square_markers(corrupted, side, 20, {});
    expect(std::find(suspicious.suspicious_ids.begin(), suspicious.suspicious_ids.end(), 23)
               != suspicious.suspicious_ids.end(),
           "grossly distorted marker should be an exclusion candidate");
}

void test_free_markers_with_axis_and_distance_constraints() {
    const double side = 60.0;
    const cv::Mat world_to_pixel = (cv::Mat_<double>(3, 3) <<
        2.1, 0.24, 510.0, -0.13, 1.72, 260.0, 0.00042, -0.00031, 1.0);
    std::vector<homography::SquareMarkerObservation> observations;
    const std::vector<cv::Point2f> poses = {
        {25, 35}, {240, 28}, {75, 205}, {315, 245}, {175, 390}};
    const std::vector<double> angles = {0.08, 0.61, -0.47, 1.17, -0.92};
    const std::vector<cv::Point2f> local = {
        {0, 0}, {static_cast<float>(side), 0},
        {static_cast<float>(side), static_cast<float>(side)},
        {0, static_cast<float>(side)}};
    for (int marker = 0; marker < static_cast<int>(poses.size()); ++marker) {
        const double c = std::cos(angles[marker]), s = std::sin(angles[marker]);
        std::vector<cv::Point2f> world;
        for (const auto& point : local)
            world.emplace_back(poses[marker].x + c * point.x - s * point.y,
                               poses[marker].y + s * point.x + c * point.y);
        observations.push_back({40 + marker, project(world, world_to_pixel)});
    }
    // 검출 코너 수준의 작은 잡음에도 축과 독립 실측 거리는 유지되어야 한다.
    observations[1].corners[2] += cv::Point2f(0.35f, -0.25f);
    observations[3].corners[0] += cv::Point2f(-0.2f, 0.3f);
    homography::ManualSolveOptions options;
    const auto axis_pixels = project({{0, 0}, {360, 0}, {0, 300}}, world_to_pixel);
    options.axes = {true, axis_pixels[0], axis_pixels[1], axis_pixels[2], 360.0, 300.0};
    const std::vector<cv::Point2f> measured_world = {{45, 80}, {330, 265}};
    const auto measured_pixels = project(measured_world, world_to_pixel);
    options.measurements.push_back({measured_pixels[0], measured_pixels[1],
                                    cv::norm(measured_world[1] - measured_world[0])});
    const auto result = homography::solve_square_markers(
        observations, side, 40, {}, options);
    expect(result.axis_max_error_mm < 0.05,
           "axis coordinates must behave as hard constraints");
    expect(result.measurement_rmse_mm < 0.1,
           "measured distance must be optimized as a true distance residual");
    expect(result.rmse_mm < 0.5,
           "free marker rotations must retain low square error (actual=" +
           std::to_string(result.rmse_mm) + ")");
    const auto mapped_axes = project(axis_pixels, result.h_pixel_to_world);
    expect_near(cv::norm(mapped_axes[0]), 0.0, 0.05, "axis origin mapping");
    expect_near(cv::norm(mapped_axes[1] - cv::Point2f(360, 0)), 0.0, 0.05,
                "X axis endpoint mapping");
    expect_near(cv::norm(mapped_axes[2] - cv::Point2f(0, 300)), 0.0, 0.05,
                "Y axis endpoint mapping");
}

void test_rtsp_capture_alignment() {
    const Config config = test_config();
    const cv::Mat board = homography::render_board(config, 20);
    const std::vector<cv::Point2f> board_extent = {{0, 0},
        {static_cast<float>(board.cols), 0},
        {static_cast<float>(board.cols), static_cast<float>(board.rows)},
        {0, static_cast<float>(board.rows)}};
    const std::vector<cv::Point2f> rtsp_extent = {{90, 80}, {690, 65}, {720, 530}, {65, 550}};
    const std::vector<cv::Point2f> capture_extent = {{330, 180}, {2240, 120}, {2350, 1370}, {210, 1430}};
    const cv::Mat board_to_rtsp = cv::getPerspectiveTransform(board_extent, rtsp_extent);
    const cv::Mat board_to_capture = cv::getPerspectiveTransform(board_extent, capture_extent);
    cv::Mat rtsp(600, 800, CV_8UC1, cv::Scalar(255));
    cv::Mat capture(1520, 2592, CV_8UC1, cv::Scalar(255));
    cv::warpPerspective(board, rtsp, board_to_rtsp, rtsp.size(), cv::INTER_NEAREST,
                        cv::BORDER_TRANSPARENT);
    cv::warpPerspective(board, capture, board_to_capture, capture.size(), cv::INTER_NEAREST,
                        cv::BORDER_TRANSPARENT);
    std::vector<int> common;
    double rmse = 0.0;
    const cv::Mat aligned = homography::align_marker_images(
        config, rtsp, capture, &common, &rmse);
    expect(!common.empty(), "RTSP alignment needs common marker IDs");
    expect(rmse < 2.0, "RTSP-to-capture marker alignment RMSE");
    const std::vector<cv::Point2f> sample_board = {{240, 180}};
    const cv::Point2f rtsp_point = project(sample_board, board_to_rtsp)[0];
    const cv::Point2f expected_capture = project(sample_board, board_to_capture)[0];
    const cv::Point2f actual_capture = project({rtsp_point}, aligned)[0];
    expect_near(cv::norm(actual_capture - expected_capture), 0.0, 3.0,
                "800x600 RTSP must be aligned before capture homography");
}

void test_mirrored_axis_frame_is_reflection_invariant() {
    // 사용자가 X/Y 축을 좌수(거울) 방향으로 그으면 픽셀->월드 변환의 선형부 det가
    // 음수(반사)가 된다. 정사각형은 뒤집어도 정사각형이므로, 이 경우에도 오차가
    // 폭발하지 않고 우수(정상) 프레임과 동일한 품질이 나와야 한다.
    // (nearest_square 가 회전뿐 아니라 반사까지 맞추는지 검증)
    const double side = 60.0;
    const cv::Mat world_to_pixel = (cv::Mat_<double>(3, 3) <<
        2.1, 0.24, 510.0, -0.13, 1.72, 260.0, 0.00042, -0.00031, 1.0);
    std::vector<homography::SquareMarkerObservation> observations;
    const std::vector<cv::Point2f> poses = {
        {25, 35}, {240, 28}, {75, 205}, {315, 245}, {175, 390}};
    const std::vector<double> angles = {0.08, 0.61, -0.47, 1.17, -0.92};
    const std::vector<cv::Point2f> local = {
        {0, 0}, {static_cast<float>(side), 0},
        {static_cast<float>(side), static_cast<float>(side)},
        {0, static_cast<float>(side)}};
    for (int marker = 0; marker < static_cast<int>(poses.size()); ++marker) {
        const double c = std::cos(angles[marker]), s = std::sin(angles[marker]);
        std::vector<cv::Point2f> world;
        for (const auto& point : local)
            world.emplace_back(poses[marker].x + c * point.x - s * point.y,
                               poses[marker].y + s * point.x + c * point.y);
        observations.push_back({40 + marker, project(world, world_to_pixel)});
    }
    homography::ManualSolveOptions options;
    const auto axis_pixels = project({{0, 0}, {360, 0}, {0, 300}}, world_to_pixel);
    // X끝점과 Y끝점(및 길이)을 서로 바꿔 좌수(거울) 좌표계를 정의한다.
    options.axes = {true, axis_pixels[0], axis_pixels[2], axis_pixels[1], 300.0, 360.0};
    const auto result = homography::solve_square_markers(
        observations, side, 40, {}, options);
    const cv::Mat& h = result.h_pixel_to_world;
    const double det = h.at<double>(0, 0) * h.at<double>(1, 1) -
                       h.at<double>(0, 1) * h.at<double>(1, 0);
    expect(det < 0.0,
           "swapped axes must actually produce a mirrored (left-handed) frame");
    expect(result.rmse_mm < 0.5,
           "mirrored frame must not inflate square error (actual=" +
           std::to_string(result.rmse_mm) + ")");
    expect(result.suspicious_ids.empty(),
           "mirrored frame must not flag every marker as suspicious");
}

}  // namespace

int main() {
    try {
        test_grid_coordinates();
        test_matrix_json_roundtrip();
        test_board_rendering();
        test_calibration();
        test_irregular_square_calibration();
        test_free_markers_with_axis_and_distance_constraints();
        test_rtsp_capture_alignment();
        test_mirrored_axis_frame_is_reflection_invariant();
        std::cout << "homography_unit_tests: 8 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "homography_unit_tests: FAILED: " << error.what() << '\n';
        return 1;
    }
}
