#include "logging/aruco_csv_logger.hpp"
#include "input/aruco_metadata_parser.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& description) {
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << description << '\n';
    if (!condition) ++failures;
}

void checkClose(double actual, double expected, const std::string& description) {
    check(std::fabs(actual - expected) < 1e-6, description);
}

std::string replaceOnce(std::string text, const std::string& from, const std::string& to) {
    const std::size_t position = text.find(from);
    if (position == std::string::npos) return text;
    text.replace(position, from.size(), to);
    return text;
}

const std::string validXml = R"xml(
<?xml version="1.0" encoding="UTF-8"?>
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema"
                   xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2"
                   xmlns:tns1="http://www.onvif.org/ver10/topics">
  <tt:Event>
    <wsnt:NotificationMessage>
      <wsnt:Topic Dialect="http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet">
        tns1:OpenApp/ArUCo_Detection/MarkerDetected
      </wsnt:Topic>
      <wsnt:Message>
        <tt:Message UtcTime="2026-08-04T12:34:56.789Z">
          <tt:Source><tt:SimpleItem Name="Channel" Value="1"/></tt:Source>
          <tt:Key/>
          <tt:Data>
            <tt:SimpleItem Name="MarkerCount" Value="2"/>
            <tt:SimpleItem Name="MarkerIds" Value="40,38"/>
            <tt:SimpleItem Name="Marker0Corners" Value="120.5,80.0,180.0,82.0,178.0,142.5,119.0,140.0"/>
            <tt:SimpleItem Name="Marker1Corners" Value="310.0,200.0,370.0,201.0,368.0,260.0,309.0,258.0"/>
          </tt:Data>
        </tt:Message>
      </wsnt:Message>
    </wsnt:NotificationMessage>
  </tt:Event>
</tt:MetadataStream>
)xml";

}  // namespace

int main() {
    const auto parsed = parseArucoMetadata(validXml);
    check(parsed.has_value(), "실제 ArUco 토픽 파싱");
    if (!parsed) return 1;

    check(parsed->utcTime == "2026-08-04T12:34:56.789Z", "카메라 UtcTime 보존");
    check(parsed->channel == 1, "1부터 시작하는 Channel 파싱");
    check(parsed->markers.size() == 2, "MarkerCount=2");
    check(parsed->markers[0].id == 40, "MarkerIds[0]과 Marker0Corners 대응");
    check(parsed->markers[1].id == 38, "MarkerIds[1]과 Marker1Corners 대응");
    checkClose(parsed->markers[0].corners[0].x, 120.5, "첫 코너 x 순서 보존");
    checkClose(parsed->markers[0].corners[3].y, 140.0, "마지막 코너 y 순서 보존");

    std::string emptyXml = replaceOnce(validXml, "MarkerCount\" Value=\"2", "MarkerCount\" Value=\"0");
    emptyXml = replaceOnce(emptyXml, "MarkerIds\" Value=\"40,38", "MarkerIds\" Value=\"");
    emptyXml = replaceOnce(emptyXml,
        "            <tt:SimpleItem Name=\"Marker0Corners\" Value=\"120.5,80.0,180.0,82.0,178.0,142.5,119.0,140.0\"/>\n", "");
    emptyXml = replaceOnce(emptyXml,
        "            <tt:SimpleItem Name=\"Marker1Corners\" Value=\"310.0,200.0,370.0,201.0,368.0,260.0,309.0,258.0\"/>\n", "");
    const auto emptyFrame = parseArucoMetadata(emptyXml);
    check(emptyFrame && emptyFrame->markers.empty(), "MarkerCount=0은 정상 미검출");

    check(!parseArucoMetadata(replaceOnce(validXml,
        kArucoMarkerDetectedTopic, "tns1:OpenApp/Other/MarkerDetected")),
        "다른 토픽 거부");
    check(!parseArucoMetadata(replaceOnce(validXml,
        "Channel\" Value=\"1", "Channel\" Value=\"0")),
        "Channel=0 거부");
    check(!parseArucoMetadata(replaceOnce(validXml,
        "MarkerCount\" Value=\"2", "MarkerCount\" Value=\"1")),
        "MarkerCount와 MarkerIds 불일치 거부");
    check(!parseArucoMetadata(replaceOnce(validXml,
        "120.5,80.0,180.0,82.0,178.0,142.5,119.0,140.0", "120.5,80.0")),
        "코너 좌표가 8개가 아니면 거부");
    check(!parseArucoMetadata("<broken>"), "손상 XML 거부");

    const std::string csvPath = "aruco_markers_test.csv";
    std::remove(csvPath.c_str());
    {
        ArucoCsvLogger logger(csvPath);
        logger.logFrame(*parsed);
    }
    std::ifstream csv(csvPath);
    std::string header;
    std::string firstRow;
    std::getline(csv, header);
    std::getline(csv, firstRow);
    check(firstRow.rfind("2026-08-04T12:34:56.789Z,,0,1,40,", 0) == 0,
          "CSV에 camera/server 시각, channel, marker ID 기록");
    csv.close();
    std::remove(csvPath.c_str());

    std::cout << "\n=== " << (failures == 0 ? "ALL PASSED" : "FAILED")
              << " (실패 " << failures << "건) ===\n";
    return failures == 0 ? 0 : 1;
}
