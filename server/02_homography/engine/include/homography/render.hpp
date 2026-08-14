#pragma once

#include "homography/types.hpp"

#include <opencv2/core.hpp>

#include <string>

// ArUco 개별 마커 출력함.
namespace homography {

// 개별 마커를 SVG로 출력함. size와 margin은 mm 단위.
void write_marker_svg(const std::string& path, const Config& config, int id,
                      double size_mm, double margin_mm,
                      const std::string& label);

// 개별 마커를 PNG로 출력함. size와 margin은 mm 단위.
void write_marker_png(const std::string& path, const Config& config, int id,
                      double size_mm, double margin_mm, double dpi,
                      const std::string& label);

}  // namespace homography
