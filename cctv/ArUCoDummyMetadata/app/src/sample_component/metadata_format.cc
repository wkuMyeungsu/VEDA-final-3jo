#include "metadata_format.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace MarkerMetadataFormat {

std::string TimePointToString(uint64_t timestamp_ms) {
  time_t sec = (time_t)(timestamp_ms / 1000);
  uint32_t msec = (uint32_t)(timestamp_ms % 1000);
  auto conv_time = ::gmtime((const time_t*)&sec);
  std::stringstream ss;
  ss << std::put_time(conv_time, "%FT%T.") << std::setfill('0') << std::setw(3) << msec << "Z";
  return ss.str();
}

std::string BuildMarkerMetadataXml(int channel, const std::vector<int>& ids,
                                    const std::vector<std::vector<Point2f>>& corners, uint64_t timestamp_ms) {
  std::string utc_time = TimePointToString(timestamp_ms);

  std::ostringstream ids_csv;
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i > 0) ids_csv << ",";
    ids_csv << ids[i];
  }

  // 마커별로 "Marker{인덱스}Corners"라는 별도 SimpleItem에 "x,y,x,y,x,y,x,y"(4점, 좌상단부터
  // 시계방향)로 담음. ids[i]와 corners[i]는 detectMarkers()가 같은 인덱스로 짝지어 반환하므로,
  // MarkerIds의 i번째 값과 Marker{i}Corners가 그대로 대응됨.
  std::ostringstream marker_corner_items;
  for (size_t i = 0; i < corners.size(); ++i) {
    const auto& quad = corners[i];
    std::ostringstream quad_csv;
    for (size_t j = 0; j < quad.size(); ++j) {
      if (j > 0) quad_csv << ",";
      quad_csv << quad[j].x << "," << quad[j].y;
    }
    marker_corner_items << "<tt:SimpleItem Name=\"Marker" << i << "Corners\" Value=\"" << quad_csv.str() << "\"/>";
  }

  // eRequestRawMetadata(Dynamic Metadata)는 tt:VideoAnalytics가 아니라
  // ONVIF WS-Notification 이벤트 형식(tt:Event/wsnt:NotificationMessage)이어야
  // 카메라 메타데이터 먹서가 인식/전달함 (SDK API 문서 3.2.2.3 Dynamic Metadata 예제 기준).
  std::ostringstream xml;
  xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
  xml << "<tt:MetadataStream xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
         "xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
         "xmlns:tns1=\"http://www.onvif.org/ver10/topics\">";
  xml << "<tt:Event><wsnt:NotificationMessage>";
  xml << "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
         "tns1:OpenApp/Dummy_ArUCo_Metadata/MarkerDetected</wsnt:Topic>";
  xml << "<wsnt:Message><tt:Message UtcTime=\"" << utc_time << "\">";
  xml << "<tt:Source><tt:SimpleItem Name=\"Channel\" Value=\"" << channel << "\"/></tt:Source>";
  xml << "<tt:Key></tt:Key>";
  xml << "<tt:Data>"
      << "<tt:SimpleItem Name=\"MarkerCount\" Value=\"" << ids.size() << "\"/>"
      << "<tt:SimpleItem Name=\"MarkerIds\" Value=\"" << ids_csv.str() << "\"/>"
      << marker_corner_items.str()
      << "</tt:Data>";
  xml << "</tt:Message></wsnt:Message></wsnt:NotificationMessage></tt:Event></tt:MetadataStream>";
  return xml.str();
}

}  // namespace MarkerMetadataFormat
