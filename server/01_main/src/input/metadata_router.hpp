#pragma once

#include <string>

// 파서는 독립적으로 유지하고, 수신 완료된 XML이 어느 파서의 입력인지 판별만 한다.
enum class MetadataType {
    ObjectDetection,
    ArucoDetection,
    Unknown,
};

MetadataType classifyMetadata(const std::string& xml);
