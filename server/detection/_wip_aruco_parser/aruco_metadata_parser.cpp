// aruco_metadata_parser.cpp
#include "aruco_metadata_parser.hpp"
#include <pugixml.hpp>
#include <cstring>
#include <sstream>

// ── 태그 매칭 헬퍼 ──────────────────────────────────────────────
// ONVIF XML 은 스트림/문서마다 "tt:", "tns1:", "wsnt:" 같은 네임스페이스
// 접두어가 다르게 붙을 수 있어, 접두어를 무시하고 로컬 태그명만 비교한다.
// (object_detection/onvif_metadata_parser.cpp 와 동일한 방식)
static bool matchesLocalName(const pugi::xml_node& node, const char* localName) {
    const char* fullName = node.name();
    const char* colon = std::strrchr(fullName, ':');
    const char* actual = colon ? colon + 1 : fullName;
    return std::strcmp(actual, localName) == 0;
}

static pugi::xml_node findChild(const pugi::xml_node& parent, const char* localName) {
    for (auto child : parent.children()) {
        if (matchesLocalName(child, localName)) return child;
    }
    return pugi::xml_node();
}

// ── SimpleItem 추출 ─────────────────────────────────────────────
// <tt:SimpleItem Name="..." Value="..."/> 중 Name 이 일치하는 것의 Value 를 돌려준다.
// 없으면 빈 문자열.
static std::string simpleItemValue(const pugi::xml_node& parent, const char* name) {
    for (auto child : parent.children()) {
        if (!matchesLocalName(child, "SimpleItem")) continue;
        if (std::strcmp(child.attribute("Name").as_string(), name) == 0) {
            return child.attribute("Value").as_string();
        }
    }
    return std::string();
}

// "40,38,24" 같은 콤마 구분 문자열을 double 리스트로 파싱한다.
static std::vector<double> splitDoubles(const std::string& csv) {
    std::vector<double> out;
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (token.empty()) continue;
        try {
            out.push_back(std::stod(token));
        } catch (const std::exception&) {
            // 숫자가 아닌 토큰은 건너뛴다 (손상된 프레임 방어).
        }
    }
    return out;
}

ArucoFrame parseArucoMessage(const pugi::xml_node& messageNode) {
    ArucoFrame frame;
    if (!messageNode) return frame;

    if (messageNode.attribute("UtcTime")) {
        frame.utcTime = messageNode.attribute("UtcTime").as_string();
    }

    // --- Source: Channel ---
    pugi::xml_node source = findChild(messageNode, "Source");
    if (source) {
        std::string ch = simpleItemValue(source, "Channel");
        if (!ch.empty()) {
            try { frame.channel = std::stoi(ch); } catch (const std::exception&) {}
        }
    }

    // --- Data: MarkerCount / MarkerIds / Marker{N}Corners ---
    pugi::xml_node data = findChild(messageNode, "Data");
    if (!data) return frame;

    int markerCount = 0;
    {
        std::string mc = simpleItemValue(data, "MarkerCount");
        if (!mc.empty()) {
            try { markerCount = std::stoi(mc); } catch (const std::exception&) {}
        }
    }

    // MarkerIds: "40,38,24" → 정수 리스트
    std::vector<int> ids;
    for (double v : splitDoubles(simpleItemValue(data, "MarkerIds"))) {
        ids.push_back(static_cast<int>(v));
    }

    // MarkerCount 가 없거나 0 이면 MarkerIds 개수로 대체한다 (둘 중 신뢰 가능한 값 사용).
    if (markerCount <= 0) markerCount = static_cast<int>(ids.size());

    for (int n = 0; n < markerCount; ++n) {
        ArucoMarker marker;
        marker.id = (n < static_cast<int>(ids.size())) ? ids[n] : -1;

        // Marker{N}Corners: "x0,y0,x1,y1,x2,y2,x3,y3" → 코너 4개
        std::string key = "Marker" + std::to_string(n) + "Corners";
        std::vector<double> coords = splitDoubles(simpleItemValue(data, key.c_str()));
        if (coords.size() >= 8) {
            for (int c = 0; c < 4; ++c) {
                marker.corners[c] = { coords[c * 2], coords[c * 2 + 1] };
            }
        }
        // 코너가 8개 미만이면 기본값(0,0)으로 둔다 — 상위에서 걸러낼 수 있게 id 는 유지.

        frame.markers.push_back(marker);
    }

    return frame;
}

std::optional<ArucoFrame> parseArucoMetadata(const std::string& xml) {
    pugi::xml_document doc;
    if (!doc.load_string(xml.c_str())) {
        // 파싱 실패 시 값 없음 반환 — 호출부에서 로그 처리 권장.
        return std::nullopt;
    }

    pugi::xml_node root = doc.root().first_child();           // MetadataStream
    pugi::xml_node event = findChild(root, "Event");
    pugi::xml_node notif = event ? findChild(event, "NotificationMessage")
                                 : pugi::xml_node();
    if (!notif) return std::nullopt;

    // --- 토픽 분기: "MarkerDetected" 포함 여부 확인 ---
    pugi::xml_node topic = findChild(notif, "Topic");
    std::string topicText = topic ? topic.text().as_string() : std::string();
    if (topicText.find("MarkerDetected") == std::string::npos) {
        // ArUco 마커 이벤트가 아님 (사람 검출 등 다른 토픽) — 무시.
        return std::nullopt;
    }

    // <wsnt:Message> 래퍼 안의 <tt:Message> 를 찾아 파서에 넘긴다.
    pugi::xml_node wsntMessage = findChild(notif, "Message");
    pugi::xml_node ttMessage = wsntMessage ? findChild(wsntMessage, "Message")
                                           : pugi::xml_node();
    if (!ttMessage) return std::nullopt;

    return parseArucoMessage(ttMessage);
}
