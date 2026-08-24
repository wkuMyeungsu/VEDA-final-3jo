#pragma once

#include "homography/types.hpp"

#include "nlohmann/json.hpp"

#include <opencv2/core.hpp>

#include <string>

// ArUco 검출과 수동 호모그래피·채널 정합 인터페이스.
namespace homography {

// 모든 ArUco 검출의 공통 픽셀 입력 경로.
void detect_marker_corners(const Config& config, const cv::Mat& image,
                           std::vector<std::vector<cv::Point2f>>& corners,
                           std::vector<int>& ids);

// ChArUco 보드 사진 여러 장에서 카메라 내부 파라미터(K, 왜곡계수)를 산출함.
struct CameraIntrinsics {
    cv::Mat camera_matrix;
    cv::Mat dist_coeffs;
    double reprojection_rmse_px = 0.0;
    int frames_used = 0;
};

CameraIntrinsics calibrate_camera(const Config& config,
                                  const std::vector<std::string>& image_paths);

// 렌즈 왜곡 보정. camera_intrinsics.json이 있을 때 설정하면, 이후 모든 픽셀 입력은
// 무왜곡 좌표로 변환되어 호모그래피 산출에 쓰인다(미설정이면 통과 = 기존 동작).
void set_active_intrinsics(const cv::Mat& camera_matrix, const cv::Mat& dist_coeffs);
bool has_active_intrinsics();
cv::Point2f undistort_point(cv::Point2f pixel);

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
