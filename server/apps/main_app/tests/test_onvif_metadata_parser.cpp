#include "input/onvif_metadata_parser.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& description) {
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << description << '\n';
    if (!condition) ++failures;
}

void checkClose(float actual, float expected, const std::string& description) {
    check(std::fabs(actual - expected) < 1e-5f, description);
}

const std::string kValidXml = R"xml(
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema"
                   xmlns:wsnt="http://docs.oasis-open.org/wsn/b-2"
                   xmlns:tns1="http://www.onvif.org/ver10/topics">
  <tt:VideoAnalytics>
    <tt:Frame UtcTime="2026-07-13T09:00:00Z">
      <tt:Object ObjectId="11">
        <tt:Appearance>
          <tt:Shape>
            <tt:BoundingBox left="907.0" top="1202.0" right="1438.0" bottom="1490.0"/>
            <tt:CenterOfGravity x="1172.5" y="1346.0"/>
          </tt:Shape>
          <tt:Class>
            <tt:Type Likelihood="0.8">Human</tt:Type>
          </tt:Class>
          <tt:ProximateObjects>
            <tt:ProximateObject Id="12" Distance="240.5"/>
          </tt:ProximateObjects>
        </tt:Appearance>
      </tt:Object>
      <tt:Object ObjectId="12">
        <tt:Appearance>
          <tt:Shape>
            <tt:BoundingBox left="1500.0" top="900.0" right="2100.0" bottom="1400.0"/>
          </tt:Shape>
          <tt:Class>
            <tt:Type Likelihood="0.93">Vehicle</tt:Type>
          </tt:Class>
        </tt:Appearance>
      </tt:Object>
    </tt:Frame>
  </tt:VideoAnalytics>
</tt:MetadataStream>
)xml";

}  // namespace

int main() {
    const MetadataFrame frame = parseOnvifMetadata(kValidXml);
    check(frame.utcTime == "2026-07-13T09:00:00Z", "Frame UtcTime을 보존함");
    check(frame.objects.size() == 2, "Frame 안의 객체 2개를 읽음");

    if (frame.objects.size() == 2) {
        const auto& human = frame.objects[0];
        check(human.objectId == 11, "첫 객체 ObjectId=11");
        check(human.classInfo.type == "Human", "직접 Type의 클래스명 Human을 읽음");
        checkClose(human.classInfo.likelihood, 0.8f, "Human likelihood=0.8");
        checkClose(human.bbox.left, 907.0f, "BoundingBox left를 보존함");
        checkClose(human.bbox.bottom, 1490.0f, "BoundingBox bottom을 보존함");
        checkClose(human.bbox.centerX(), 1172.5f, "BoundingBox 중심 X를 계산함");
        checkClose(human.bbox.groundY(), 1490.0f, "BoundingBox bottom을 ground Y로 계산함");
        check(human.centerOfGravity.has_value(), "CenterOfGravity를 읽음");
        if (human.centerOfGravity) {
            checkClose(human.centerOfGravity->first, 1172.5f, "CenterOfGravity X를 보존함");
            checkClose(human.centerOfGravity->second, 1346.0f, "CenterOfGravity Y를 보존함");
        }
        check(human.proximateObjects.size() == 1, "ProximateObject 1개를 읽음");
        if (human.proximateObjects.size() == 1) {
            check(human.proximateObjects[0].id == 12, "ProximateObject Id=12");
            checkClose(human.proximateObjects[0].distance, 240.5f,
                       "ProximateObject Distance=240.5");
        }

        const auto& vehicle = frame.objects[1];
        check(vehicle.objectId == 12, "둘째 객체 ObjectId=12");
        check(vehicle.classInfo.type == "Vehicle", "둘째 객체 클래스명 Vehicle");
        check(!vehicle.centerOfGravity.has_value(), "CenterOfGravity가 없으면 optional이 비어 있음");
        check(vehicle.proximateObjects.empty(), "ProximateObjects가 없으면 목록이 비어 있음");
    }

    const std::string prefixlessXml = R"xml(
<MetadataStream>
  <VideoAnalytics>
    <Frame UtcTime="2026-07-13T09:01:00Z">
      <Object ObjectId="21"><Appearance><Class><Type Likelihood="0.5">Human</Type></Class></Appearance></Object>
    </Frame>
  </VideoAnalytics>
</MetadataStream>
)xml";
    const MetadataFrame prefixless = parseOnvifMetadata(prefixlessXml);
    check(prefixless.objects.size() == 1 && prefixless.objects[0].objectId == 21,
          "네임스페이스 접두어가 없는 ONVIF XML도 읽음");

    const MetadataFrame missingFrame = parseOnvifMetadata(
        "<MetadataStream><VideoAnalytics/></MetadataStream>");
    check(missingFrame.utcTime.empty() && missingFrame.objects.empty(),
          "Frame이 없으면 빈 메타데이터를 반환함");

    const MetadataFrame malformed = parseOnvifMetadata("<broken>");
    check(malformed.utcTime.empty() && malformed.objects.empty(),
          "손상된 XML은 빈 메타데이터를 반환함");

    std::cout << "\n=== "
              << (failures == 0 ? "ALL PASSED" : "FAILED")
              << " (실패 " << failures << "건) ===\n";
    return failures == 0 ? 0 : 1;
}
