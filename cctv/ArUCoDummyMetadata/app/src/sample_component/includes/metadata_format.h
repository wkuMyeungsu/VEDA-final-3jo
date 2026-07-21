#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace MarkerMetadataFormat {

// 이 더미 앱은 실제 이미지 처리를 하지 않으므로 OpenCV의 cv::Point2f 대신
// 이 최소 구조체만 사용 (ArUco_Detection과 XML 포맷만 동일하게 맞추면 됨).
struct Point2f {
  float x = 0.f;
  float y = 0.f;
};

// UTC 타임스탬프(ms)를 ONVIF UtcTime 속성 형식("YYYY-MM-DDTHH:MM:SS.mmmZ")으로 변환
std::string TimePointToString(uint64_t timestamp_ms);

// 검출된 마커 id/코너를 ONVIF WS-Notification 이벤트 XML로 직렬화.
// MarkerIds의 i번째 값과 "Marker{i}Corners" SimpleItem이 인덱스로 대응됨.
// (eRequestRawMetadata/Dynamic Metadata는 tt:VideoAnalytics가 아니라 이 tt:Event 형식이어야
// 카메라 메타데이터 먹서가 인식/전달함 — SDK API 문서 3.2.2.3 Dynamic Metadata 예제 기준)
std::string BuildMarkerMetadataXml(int channel, const std::vector<int>& ids,
                                    const std::vector<std::vector<Point2f>>& corners, uint64_t timestamp_ms);

}  // namespace MarkerMetadataFormat
