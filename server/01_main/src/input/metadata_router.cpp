#include "input/metadata_router.hpp"

#include <pugixml.hpp>

#include <cctype>
#include <cstring>
#include <string>

#include "input/aruco_metadata_parser.hpp"

namespace {

bool matchesLocalName(const pugi::xml_node& node, const char* localName) {
    const char* fullName = node.name();
    const char* colon = std::strrchr(fullName, ':');
    return std::strcmp(colon ? colon + 1 : fullName, localName) == 0;
}

pugi::xml_node findDescendant(const pugi::xml_node& parent, const char* localName) {
    for (const auto& child : parent.children()) {
        if (matchesLocalName(child, localName)) return child;
        const auto found = findDescendant(child, localName);
        if (found) return found;
    }
    return {};
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

MetadataType classifyMetadata(const std::string& xml) {
    pugi::xml_document document;
    if (!document.load_string(xml.c_str())) return MetadataType::Unknown;

    const auto root = document.document_element();
    if (!root) return MetadataType::Unknown;

    // ArUco 이벤트는 계약에 명시된 정확한 Topic으로 분기한다. 잘못된 ArUco
    // 메시지가 객체탐지 파서로 흘러가지 않도록 VideoAnalytics보다 먼저 검사한다.
    const auto topic = findDescendant(root, "Topic");
    if (topic && trim(topic.text().as_string()) == kArucoMarkerDetectedTopic) {
        return MetadataType::ArucoDetection;
    }

    // 기존 객체탐지 메타데이터에는 Event Topic이 없으므로 VideoAnalytics 구조로 판별한다.
    if (matchesLocalName(root, "VideoAnalytics") ||
        findDescendant(root, "VideoAnalytics")) {
        return MetadataType::ObjectDetection;
    }

    return MetadataType::Unknown;
}
