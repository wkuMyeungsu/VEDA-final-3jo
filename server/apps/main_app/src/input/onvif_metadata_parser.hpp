// onvif_metadata_parser.hpp
#pragma once
#include <string>
#include <vector>
#include <optional>

struct BoundingBox {
    float left = 0.f, top = 0.f, right = 0.f, bottom = 0.f;
    // 카메라 원본 해상도(2592x1520) 기준 픽셀 좌표 — attributes.cgi ROICoordinate 범위와 일치
    float width()  const { return right - left; }
    float height() const { return bottom - top; }
    float centerX() const { return (left + right) / 2.f; }
    float centerY() const { return (top + bottom) / 2.f; }
    float groundX() const { return (left + right) / 2.f; }
    float groundY() const { return bottom; }
};

struct ClassInfo {
    std::string type;      // "Human", "Vehicle", "Face", "LicensePlate", "Head", "Unknown"
    float likelihood = 0.f; // 0.0 ~ 1.0
};

struct ProximateObject {
    int id = -1;
    float distance = 0.f;  // 단위 미확인 — 실측 필요
};

struct DetectedObject {
    int objectId = -1;
    int parentId = -1;     // Parent (있으면 상위 객체, 예: HumanFace의 부모가 HumanBody)
    BoundingBox bbox;
    std::optional<std::pair<float,float>> centerOfGravity;
    ClassInfo classInfo;
    std::vector<ProximateObject> proximateObjects;
};

struct MetadataFrame {
    std::string utcTime;               // Frame 단위 타임스탬프 — PTS 동기화에 사용
    std::string serverReceivedUtc;
    double deltaMs = 0.0;
    std::string stream_id;
    std::string camera_id;
    int channel = -1;
    std::vector<DetectedObject> objects;
};

// 하나의 <tt:MetadataStream> XML을 파싱
MetadataFrame parseOnvifMetadata(const std::string& xml);
