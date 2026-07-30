#include "metadata_format.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace MarkerMetadataFormat {
    
    // UTC 타임스탬프(ms)를 ONVIF UtcTime 속성 형식("YYYY-MM-DDTHH:MM:SS.mmmZ")으로 변환.
    std::string TimePointToString(uint64_t timestamp_ms)
    {
        // timestamp_ms : 1970-01-01 부터 흐른 밀리초 수
        // 표준 C시간 함수들은 다 초 단위(time_t)로 동작함. 그래서 1000으로 나눠서 정수 초 부분을, 나머지로 소수점 밀리초 부분을 따로 뽑음. 
        time_t sec = (time_t)(timestamp_ms / 1000);
        uint32_t msec = (uint32_t)(timestamp_ms % 1000);
        
        // ::gmtime((const time_t*)&sec) — sec처럼 그냥 정수 하나로 된 시간을 사람이 읽는 struct tm(연/월/일/시/분/초로 나뉜 구조체)으로 바꿔줍
        // 
        auto conv_time = ::gmtime((const time_t*)&sec);

        // <sstream>에 있는 타입.
        // std::cout처럼 << 연산자로 이것저것 이어 붙일 수 있는데 화면에 출력하는 대신 내부 버퍼에 문자열로 쌓아두는 객체.
        // 여러 조각을 이어 붙여서 문자열 하나를 완성하고 싶을 때 쓰는 도구이다.
        std::stringstream ss;

        // ONVIF가 요구하는 UtcTime 형식 "2026-07-22T14:36:05.007Z" 로 출력함.
        // 1. std::put_time(conv_time, "%FT%T.") : time_t 구조체의 요소들을 2026-07-22T14:36:05 형태로 출력
        // 2. std::setfill('0') : 이후에 쭉 빈 자리를 '0'으로 채우라는 설정
        // 3. std::setw(3) : 바로 다음 값 하나만 최소 3자리 폭으로 찍으라는 설정
        // 4. msec : 위의 설정한 값을 반영
        // 5. "Z" : "Z" (ISO8601에서 UTC를 뜻하는 리터럴 문자)
        ss << std::put_time(conv_time, "%FT%T.") << std::setfill('0') << std::setw(3) << msec << "Z";

        // 작업대 위 결과물을 진짜 std::string으로 꺼내기
        return ss.str();
    
    }

    std::string BuildMarkerMetadataXml(
        int channel,
        const std::vector<int>& ids,
        const std::vector<std::vector<cv::Point2f>>& corners,
        uint64_t timestamp_ms)
    {
        std::string utc_time = TimePointToString(timestamp_ms);
        // MarkerIds="7,12" 처럼 검출된 id를 콤마로 이어 붙인 문자열 하나로 만든다.
        std::ostringstream ids_csv;
        for (size_t i = 0; i < ids.size(); i++) {
            if (i > 0) ids_csv << ",";
            ids_csv << ids[i];
        }

        // 마커별로 "Marker{i}Corners" SimpleItem을 하나씩 만든다.
        // corners[i]는 4개의 (x, y) 점이라, "x,y,x,y,x,y,x,y" 형태로 이어 붙인다.
        // 
        // ids[i]와 corners[i]는 detectMarkers()가 같은 인덱스로 짝지어 반환하므로,
        // MarkerIds의 i번째 값과 Marker{i}Corners가 그대로 대응된다.
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

        // ONVIF WS-Notification 이벤트(tt:Event) 형식으로 감싼다.
        // tt:VideoAnalytics 형식으로 주면 카메라 메타데이터 먹서가 인식하지 못하고 버린다.
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        xml << "<tt:MetadataStream xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
                "xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\" "
                "xmlns:tns1=\"http://www.onvif.org/ver10/topics\">";
        xml << "<tt:Event><wsnt:NotificationMessage>";
        xml << "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                "tns1:OpenApp/ArUCo_Detection/MarkerDetected</wsnt:Topic>";
        xml << "<wsnt:Message><tt:Message UtcTime=\"" << utc_time << "\">";

        // Source = 이 이벤트를 발생시킨 대상. "어느 채널 카메라에서 검출됐는지"를 표시.
        xml << "<tt:Source><tt:SimpleItem Name=\"Channel\" Value=\"" << channel << "\"/></tt:Source>";

        // ONVIF 스키마상 필수 요소라 태그만 넣음 (이 앱은 프로퍼티 상태 키를 안 씀).
        xml << "<tt:Key></tt:Key>";
        xml << "<tt:Data>"
            << "<tt:SimpleItem Name=\"MarkerCount\" Value=\"" << ids.size() << "\"/>"
            << "<tt:SimpleItem Name=\"MarkerIds\" Value=\"" << ids_csv.str() << "\"/>"
            << marker_corner_items.str()
            << "</tt:Data>";
        xml << "</tt:Message></wsnt:Message></wsnt:NotificationMessage></tt:Event></tt:MetadataStream>";
        return xml.str();
    }
}   // namespace MarkerMetadataFormat