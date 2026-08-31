// 서버 toJson() → Qt 단말/관제 핵심 로직 E2E.
// Qt6 런타임이 없는 환경에서 forklift-device / operator-device 의 파서·HUD·
// 경보 목록·FPGA 송신 분기를 그대로 돌려, MQTT 페이로드가 화면을 어떻게
// 바꾸는지 검증한다.

#include <chrono>
#include <iostream>
#include <string>

#include "logic/judgment/announcement_gate.hpp"
#include "logic/judgment/danger_judgment_engine.h"
#include "qt_client_contract.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    std::cout << (condition ? "  [OK]   " : "  [FAIL] ") << message << '\n';
    if (!condition) ++failures;
}

forklift::config::DangerJudgmentConfig productionThresholds() {
    forklift::config::DangerJudgmentConfig config{};
    config.caution_threshold_mm = 750.0;
    config.danger_threshold_mm = 550.0;
    config.emergency_threshold_mm = 350.0;
    config.emergency_release_margin_mm = 100.0;
    config.tof_caution_mm = 750.0;
    config.tof_danger_mm = 550.0;
    config.impact_accel_threshold_g = 2.0;
    return config;
}

CameraInput cameraAt(double distance_mm, bool localized = true, bool person = true) {
    CameraInput cam;
    cam.forklift_localized = localized;
    cam.person_detected = person;
    cam.forklift = {0.0, 0.0};
    cam.person = {distance_mm, 0.0};
    cam.camera_id = "CAM_01";
    return cam;
}

SensorInput ignoredSensor() {
    SensorInput sen;
    sen.imu_ok = true;
    sen.tof_ok = true;
    sen.tof_distance_mm = 5000.0;
    return sen;
}

JudgmentResult tagged(JudgmentResult result, const std::string& stream_id) {
    result.terminal_id = "TERM_01";
    result.stream_id = stream_id;
    result.camera_id = "CAM_01";
    result.channel = 2;
    return result;
}

qt_client::DriverScreen screenFrom(const JudgmentResult& result,
                                   const std::string& active = "CAM_01_CH_02") {
    return qt_client::renderOnTerminal(qt_client::fromJsonText(toJson(result)), active);
}

}  // namespace

