#pragma once

#include "homography/types.hpp"

#include "nlohmann/json.hpp"

#include <string>

// 결과를 JSON으로 변환함.
namespace homography {

using json = nlohmann::json;

// CV_64F 행렬을 행 우선 숫자 배열로 변환함.
json matrix_to_json(const cv::Mat& matrix);

// 3×3 숫자 배열을 CV_64F 행렬로 복원함.
cv::Mat json_to_matrix(const json& value);

// 현재 시각을 UTC ISO-8601 문자열로 반환함.
std::string utc_now();

}  // namespace homography
