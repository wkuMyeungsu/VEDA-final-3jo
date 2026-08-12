#pragma once

#include <opencv2/core.hpp>

#include <limits>
#include <string>
#include <vector>

// 엔진 설정과 계산 결과 타입 정의함.
namespace homography {

// 설정 파일에서 읽은 보드·검출·출력 값.
struct Config {
    std::string dictionary;
    int cols;
    int rows;
    int id_offset;
    double marker_len_cm;  // 실제 검은 마커 한 변(cm).
    double gap_cm;         // 이웃한 마커 사이 간격(cm).
    std::string origin_corner;

    // 개별 마커 출력 설정.
    struct MarkerOutput {
        double size_mm;    // 출력 마커 영역 한 변(mm).
        double margin_mm;  // 출력 이미지 바깥 여백(mm).
        double dpi;        // PNG 출력 해상도(DPI).
        std::string label;
    } marker_output;

    // 자동 캘리브레이션 설정.
    struct Calibration {
        double max_rmse_cm;          // 통과 가능한 최대 RMSE(cm).
        double ransac_threshold_cm;  // RANSAC 허용 오차(cm).
        int channel;                 // 결과에 기록할 채널 번호.
    } calibration;

    // 수동 산출 설정.
    struct ManualSolve {
        double marker_size_mm;        // 실제 마커 한 변(mm).
        double ransac_threshold_mm;   // RANSAC 허용 오차(mm).
    } manual_solve;

    // 검증 미리보기 설정.
    struct Preview {
        int scale;                // 1cm를 그리는 픽셀 수(px/cm).
        double good_error_cm;     // 양호 판정 기준(cm).
        double warning_error_cm;  // 경고 판정 기준(cm).
    } preview;
};

// 자동 검출 결과. world와 rmse는 cm 단위.
struct DetectionResult {
    cv::Mat h_pixel_to_world;
    cv::Mat h_world_to_pixel;
    std::vector<int> ids;
    std::vector<cv::Point2f> pixels;
    std::vector<cv::Point2f> worlds;
    int inliers = 0;
    double rmse_cm = std::numeric_limits<double>::infinity();
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
};

}  // namespace homography
