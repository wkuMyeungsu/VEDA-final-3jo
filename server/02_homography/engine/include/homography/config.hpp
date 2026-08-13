#pragma once

#include "homography/types.hpp"

#include <opencv2/aruco.hpp>

#include <string>
#include <vector>

// 설정 파일 해석 및 보드 좌표 계산함.
namespace homography {

// JSON 설정 파일을 읽고 필수 항목과 값의 범위를 검증함.
// 길이 필드는 파일의 단위(cm 또는 mm)를 유지하고, 잘못된 설정은 오류 처리함.
Config read_config(const std::string& path);

// 설정에 지정된 이름의 OpenCV ArUco 사전 반환함.
// 지원하지 않는 사전 이름은 대체하지 않고 오류 처리함.
cv::Ptr<cv::aruco::Dictionary> dictionary(const Config& config);

// 마커 ID에 대응하는 네 개의 실제 보드 코너를 cm 단위로 반환함.
// 지원 범위를 벗어난 ID이면 빈 벡터 반환함.
std::vector<cv::Point2f> world_corners(const Config& config, int id);

// 설정된 마커 간격을 포함한 격자 너비를 mm 단위로 계산함.
// 바깥 여백은 포함하지 않고, 마커 사이 gap만 포함함.
double grid_width_mm(const Config& config);

// 설정된 마커 간격을 포함한 격자 높이를 mm 단위로 계산함.
// 바깥 여백은 포함하지 않고, 마커 사이 gap만 포함함.
double grid_height_mm(const Config& config);

}  // namespace homography
