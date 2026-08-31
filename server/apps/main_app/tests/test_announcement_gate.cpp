#include <iostream>
#include <string>

#include "logic/judgment/announcement_gate.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    std::cout << (condition ? "  [OK]   " : "  [FAIL] ") << message << '\n';
    if (!condition) ++failures;
}

JudgmentResult makeResult(RiskLevel risk, ExceptionState exception, double distance_mm) {
    JudgmentResult result{};
    result.camera_risk = risk;
    result.tof_risk = RiskLevel::SAFE;
    result.final_risk = risk;
    result.exception = exception;
    result.distance_mm = distance_mm;
    result.stream_id = "CAM_01_CH_02";
    result.camera_id = "CAM_01";
    result.channel = 2;
    result.terminal_id = "TERM_01";
    return result;
}

}  // namespace

int main() {
    using Clock = AnnouncementGate::Clock;
    using ms = std::chrono::milliseconds;
    const auto t0 = Clock::now();

    std::cout << "[DEAD_RECKONING 은 Qt 공지에서 뺀다]\n";
    AnnouncementGate gate(ms(400), ms(1500));
    const auto held = gate.apply(
        makeResult(RiskLevel::SAFE, ExceptionState::DEAD_RECKONING, 1097.0), t0);
    check(held.final_risk == RiskLevel::SAFE && held.exception == ExceptionState::NONE,
          "직전 SAFE + 위치 추정이면 단말에는 SAFE/NONE");
    check(held.distance_mm == 1097.0, "직전 거리는 그대로 실어 보낸다");

    AnnouncementGate unknown_gate(ms(400), ms(1500));
    const auto unknown = unknown_gate.apply(
        makeResult(RiskLevel::CAUTION, ExceptionState::DEAD_RECKONING, -1.0), t0);
    check(unknown.final_risk == RiskLevel::SAFE && unknown.exception == ExceptionState::NONE,
          "측정 전 폐색 CAUTION+DR 은 공지에서 SAFE/NONE");

    std::cout << "\n[상승은 확인 후에, EMERGENCY는 즉시]\n";
    AnnouncementGate rise_gate(ms(400), ms(1500));
    rise_gate.apply(makeResult(RiskLevel::SAFE, ExceptionState::NONE, 1097.0), t0);
    const auto early_caution = rise_gate.apply(
        makeResult(RiskLevel::CAUTION, ExceptionState::NONE, 700.0), t0 + ms(200));
    check(early_caution.final_risk == RiskLevel::SAFE,
          "CAUTION 200ms 는 아직 공지하지 않음");
    const auto confirmed_caution = rise_gate.apply(
        makeResult(RiskLevel::CAUTION, ExceptionState::NONE, 700.0), t0 + ms(600));
    check(confirmed_caution.final_risk == RiskLevel::CAUTION,
          "CAUTION 400ms 유지 시 공지");

    AnnouncementGate emergency_gate(ms(400), ms(1500));
    emergency_gate.apply(makeResult(RiskLevel::SAFE, ExceptionState::NONE, 1097.0), t0);
    const auto emergency = emergency_gate.apply(
        makeResult(RiskLevel::EMERGENCY, ExceptionState::NONE, 300.0), t0 + ms(1));
    check(emergency.final_risk == RiskLevel::EMERGENCY,
          "EMERGENCY 상승은 확인 대기 없이 즉시 공지");

    std::cout << "\n[하락은 더 오래 붙든다]\n";
    AnnouncementGate fall_gate(ms(400), ms(1500));
    fall_gate.apply(makeResult(RiskLevel::CAUTION, ExceptionState::NONE, 700.0), t0);
    const auto early_safe = fall_gate.apply(
        makeResult(RiskLevel::SAFE, ExceptionState::NONE, 1097.0), t0 + ms(800));
    check(early_safe.final_risk == RiskLevel::CAUTION,
          "SAFE 0.8초는 아직 공지하지 않음");
    const auto confirmed_safe = fall_gate.apply(
        makeResult(RiskLevel::SAFE, ExceptionState::NONE, 1097.0), t0 + ms(2300));
    check(confirmed_safe.final_risk == RiskLevel::SAFE,
          "SAFE 1.5초 유지 시 공지 해제");

    std::cout << "\n[실제 예외는 그대로 내보낸다]\n";
    AnnouncementGate sensor_gate(ms(0), ms(0));
    const auto fault = sensor_gate.apply(
        makeResult(RiskLevel::CAUTION, ExceptionState::SENSOR_FAULT, 1097.0), t0);
    check(fault.exception == ExceptionState::SENSOR_FAULT &&
              fault.final_risk == RiskLevel::CAUTION,
          "SENSOR_FAULT 는 Qt가 센서 점검으로 보여도 공지한다");

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
