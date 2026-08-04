#include "metadata_router.hpp"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& description) {
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << description << '\n';
    if (!condition) ++failures;
}

}  // namespace

int main() {
    const std::string arucoXml = R"xml(
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema"
                   xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2"
                   xmlns:tns1="http://www.onvif.org/ver10/topics">
  <tt:Event><wsnt:NotificationMessage>
    <wsnt:Topic>
      tns1:OpenApp/ArUCo_Detection/MarkerDetected
    </wsnt:Topic>
  </wsnt:NotificationMessage></tt:Event>
</tt:MetadataStream>)xml";

    const std::string objectXml = R"xml(
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
  <tt:VideoAnalytics><tt:Frame UtcTime="2026-08-04T00:00:00Z"/></tt:VideoAnalytics>
</tt:MetadataStream>)xml";

    check(classifyMetadata(arucoXml) == MetadataType::ArucoDetection,
          "ArUco Topic을 ArUco 파서 입력으로 분류");
    check(classifyMetadata(objectXml) == MetadataType::ObjectDetection,
          "VideoAnalytics를 기존 객체탐지 파서 입력으로 분류");
    check(classifyMetadata("<tt:MetadataStream xmlns:tt=\"urn:test\"/>") ==
              MetadataType::Unknown,
          "지원하지 않는 메타데이터는 Unknown");
    check(classifyMetadata("<broken>") == MetadataType::Unknown,
          "손상 XML은 Unknown");

    return failures == 0 ? 0 : 1;
}
