#pragma once

#include "homography/types.hpp"

#include "nlohmann/json.hpp"

#include <opencv2/core.hpp>

#include <string>

// ArUco 검출과 자동·수동 호모그래피 산출 인터페이스.
namespace homography {

// 모든 ArUco 검출의 공통 픽셀 입력 경로. 선택 인자는 후속 undistort 연결용이며
// 이번 버전에서는 계수를 적용하지 않는다.
void detect_marker_corners(const Config& config, const cv::Mat& image,
                           std::vector<std::vector<cv::Point2f>>& corners,
                           std::vector<int>& ids,
                           const cv::Mat& camera_matrix = {},
                           const cv::Mat& dist_coeffs = {});

// 이미지에서 ArUco 마커를 검출하고 픽셀→mm 호모그래피를 계산함.
// 유효한 마커 두 개 이상 필요. 입력은 같은 카메라의 보정 영상.
DetectionResult calibrate_image(const Config& config, const cv::Mat& image);

// 자동 캘리브레이션 결과와 당시 설정을 JSON으로 저장함.
// H_pixel_to_world의 월드 단위와 gate 판정 임계값은 모두 mm임.
void write_calibration(const std::string& path, const Config& config,
                       const DetectionResult& detection, const cv::Size& size,
                       int channel, double gate);

// layout의 x_mm/y_mm은 좌상단 위치. marker_size_mm로 네 코너 확장함.
// overlay가 있으면 입력 영상 크기의 진단 이미지 생성함.
ManualSolveResult solve_manual_image(const Config& config, const cv::Mat& image,
                                     const nlohmann::json& layout,
                                     cv::Mat* overlay);

ManualSolveResult solve_square_markers(
    const std::vector<SquareMarkerObservation>& observations,
    double marker_size_mm, int reference_marker_id,
    const std::vector<int>& excluded_ids,
    const ManualSolveOptions& options = {});

// 두 이미지의 공통 ArUco 코너로 source 픽셀→destination 픽셀 정합을 구함.
cv::Mat align_marker_images(const Config& config, const cv::Mat& source,
                            const cv::Mat& destination,
                            std::vector<int>* common_ids = nullptr,
                            double* rmse_px = nullptr);

}  // namespace homography
