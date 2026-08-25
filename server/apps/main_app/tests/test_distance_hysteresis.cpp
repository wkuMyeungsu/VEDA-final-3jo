// test_distance_hysteresis.cpp
// 거리 경계 히스테리시스(슈미트 트리거) 검증.
//
// 운영 관찰된 버그: 두 카메라의 사람 거리가 CAUTION 임계값 근처를 오갈 때
// SAFE<->CAUTION 판정과 WARN/INFO 로그가 초당 수 회 반복됐다(2026-08-25).
// EMERGENCY 래치와 같은 원칙 - "상승은 즉시, 하락은 release margin 넘었을 때만" -
// 을 SAFE/CAUTION/DANGER 경계로 확장한 것을 검증한다.
//
// 임계값: caution=750, danger=550, emergency=350, release margin=100 (운영값).

#include <iostream>
#include <string>

#include "logic/judgment/danger_judgment_engine.h"


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

CameraInput cameraAt(double distance_mm) {
    CameraInput cam;
    cam.forklift_localized = true;
    cam.person_detected = true;
    cam.forklift = {0.0, 0.0};
    cam.person = {distance_mm, 0.0};
    return cam;
}

SensorInput healthySensor() {
    SensorInput sen;
    sen.imu_ok = true;
    sen.tof_ok = true;
    sen.tof_distance_mm = 5000.0;
    sen.imu_accel_g = 0.0;
    return sen;
}

}  // namespace

int main() {
    const auto config = productionThresholds();
    std::chrono::milliseconds no_grace(0);
    DangerJudgmentEngine engine(config, /*collision_radius_mm=*/0.0,
                                                 no_grace);

    auto evaluateDistance = [&engine](double dist) {
        return engine.evaluate(cameraAt(dist), healthySensor()).final_risk;
    };

    std::cout << "[경계 진동 시나리오]\n";
    // 700mm: CAUTION 진입(즉시)
    check(evaluateDistance(700.0) == RiskLevel::CAUTION, "700mm에서 CAUTION 진입");
    // 800mm: 원래는 SAFE지만 해제 유예(750+100=850 이내)로 CAUTION 유지
    check(evaluateDistance(800.0) == RiskLevel::CAUTION, "800mm도 유예 구간이라 CAUTION 유지");
    // 다시 700mm 왕복: 여전히 CAUTION 한 상태 (진동 없음)
    check(evaluateDistance(700.0) == RiskLevel::CAUTION, "재진입해도 CAUTION");
    // 900mm: 유예 구간(850mm) 초과 -> SAFE 해제
    check(evaluateDistance(900.0) == RiskLevel::SAFE, "850mm 초과 시 SAFE 해제");
    // 740mm 재진입: 임계값 이하 복귀는 즉시 CAUTION (상승 방향 지연 없음)
    check(evaluateDistance(740.0) == RiskLevel::CAUTION, "SAFE에서 750mm 이하면 즉시 CAUTION 복귀");

    std::cout << "\n[DANGER 경계]\n";
    // DANGER 진입 즉시
    check(evaluateDistance(540.0) == RiskLevel::DANGER, "540mm에서 DANGER 진입");
    // 600mm: danger 유예(550+100=650 이내) + 직전 DANGER -> DANGER 유지
    check(evaluateDistance(600.0) == RiskLevel::DANGER, "600mm도 유예 구간이라 DANGER 유지");
    // 700mm: danger 유예(650) 초과 -> CAUTION으로 한 단계 해제
    check(evaluateDistance(700.0) == RiskLevel::CAUTION, "650mm 초과 시 CAUTION으로 해제");

    std::cout << "\n[EMERGENCY 래치 기존 동작 호환]\n";
    check(evaluateDistance(340.0) == RiskLevel::EMERGENCY, "340mm에서 EMERGENCY 진입");
    check(evaluateDistance(400.0) == RiskLevel::EMERGENCY, "450mm 이하선 EMERGENCY 래치 유지");
    check(evaluateDistance(460.0) != RiskLevel::EMERGENCY, "450mm 초과 시 EMERGENCY 해제");

    // resetHysteresis: 직전 단계가 초기화돼 유예 없이 다시 판정된다.
    engine.evaluate(cameraAt(700.0), healthySensor());
    engine.resetHysteresis();
    check(evaluateDistance(900.0) == RiskLevel::SAFE,
          "reset 후에는 유예 없이 SAFE 판정");

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
