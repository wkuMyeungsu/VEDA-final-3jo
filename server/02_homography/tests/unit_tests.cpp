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

}  // namespace

int main() {
    try {
        test_grid_coordinates();
        test_matrix_json_roundtrip();
        test_board_rendering();
        test_calibration();
        std::cout << "homography_unit_tests: 4 tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "homography_unit_tests: FAILED: " << error.what() << '\n';
        return 1;
    }
}
