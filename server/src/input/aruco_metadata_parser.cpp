#include "input/aruco_metadata_parser.hpp"

#include <pugixml.hpp>

#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool matchesLocalName(const pugi::xml_node& node, const char* localName) {
    const char* fullName = node.name();
    const char* colon = std::strrchr(fullName, ':');
    const char* actual = colon ? colon + 1 : fullName;
    return std::strcmp(actual, localName) == 0;
}

pugi::xml_node findChild(const pugi::xml_node& parent, const char* localName) {
    for (const auto& child : parent.children()) {
        if (matchesLocalName(child, localName)) return child;
    }
    return {};
}

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> simpleItemValue(
    const pugi::xml_node& parent,
    const char* name) {
    for (const auto& child : parent.children()) {
        if (!matchesLocalName(child, "SimpleItem")) continue;
        if (std::strcmp(child.attribute("Name").as_string(), name) == 0) {
            return std::string(child.attribute("Value").as_string());
        }
    }
    return std::nullopt;
}

bool parseInt(const std::string& text, int& output) {
    const std::string value = trim(text);
    if (value.empty()) return false;
    std::size_t used = 0;
    try {
        output = std::stoi(value, &used);
    } catch (const std::exception&) {
        return false;
    }
    return used == value.size();
}

bool parseIds(const std::string& csv, std::vector<int>& output) {
    output.clear();
    if (trim(csv).empty()) return true;

    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        int id = -1;
        if (!parseInt(token, id) || id < 0) return false;
        output.push_back(id);
    }
    return true;
}

bool parseCoordinates(const std::string& csv, std::vector<double>& output) {
    output.clear();
    std::stringstream stream(csv);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trim(token);
        if (token.empty()) return false;
        std::size_t used = 0;
        double coordinate = 0.0;
        try {
            coordinate = std::stod(token, &used);
        } catch (const std::exception&) {
            return false;
        }
        if (used != token.size() || !std::isfinite(coordinate)) return false;
        output.push_back(coordinate);
    }
    return output.size() == 8;
}

}  // namespace

std::optional<ArucoFrame> parseArucoMetadata(const std::string& xml) {
    pugi::xml_document document;
    if (!document.load_string(xml.c_str())) return std::nullopt;

    const pugi::xml_node root = document.root().first_child();
    const pugi::xml_node event = findChild(root, "Event");
    const pugi::xml_node notification = findChild(event, "NotificationMessage");
    if (!notification) return std::nullopt;

    const pugi::xml_node topicNode = findChild(notification, "Topic");
    if (!topicNode || trim(topicNode.text().as_string()) != kArucoMarkerDetectedTopic) {
        return std::nullopt;
    }

    const pugi::xml_node wrapper = findChild(notification, "Message");
    const pugi::xml_node message = findChild(wrapper, "Message");
    if (!message || !message.attribute("UtcTime")) return std::nullopt;

    ArucoFrame frame;
    frame.utcTime = message.attribute("UtcTime").as_string();
    if (frame.utcTime.empty()) return std::nullopt;

    const pugi::xml_node source = findChild(message, "Source");
    const auto channelText = simpleItemValue(source, "Channel");
    if (!channelText || !parseInt(*channelText, frame.channel) || frame.channel < 1) {
        return std::nullopt;
    }

    const pugi::xml_node data = findChild(message, "Data");
    const auto countText = simpleItemValue(data, "MarkerCount");
    const auto idsText = simpleItemValue(data, "MarkerIds");
    int markerCount = -1;
    std::vector<int> ids;
    if (!countText || !parseInt(*countText, markerCount) || markerCount < 0 ||
        !idsText || !parseIds(*idsText, ids) ||
        static_cast<int>(ids.size()) != markerCount) {
        return std::nullopt;
    }

    frame.markers.reserve(ids.size());
    for (int index = 0; index < markerCount; ++index) {
        const std::string key = "Marker" + std::to_string(index) + "Corners";
        const auto cornersText = simpleItemValue(data, key.c_str());
        std::vector<double> coordinates;
        if (!cornersText || !parseCoordinates(*cornersText, coordinates)) {
            return std::nullopt;
        }

        ArucoMarker marker;
        marker.id = ids[static_cast<std::size_t>(index)];
        for (int corner = 0; corner < 4; ++corner) {
            marker.corners[corner] = {
                coordinates[static_cast<std::size_t>(corner * 2)],
                coordinates[static_cast<std::size_t>(corner * 2 + 1)],
            };
        }
        frame.markers.push_back(marker);
    }

    return frame;
}