int main() {
    using Clock = AnnouncementGate::Clock;
    using ms = std::chrono::milliseconds;
    const auto t0 = Clock::now();
    const bool ignore_sensor = true;
    DangerJudgmentEngine engine(productionThresholds(), 0.0, ms(0), ignore_sensor);

    std::cout << "[1] 게이트 없이 마커 폐색 JSON을 Qt가 받으면\n";
    engine.resetHysteresis();
    engine.evaluate(cameraAt(1097.0), ignoredSensor());
    CameraInput occluded = cameraAt(1097.0, /*localized=*/false);
    const auto raw_dr = tagged(engine.evaluate(occluded, ignoredSensor()), "CAM_01_CH_02");
    check(raw_dr.exception == ExceptionState::DEAD_RECKONING &&
              raw_dr.final_risk == RiskLevel::SAFE,
          "엔진은 SAFE + DEAD_RECKONING");
    const auto raw_screen = screenFrom(raw_dr);
    check(raw_screen.applied && raw_screen.hud_visible &&
              std::string(raw_screen.hud_tag) == "자율 항법",
          "구형 페이로드면 운전석 HUD가 '자율 항법'으로 켜짐");
    check(raw_screen.edge_glow && raw_screen.banner_unknown && raw_screen.alert_list,
          "구형 페이로드면 테두리·관제 UNKNOWN·경보 목록이 같이 켜짐");
    check(raw_screen.fpga_risk == 0, "FPGA는 risk_level만 보므로 0 유지");

    std::cout << "\n[2] 공지 게이트를 거친 같은 폐색\n";
    engine.resetHysteresis();
    AnnouncementGate gate(ms(400), ms(1500));
    auto safe = tagged(engine.evaluate(cameraAt(1097.0), ignoredSensor()), "CAM_01_CH_02");
    gate.apply(safe, t0);
    const auto announced = gate.apply(
        tagged(engine.evaluate(occluded, ignoredSensor()), "CAM_01_CH_02"), t0 + ms(10));
    const auto gated_json = nlohmann::json::parse(toJson(announced));
    check(gated_json.at("exception_state") == "NONE" && gated_json.at("risk_level") == 0,
          "MQTT JSON은 exception NONE, risk_level 0");
    check(gated_json.at("stream_id") == "CAM_01_CH_02" && gated_json.at("camera_id") == "CAM_01",
          "stream_id/camera_id 계약 유지");
    check(gated_json.at("distance_mm").is_number(), "직전 거리 숫자는 그대로");
    check(gated_json.contains("distance_m") && gated_json.at("distance_m").is_number() &&
              std::abs(gated_json.at("distance_m").get<double>() * 1000.0 -
                       gated_json.at("distance_mm").get<double>()) < 15.0,
          "구형 Qt용 distance_m(미터)도 같이 나간다");
    const auto gated_screen = screenFrom(announced);
    check(gated_screen.applied && !gated_screen.hud_visible,
          "운전석 HUD는 Clean View (숨김)");
    check(!gated_screen.edge_glow && !gated_screen.banner_unknown && !gated_screen.alert_list,
          "테두리·관제 UNKNOWN·경보 목록이 안 켜짐");
    check(gated_screen.fpga_risk == 0, "FPGA risk 0");
    check(std::string(qt_client::hudTag(qt_client::fromJsonText(toJson(announced)))) !=
              "자율 항법",
          "공지 페이로드는 '자율 항법' 태그를 만들지 않음");

    std::cout << "\n[3] stream_id 가 활성 화면과 다를 때 HUD는 무시\n";
    auto other = announced;
    other.stream_id = "CAM_01_CH_03";
    other.final_risk = RiskLevel::DANGER;
    other.exception = ExceptionState::NONE;
    const auto missed = screenFrom(other, "CAM_01_CH_02");
    check(!missed.applied, "CH_03 페이로드는 CH_02 활성 화면에 안 붙음");
    auto assigned = other;
    assigned.stream_id = "CAM_01_CH_02";
    const auto bound = screenFrom(assigned, "CAM_01_CH_02");
    check(bound.applied && bound.hud_visible && std::string(bound.hud_tag) == "경보 [DANGER]",
          "배정 stream_id 면 HUD가 DANGER를 표시");

    std::cout << "\n[4] 공지 지연: 짧은 CAUTION은 화면에 안 나감\n";
    engine.resetHysteresis();
    AnnouncementGate dwell(ms(400), ms(1500));
    dwell.apply(tagged(engine.evaluate(cameraAt(1097.0), ignoredSensor()), "CAM_01_CH_02"), t0);
    const auto brief = dwell.apply(
        tagged(engine.evaluate(cameraAt(700.0), ignoredSensor()), "CAM_01_CH_02"), t0 + ms(200));
    const auto brief_screen = screenFrom(brief);
    check(!brief_screen.hud_visible && brief_screen.fpga_risk == 0,
          "CAUTION 0.2초는 HUD/FPGA에 안 나감");
    const auto held = dwell.apply(
        tagged(engine.evaluate(cameraAt(700.0), ignoredSensor()), "CAM_01_CH_02"), t0 + ms(600));
    const auto held_screen = screenFrom(held);
    check(held_screen.hud_visible && std::string(held_screen.hud_tag) == "주의 [CAUTION]" &&
              held_screen.fpga_risk == 1,
          "CAUTION 0.4초 유지 시 HUD 주의 + FPGA 1");

    std::cout << "\n[5] EMERGENCY는 즉시 HUD/FPGA 3\n";
    engine.resetHysteresis();
    AnnouncementGate emergency_gate(ms(400), ms(1500));
    emergency_gate.apply(
        tagged(engine.evaluate(cameraAt(1097.0), ignoredSensor()), "CAM_01_CH_02"), t0);
    const auto emergency = emergency_gate.apply(
        tagged(engine.evaluate(cameraAt(300.0), ignoredSensor()), "CAM_01_CH_02"), t0 + ms(1));
    const auto emergency_screen = screenFrom(emergency);
    check(emergency_screen.hud_visible &&
              std::string(emergency_screen.hud_tag) == "비상 [EMERGENCY]" &&
              emergency_screen.fpga_risk == 3,
          "EMERGENCY는 확인 대기 없이 단말·FPGA에 즉시 반영");

    std::cout << "\n[6] 실제 예외 SENSOR_FAULT 는 관제 UNKNOWN으로 남아야 함\n";
    AnnouncementGate sensor_gate(ms(0), ms(0));
    JudgmentResult fault{};
    fault.final_risk = RiskLevel::CAUTION;
    fault.exception = ExceptionState::SENSOR_FAULT;
    fault.distance_mm = 1097.0;
    fault = tagged(fault, "CAM_01_CH_02");
    const auto fault_screen = screenFrom(sensor_gate.apply(fault, t0));
    check(fault_screen.hud_visible && std::string(fault_screen.hud_tag) == "센서 점검" &&
              fault_screen.banner_unknown,
          "센서 고장은 단말 '센서 점검' + 관제 UNKNOWN");

    std::cout << "\n[7] 하락 1.5초 전에는 주의 HUD 유지\n";
    engine.resetHysteresis();
    AnnouncementGate fall(ms(400), ms(1500));
    fall.apply(tagged(engine.evaluate(cameraAt(700.0), ignoredSensor()), "CAM_01_CH_02"), t0);
    const auto still = fall.apply(
        tagged(engine.evaluate(cameraAt(1097.0), ignoredSensor()), "CAM_01_CH_02"), t0 + ms(800));
    check(screenFrom(still).hud_visible && std::string(screenFrom(still).hud_tag) == "주의 [CAUTION]",
          "SAFE로 내려가도 0.8초는 주의 HUD 유지");
    const auto released = fall.apply(
        tagged(engine.evaluate(cameraAt(1097.0), ignoredSensor()), "CAM_01_CH_02"), t0 + ms(2300));
    check(!screenFrom(released).hud_visible && screenFrom(released).fpga_risk == 0,
          "1.5초 뒤 Clean View + FPGA 0");

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
