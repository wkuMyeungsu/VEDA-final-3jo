#pragma once

#include "homography/types.hpp"

#include <opencv2/aruco.hpp>

#include <string>
#include <vector>

// CLI 공통 인자 처리함.
namespace homography {

void print_usage();

// 옵션 값 반환함. 옵션이 없으면 fallback 반환함.
std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback = "");

// 문자열을 정수로 변환함. 실패하면 오류 발생함.
int parse_int(const std::string& value, const char* label);

// 문자열을 실수로 변환함. 실패하면 오류 발생함.
double parse_double(const std::string& value, const char* label);

}  // namespace homography
