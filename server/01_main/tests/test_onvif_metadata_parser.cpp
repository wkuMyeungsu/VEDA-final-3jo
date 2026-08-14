// test_main.cpp
#include "input/onvif_metadata_parser.hpp"
#include <iostream>

int main() {
    // Complete Guide 예시 + metaframecapability에서 확인한 필드 조합한 샘플
    // 실제 값 범위: 2592x1520 (attributes.cgi ROICoordinate 기준)
    std::string sampleXml = R"(
    <tt:MetadataStream>
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
    )";

    MetadataFrame frame = parseOnvifMetadata(sampleXml);

    std::cout << "UtcTime: " << frame.utcTime << "\n";
    std::cout << "Detected objects: " << frame.objects.size() << "\n\n";

    for (const auto& obj : frame.objects) {
        std::cout << "ObjectId=" << obj.objectId
                  << " Class=" << obj.classInfo.type
                  << " (" << obj.classInfo.likelihood << ")\n";
        std::cout << "  BBox: (" << obj.bbox.left << "," << obj.bbox.top
                  << ") - (" << obj.bbox.right << "," << obj.bbox.bottom << ")"
                  << "  center=(" << obj.bbox.centerX() << "," << obj.bbox.centerY() << ")\n";
        for (const auto& po : obj.proximateObjects) {
            std::cout << "  ProximateObject Id=" << po.id
                      << " Distance=" << po.distance << " mm\n";
        }
    }
    return 0;
}
