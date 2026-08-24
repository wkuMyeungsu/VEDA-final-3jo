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
