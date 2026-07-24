#pragma once

#include <opencv2/core.hpp>

#include "camera_calibration.h"

// 왜곡보정 (옵션)
// enabled && 캘리브레이션 유효 && 해상도 일치일 때만 보정본 반환
// 아니면 원본 그대로. 
// out_applied = 실제 적용 여부
cv::Mat TryUndistort(const cv::Mat& color, const CameraCalibration& calib, bool enabled, bool& out_applied);

// BGR -> grayscale (항상)
cv::Mat ConvertToGrayscale(const cv::Mat& img);