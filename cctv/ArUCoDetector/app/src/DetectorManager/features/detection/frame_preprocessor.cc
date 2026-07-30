#include "frame_preprocessor.h"

#include <opencv2/calib3d.hpp>  // cv::undistort

cv::Mat TryUndistort(const cv::Mat& color, const CameraCalibration& calib, bool enabled, bool& out_applied) 
{
    out_applied = false;
    if (enabled && calib.valid && calib.image_width == color.cols && calib.image_height == color.rows) {
        cv::Mat undistorted;
        cv::undistort(color, undistorted, calib.camera_matrix, calib.dist_coeffs);
        out_applied = true;
        return undistorted; // 보정본 반환
    }
    return color; // 보정 안 함 -> 원본 그대로
}