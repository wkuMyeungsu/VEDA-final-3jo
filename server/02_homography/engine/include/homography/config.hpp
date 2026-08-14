#pragma once

#include "homography/types.hpp"

#include <opencv2/aruco.hpp>

#include <string>
#include <vector>

// 설정 파일 해석 및 보드 좌표 계산함.
namespace homography {

// JSON 설정 파일을 읽고 필수 항목과 값의 범위를 검증함.
// 모든 길이 필드는 mm로 읽으며, 누락되거나 잘못된 설정은 오류 처리함.
Config read_config(const std::string& path);

// 설정에 지정된 이름의 OpenCV ArUco 사전 반환함.
// 지원하지 않는 사전 이름은 대체하지 않고 오류 처리함.
cv::Ptr<cv::aruco::Dictionary> dictionary(const Config& config);

}  // namespace homography
