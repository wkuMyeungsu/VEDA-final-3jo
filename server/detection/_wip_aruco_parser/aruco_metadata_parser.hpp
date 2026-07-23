// aruco_metadata_parser.hpp
//
// ONVIF Profile M 메타데이터 중 ArUco 마커 검출 이벤트 파서 (WIP — 위치 미확정).
//   신규 토픽: tns1:OpenApp/ArUCoDummyMetadata/MarkerDetected
//
// 순수 파싱 로직만 담는다 (RTSP 수신 없음).
//   입력: <tt:Message> XML 노드 (또는 전체 <tt:MetadataStream> 문자열)
//   출력: 마커 리스트를 담은 ArucoFrame
//
// object_detection/ 의 onvif_metadata_parser 스타일(네임스페이스 무시 태그 매칭,
// pugixml SimpleItem 추출)을 참고했다. 코드는 이 폴더에만 독립적으로 둔다.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <optional>

// pugixml 헤더 노출을 피하려고 전방 선언만 한다 (참조 파라미터에만 사용).
namespace pugi { class xml_node; }

struct ArucoMarker {
    int id = -1;
    // 코너 4개. 카메라 원본 해상도 기준 픽셀 좌표.
    // 순서는 ArUco 표준(TL, TR, BR, BL)으로 가정한다.
    //   *** 확인 필요: 실제 카메라가 발행하는 코너 순서가 표준과 같은지 미검증 ***
    std::pair<double, double> corners[4];
};

struct ArucoFrame {
    std::string utcTime;              // <tt:Message UtcTime="...">
    int channel = -1;                 // <tt:Source> 의 SimpleItem Name="Channel"
    std::vector<ArucoMarker> markers;
};

// <tt:Message> 노드 하나를 파싱해 마커 리스트(ArucoFrame)를 반환한다.
// Data 아래 SimpleItem 에서 MarkerCount / MarkerIds / Marker{N}Corners 를 추출한다.
ArucoFrame parseArucoMessage(const pugi::xml_node& messageNode);

// 전체 <tt:MetadataStream> XML 문자열을 파싱한다.
//   토픽 분기: <wsnt:Topic> 텍스트에 "MarkerDetected" 가 포함될 때만 파싱한다.
//   해당 토픽이 아니면 std::nullopt 를 반환한다 (사람 검출 등 다른 이벤트는 무시).
std::optional<ArucoFrame> parseArucoMetadata(const std::string& xml);
