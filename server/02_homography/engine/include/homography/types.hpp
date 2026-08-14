#pragma once

#include <opencv2/core.hpp>

#include <limits>
#include <string>
#include <vector>

// 엔진 설정과 계산 결과 타입 정의함.
namespace homography {

// 설정 파일에서 읽은 마커 검출·출력·수동 산출 값.
struct Config {
    std::string dictionary;

    // 개별 마커 출력 설정.
    struct MarkerOutput {
        double size_mm;    // 출력 마커 영역 한 변(mm).
        double margin_mm;  // 출력 이미지 바깥 여백(mm).
        double dpi;        // PNG 출력 해상도(DPI).
        std::string label;
    } marker_output;

    // 수동 산출 설정.
    struct ManualSolve {
        double marker_size_mm;        // 실제 마커 한 변(mm).
    } manual_solve;
};

// 수동 산출 결과. world와 rmse는 mm 단위.
struct ManualSolveResult {
    cv::Mat h_pixel_to_world;
    cv::Mat h_world_to_pixel;
    int detected = 0;
    int used = 0;
    int inliers = 0;
    double rmse_mm = std::numeric_limits<double>::infinity();
    std::vector<int> detected_ids;
    std::vector<int> used_ids;
    std::vector<int> missing_ids;
    int reference_marker_id = -1;
    int iterations = 0;
    double axis_max_error_mm = 0.0;
    double measurement_rmse_mm = 0.0;
    std::vector<double> measurement_errors_mm;
    std::vector<int> excluded_ids;
    std::vector<int> suspicious_ids;
    struct MarkerPose {
        int id = -1;
        double x_mm = 0.0;
        double y_mm = 0.0;
        double rotation_deg = 0.0;
        double square_error_mm = 0.0;
    };
    std::vector<MarkerPose> markers;
};

// 이미지 검출과 분리해 합성 데이터에서도 정사각형 제약 산출을 검증할 수 있음.
struct SquareMarkerObservation {
    int id = -1;
    std::vector<cv::Point2f> corners;
};

// 선택적인 픽셀 전처리(향후 undistort) 뒤 최적화에 전달되는 실측 제약.
struct ManualDistanceConstraint {
    cv::Point2f origin_px;
    cv::Point2f target_px;
    double distance_mm = 0.0;
};

struct ManualAxisConstraint {
    bool enabled = false;
    cv::Point2f origin_px;
    cv::Point2f x_end_px;
    cv::Point2f y_end_px;
    double x_length_mm = 0.0;
    double y_length_mm = 0.0;
};

struct ManualSolveOptions {
    ManualAxisConstraint axes;
    std::vector<ManualDistanceConstraint> measurements;
};

}  // namespace homography
