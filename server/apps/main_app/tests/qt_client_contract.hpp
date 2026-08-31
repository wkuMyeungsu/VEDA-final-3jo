#pragma once

// Qt 단말/관제 핵심 로직을 Qt6 없이 그대로 재현한다.
// 분기·문자열·enum 순서는 아래 소스를 옮긴 것이다. 동작이 달라지면 안 된다.
//
//   forklift-device/qt/common/models/Types.cpp
//   forklift-device/qt/common/models/RiskMetadata.cpp
//   forklift-device/qt/qml/operator_terminal/RiskHud.qml
//   forklift-device/qt/qml/operator_terminal/EdgeWarningFrame.qml
//   forklift-device/qt/qml/components/RiskBanner.qml
//   forklift-device/qt/common/services/AlertListModel.cpp
//   forklift-device/qt/apps/operator_terminal/ActiveCameraController.cpp
//   operator-device/qt/qml/components/RiskBanner.qml
//   operator-device/qt/qml/control_center/AlertListView.qml

#include <string>

#include <nlohmann/json.hpp>

namespace qt_client {

// Types.h RiskTypes::RiskLevel / ExceptionState 선언 순서와 같다.
enum class RiskLevel { Safe = 0, Caution = 1, Danger = 2, Emergency = 3 };
enum class ExceptionState {
    None = 0,
    SensorFault = 1,
    DeadReckoning = 2,
    EmergencyImpact = 3,
    NetworkDisconnected = 4,
    CameraDisconnected = 5,
    UnconfirmedProximity = 6
};

// Types.cpp ::riskLevelFromInt
inline RiskLevel riskLevelFromInt(int value) {
    switch (value) {
        case 1: return RiskLevel::Caution;
        case 2: return RiskLevel::Danger;
        case 3: return RiskLevel::Emergency;
        default: return RiskLevel::Safe;
    }
}

// Types.cpp ::exceptionStateFromString
inline ExceptionState exceptionStateFromString(const std::string& value) {
    if (value == "SENSOR_FAULT") return ExceptionState::SensorFault;
    if (value == "DEAD_RECKONING") return ExceptionState::DeadReckoning;
    if (value == "EMERGENCY_IMPACT") return ExceptionState::EmergencyImpact;
    if (value == "NETWORK_DISCONNECTED") return ExceptionState::NetworkDisconnected;
    if (value == "CAMERA_DISCONNECTED") return ExceptionState::CameraDisconnected;
    if (value == "UNCONFIRMED_PROXIMITY") return ExceptionState::UnconfirmedProximity;
    return ExceptionState::None;
}

struct RiskMetadata {
    std::string stream_id;
    std::string camera_id;
    int channel = 0;
    std::string zone;
    RiskLevel risk_level = RiskLevel::Safe;
    double distance_m = 0.0;
    bool distance_valid = true;
    ExceptionState exception_state = ExceptionState::None;
    std::string utc_time;
};

// RiskMetadata.cpp ::fromJson
// QJsonValue::isDouble()는 JSON number면 true, null이면 false.
inline RiskMetadata fromJson(const nlohmann::json& obj) {
    RiskMetadata meta;
    if (obj.contains("stream_id") && obj["stream_id"].is_string())
        meta.stream_id = obj["stream_id"].get<std::string>();
    if (obj.contains("camera_id") && obj["camera_id"].is_string())
        meta.camera_id = obj["camera_id"].get<std::string>();
    if (obj.contains("channel") && obj["channel"].is_number_integer())
        meta.channel = obj["channel"].get<int>();
    if (obj.contains("zone") && obj["zone"].is_string())
        meta.zone = obj["zone"].get<std::string>();
    const int risk_int = (obj.contains("risk_level") && obj["risk_level"].is_number_integer())
                             ? obj["risk_level"].get<int>()
                             : 0;
    meta.risk_level = riskLevelFromInt(risk_int);

    if (obj.contains("distance_mm") && obj["distance_mm"].is_number()) {
        meta.distance_valid = true;
        meta.distance_m = obj["distance_mm"].get<double>() / 1000.0;
    } else if (obj.contains("distance_m") && obj["distance_m"].is_number()) {
        meta.distance_valid = true;
        meta.distance_m = obj["distance_m"].get<double>();
    } else {
        meta.distance_valid = false;
        meta.distance_m = 0.0;
    }

    const std::string exception =
        (obj.contains("exception_state") && obj["exception_state"].is_string())
            ? obj["exception_state"].get<std::string>()
            : std::string();
    meta.exception_state = exceptionStateFromString(exception);
    if (obj.contains("utc_time") && obj["utc_time"].is_string())
        meta.utc_time = obj["utc_time"].get<std::string>();
    return meta;
}

inline RiskMetadata fromJsonText(const std::string& text) {
    return fromJson(nlohmann::json::parse(text));
}

// ActiveCameraController.cpp ::handleMetadataUpdated 의 matches
inline bool matchesActiveCamera(const RiskMetadata& metadata, const std::string& active_stream_id,
                                const std::string& active_physical_camera_id = {}) {
    if (!metadata.stream_id.empty() && metadata.stream_id == active_stream_id) return true;
    if (metadata.camera_id == active_stream_id) return true;
    if (!active_physical_camera_id.empty() && metadata.camera_id == active_physical_camera_id &&
        metadata.stream_id.empty())
        return true;
    return false;
}

// RiskHud.qml
inline bool hudVisible(const RiskMetadata& m) {
    const int risk = static_cast<int>(m.risk_level);
    const int exception = static_cast<int>(m.exception_state);
    const bool isAlert = risk > 0;
    const bool hasException = exception > 0;
    return isAlert || hasException;
}

inline const char* hudTag(const RiskMetadata& m) {
    const int exception = static_cast<int>(m.exception_state);
    if (exception > 0) {
        switch (exception) {
            case 1: return "센서 점검";
            case 2: return "자율 항법";
            case 3: return "비상 충돌";
            case 4: return "통신 단절";
            case 5: return "카메라 단절";
            case 6: return "거리 미확인";
            default: return "상태 알림";
        }
    }
    switch (static_cast<int>(m.risk_level)) {
        case 1: return "주의 [CAUTION]";
        case 2: return "경보 [DANGER]";
        case 3: return "비상 [EMERGENCY]";
        default: return "정상 [SAFE]";
    }
}

// EdgeWarningFrame.qml
inline bool edgeIsEmergency(const RiskMetadata& m) {
    return static_cast<int>(m.risk_level) >= 3 ||
           m.exception_state == ExceptionState::EmergencyImpact;
}
inline bool edgeIsDanger(const RiskMetadata& m) {
    if (edgeIsEmergency(m)) return false;
    return static_cast<int>(m.risk_level) == 2 ||
           m.exception_state == ExceptionState::NetworkDisconnected ||
           m.exception_state == ExceptionState::CameraDisconnected;
}
inline bool edgeIsCaution(const RiskMetadata& m) {
    if (edgeIsEmergency(m) || edgeIsDanger(m)) return false;
    return static_cast<int>(m.risk_level) == 1 ||
           static_cast<int>(m.exception_state) > 0;
}
inline bool edgeAlert(const RiskMetadata& m) {
    return edgeIsEmergency(m) || edgeIsDanger(m) || edgeIsCaution(m);
}

// RiskBanner.qml (단말 카드 + 관제 카드 공통)
inline bool bannerUnknown(const RiskMetadata& m) {
    return static_cast<int>(m.exception_state) != 0;
}

// AlertListModel.cpp ::upsert shouldBeVisible
inline bool alertListVisible(const RiskMetadata& m) {
    return m.risk_level != RiskLevel::Safe || m.exception_state != ExceptionState::None;
}

// ActiveCameraController.cpp ::applyRiskToWarningDevice
inline bool fpgaTxSuspended(const RiskMetadata& m) {
    return m.exception_state == ExceptionState::NetworkDisconnected;
}
inline int fpgaRiskLevel(const RiskMetadata& m) {
    if (fpgaTxSuspended(m)) return -1;
    return static_cast<int>(m.risk_level);
}

struct DriverScreen {
    bool applied = false;
    bool hud_visible = false;
    const char* hud_tag = "";
    bool edge_glow = false;
    bool banner_unknown = false;
    bool alert_list = false;
    int fpga_risk = 0;
};

inline DriverScreen renderOnTerminal(const RiskMetadata& metadata,
                                     const std::string& active_stream_id) {
    DriverScreen screen;
    if (!matchesActiveCamera(metadata, active_stream_id, "CAM_01")) return screen;
    screen.applied = true;
    screen.hud_visible = hudVisible(metadata);
    screen.hud_tag = hudTag(metadata);
    screen.edge_glow = edgeAlert(metadata);
    screen.banner_unknown = bannerUnknown(metadata);
    screen.alert_list = alertListVisible(metadata);
    screen.fpga_risk = fpgaRiskLevel(metadata);
    return screen;
}

}  // namespace qt_client
