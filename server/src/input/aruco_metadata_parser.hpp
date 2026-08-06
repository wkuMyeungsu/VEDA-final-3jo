// aruco_metadata_parser.hpp
//
// ONVIF 메타데이터 중 카메라 앱의 ArUco 마커 검출 이벤트 파서.
//   토픽: tns1:OpenApp/ArUCo_Detection/MarkerDetected
//
// 순수 파싱 로직만 담는다 (RTSP 수신 없음).
//   입력: 전체 <tt:MetadataStream> XML 문자열
//   출력: 마커 리스트를 담은 ArucoFrame
//
// object_detection/ 의 onvif_metadata_parser 스타일(네임스페이스 무시 태그 매칭,
// pugixml SimpleItem 추출)을 참고했다. 코드는 이 폴더에만 독립적으로 둔다.
#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <vector>
#include <optional>

#include "common/types.hpp"

inline constexpr const char* kArucoMarkerDetectedTopic =
    "tns1:OpenApp/ArUCo_Detection/MarkerDetected";

struct ArucoMarker {
    int id = -1;
    // 코너 4개. 카메라 원본 해상도 기준 픽셀 좌표.
    // 카메라 앱이 보낸 x0,y0,...,x3,y3 순서를 재정렬하지 않고 그대로 보존한다.
    std::array<forklift::common::PixelPoint, 4> corners{};
};

struct ArucoFrame {
    std::string utcTime;              // <tt:Message UtcTime="...">
    std::string serverReceivedUtc;
    double deltaMs = 0.0;
    int channel = -1;                 // 앱 기준 1부터 시작하는 Channel
    std::vector<ArucoMarker> markers;

    std::size_t markerCount() const noexcept { return markers.size(); }
};

// 전체 <tt:MetadataStream> XML 문자열을 파싱한다.
// 토픽이 다르거나 필수 필드/숫자/마커 대응 관계가 잘못되면 nullopt를 반환한다.
// MarkerCount=0, MarkerIds=""는 정상적인 미검출 프레임으로 반환한다.
std::optional<ArucoFrame> parseArucoMetadata(const std::string& xml);
