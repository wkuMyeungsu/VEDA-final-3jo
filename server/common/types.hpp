#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace forklift::common {

struct PixelPoint {
    double x = 0.0;
    double y = 0.0;
};

struct WorldPoint {
    double x = 0.0;
    double y = 0.0;
};

struct ArucoMarker {
    int marker_id = -1;
    // 카메라가 보낸 코너 순서를 그대로 보존한다.
    std::array<PixelPoint, 4> corners{};    
};

struct ArucoFrame {
    // 카메라 앱 기준 채널 번호. 송신 계약상 1부터 시작하며, -1은 파싱 실패/미설정이다.
    int channel = -1;

    // 카메라 송신 시각과 서버 XML 수신 시각 (UTC)
    std::string camera_utc_time;
    std::string server_received_utc_time;

    // 비어 있으면 해당 시점에 검출된 마커가 없다는 뜻이다.
    std::vector<ArucoMarker> markers;

    std::size_t markerCount() const noexcept { return markers.size(); }
};

}  // namespace forklift::common

