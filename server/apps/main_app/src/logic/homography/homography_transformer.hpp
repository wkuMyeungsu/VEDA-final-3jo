#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>

#include "common/types.hpp"
#include "config_loader/safety_server_config.hpp"

namespace forklift::logic {

// 전역 stream_id별 H를 기동 시 한 번만 검증해 캐시하고, 실시간 프레임에서는
// 행렬 계산만 수행한다. 잘못된 H는 항등행렬로 대체하지 않고 해당 stream_id를
// 로드 실패 상태로 남긴다.
class HomographyTransformer {
public:
    struct StreamHomography {
        std::array<double, 9> h{};      // 카메라 픽셀 -> 지도(mm)
        std::array<double, 9> h_inv{};  // 지도(mm) -> 카메라 픽셀 (FOV 판정용)
        int image_width_px = 0;
        int image_height_px = 0;
        bool lens_undistorted = false;   // true면 pixelToWorld 입력에 왜곡 역산 적용
        // 이 스트림의 캘리브레이션 산출물(스트림 전용 파일 우선, 공용 폴백).
        struct Intrinsics {
            double fx = 0, fy = 0, cx = 0, cy = 0;
            double k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0;
            bool valid = false;
        } intrinsics;
    };

    explicit HomographyTransformer(const config::SafetyServerConfig& config);
    bool hasStream(const std::string& stream_id) const;
    // 동차좌표 분모가 0에 가깝거나 계산 결과가 NaN/Inf이면 위치 미확정을 반환한다.
    std::optional<common::WorldPoint> pixelToWorld(const std::string& stream_id,
                                                   const common::PixelPoint& pixel) const;
    // 지도(mm) 좌표를 해당 스트림의 카메라 픽셀로 되돌린다(핸드오버 FOV 판정용).
    std::optional<common::PixelPoint> worldToPixel(const std::string& stream_id,
                                                   const common::WorldPoint& world) const;
    std::optional<std::pair<int, int>> imageSize(const std::string& stream_id) const;
    const std::map<std::string, std::string>& streamLoadErrors() const { return stream_load_errors_; }

private:
    std::map<std::string, StreamHomography> streams_;
    std::map<std::string, std::string> stream_load_errors_;
};

}  // namespace forklift::logic
