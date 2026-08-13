#pragma once

#include "homography/types.hpp"

#include "nlohmann/json.hpp"

#include <string>

// 결과와 설정을 JSON으로 변환함.
namespace homography {

using json = nlohmann::json;

// CV_64F 행렬을 행 우선 숫자 배열로 변환함.
json matrix_to_json(const cv::Mat& matrix);

// 3×3 숫자 배열을 CV_64F 행렬로 복원함.
cv::Mat json_to_matrix(const json& value);

// 현재 시각을 UTC ISO-8601 문자열로 반환함.
std::string utc_now();

// 설정을 결과 JSON에 넣을 객체로 변환함.
json config_to_json(const Config& config);

// 호모그래피 결과 JSON 읽음.
json read_homography(const std::string& path);

}  // namespace homography
