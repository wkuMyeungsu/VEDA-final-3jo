#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <opencv2/core.hpp>

namespace MetadataXmlBuilder {
    std::string TimePointToString(uint64_t timestamp_ms);

    // 검출된 마커 id와 코너를 ONVIF WS-Notification 이벤트 XML로 직렬화
    // tt:VideoAnalytics가 아니라 tt:Event 형식이어야 카메라 메타데이터 먹서가 인식한다. (SDK 문서 3.2.2.3 Dynamic Metadata 예제 기준)
    // ids[i]와 "Marker{i}Corners" 는 같은 인덱스로 짝지어짐.
    std::string BuildMarkerMetadataXml(
        int channel, 
        const std::vector<int>& ids, 
        const std::vector<std::vector<cv::Point2f>>& corners, 
        uint64_t timestamp_ms
    );
}