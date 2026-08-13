// test_exception_trigger.cpp
// DangerJudgmentEngine 예외처리 트리거 검증 테스트
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   evaluate()가 만들어내는 예외 상태 4종
//   (SENSOR_FAULT / DEAD_RECKONING / EMERGENCY_IMPACT / UNCONFIRMED_PROXIMITY)이
//   임계값 경계와 조건 중첩 상황에서 의도대로 결정되는지 확인한다.
//   NETWORK_DISCONNECTED / CAMERA_DISCONNECTED는 단말 로컬 판정이라 이 파일 범위 밖.
//
//   [테스트 1] 임계값 경계값 - 바로 아래/정확히/바로 위에서 판정이 정확히 갈리는지
//              (impact_accel_threshold_g=2.0, tof_danger_mm=500, tof_caution_mm=1000)
//   [테스트 2] detectException() 우선순위 충돌 매트릭스 -
//              충돌 > 센서고장 > DR > 미확인근접 순서가 조건 중첩 시에도 유지되는지,
//              그 우선순위 보정이 final_risk에도 맞게 반영되는지
//   [테스트 3] 거리 기반 EMERGENCY(risk_level 3)와 IMU 기반 EMERGENCY_IMPACT(exception)가
//              서로 다른 축이라는 것 - 특히 예외 보정이 위험도를 낮추지 않는지
//   [테스트 4] 충돌 반경 0mm에서는 중심점 거리 판정이 그대로 유지되고,
//              반경을 주더라도 외부 distance_mm에는 원시 중심점 거리가 남는지
//
//   DEAD_RECKONING은 트리거 발생 여부와 "최소 CAUTION 유지"까지만 검증한다.
//   회전 중 폐색 지속에 따른 DANGER 에스컬레이션은 회전 임계값이 잠정치라 범위 밖.
//
// 엔진은 danger_judgment_engine.h(선언) / .cpp(구현)로 분리되어 있고 main()은
// danger_judgment_engine_main.cpp에 따로 있다. 따라서 헤더만 평범하게 include하고
// 엔진 구현은 링크 시점에 합친다(예전의 #define main 토큰 치환 트릭은 불필요해져 제거).
//
// 임계값 비교는 전부 부동소수점 상등/부등이지만, 경계값(2.0 / 0.5 / 1.0)은 모두
// 이진 부동소수점으로 정확히 표현되는 값이라 판정이 결정론적이다.
//
// 빌드: g++ -std=c++17 test_exception_trigger.cpp danger_judgment_engine.cpp \
//           -o test_exception_trigger -pthread
// 실행: ./test_exception_trigger   (종료코드 0=성공, 1=실패)

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "logic/judgment/danger_judgment_engine.h"

namespace {

int failures = 0;

// 한 케이스의 입력과 기대값.
// exception과 final_risk를 함께 검증한다(우선순위 보정이 fused 값에도 반영되는지 확인).
struct Case {
    const char*    name;
    CameraInput    cam;
    SensorInput    sen;
    ExceptionState expected_exception;
    RiskLevel      expected_risk;
};

// 케이스 이름에 한글이 섞이면 std::setw(바이트 수 기준)로는 열이 안 맞는다.
// UTF-8 멀티바이트 문자를 2열(터미널 전각폭)로 세어 표시폭 기준으로 패딩한다.
std::string padTo(const std::string& s, std::size_t width) {
    std::size_t cols = 0;
    for (std::size_t i = 0; i < s.size(); ) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch < 0x80)        { cols += 1; i += 1; }  // ASCII
        else if (ch < 0xE0)   { cols += 1; i += 2; }  // 2바이트 (라틴 확장 등)
        else if (ch < 0xF0)   { cols += 2; i += 3; }  // 3바이트 (한글/CJK)
        else                  { cols += 2; i += 4; }  // 4바이트 (이모지 등)
    }
    return cols >= width ? s + " " : s + std::string(width - cols, ' ');
}

void runCase(const DangerJudgmentEngine& engine, const Case& c) {
    JudgmentResult r = engine.evaluate(c.cam, c.sen);

    const bool exc_ok  = (r.exception  == c.expected_exception);
    const bool risk_ok = (r.final_risk == c.expected_risk);
    const bool ok      = exc_ok && risk_ok;
    if (!ok) ++failures;

    std::cout << (ok ? "  [OK]   " : "  [FAIL] ")
              << padTo(c.name, 42)
              << "기대: exc=" << padTo(toString(c.expected_exception), 23)
              << "risk=" << toString(c.expected_risk);
    if (ok) {
        std::cout << "\n";
    } else {
        // 어느 쪽이 틀렸는지 바로 보이도록 실제값을 항목별로 붙여 출력한다.
        std::cout << "\n           -> " << padTo("실제:", 42 - 3)
                  << "      exc=" << padTo(toString(r.exception), 23)
                  << "risk=" << toString(r.final_risk)
                  << "   (mismatch: " << (exc_ok ? "" : "exception ")
                  << (risk_ok ? "" : "final_risk") << ")"
                  << "  [cam=" << toString(r.camera_risk)
                  << " tof=" << toString(r.tof_risk) << "]\n";
    }
}

void runCases(const DangerJudgmentEngine& engine, const std::vector<Case>& cases) {
    for (const Case& c : cases) runCase(engine, c);
}

// CameraInput 인자 순서: forklift_localized, person_detected, forklift, person, camera_id, zone
// SensorInput 인자 순서:  imu_ok, tof_ok, tof_distance_mm, imu_accel_g, is_dead_reckoning

forklift::config::DangerJudgmentConfig testJudgmentConfig() {
    return {3000.0, 1500.0, 400.0, 100.0, 1000.0, 500.0, 2.0, 0.0};
}

// 카메라 기준으로 확실히 SAFE인 좌표쌍(거리 약 14142mm) - 카메라 축을 중립화할 때 사용.
const WorldPoint kFarA{0.0, 0.0};
const WorldPoint kFarB{10000.0, 10000.0};

// ── 테스트 1: 경계값 ────────────────────────────────────────
// 각 임계값의 바로 아래 / 정확히 / 바로 위 3점에서 판정이 갈리는 위치를 확인한다.
void testBoundaryValues() {
    DangerJudgmentEngine engine(testJudgmentConfig(), std::chrono::milliseconds(500));

    std::cout << "[테스트 1] 임계값 경계값 - 판정이 갈리는 지점 확인\n";

    // (1) EMERGENCY_IMPACT: imu_accel_g >= 2.0g
    //     이 값은 테스트 코드의 상수가 아니라 testJudgmentConfig()로 주입한 설정값이다.
    //     카메라·ToF는 모두 SAFE로 중립화해 충돌 트리거만 관측한다.
    std::cout << "  -- EMERGENCY_IMPACT (impact_accel_threshold_g = 2.0g) --\n";
    runCases(engine, {
        {"accel 1.99g (임계 바로 아래)",
         CameraInput{true, true, kFarA, kFarB, "", ""},
         SensorInput{true, true, 5000.0, 1.99, false},
         ExceptionState::NONE,             RiskLevel::SAFE},

        {"accel 2.00g (임계 정확히)",
         CameraInput{true, true, kFarA, kFarB, "", ""},
         SensorInput{true, true, 5000.0, 2.00, false},
         ExceptionState::EMERGENCY_IMPACT, RiskLevel::DANGER},

        {"accel 2.01g (임계 바로 위)",
         CameraInput{true, true, kFarA, kFarB, "", ""},
         SensorInput{true, true, 5000.0, 2.01, false},
         ExceptionState::EMERGENCY_IMPACT, RiskLevel::DANGER},
    });

    // (2) ToF danger 경계: tof_distance_mm <= 500 -> DANGER
    //     person_detected=false로 두어 UNCONFIRMED_PROXIMITY 트리거 조건을 함께 검증한다.
    //     (지게차 좌표는 정상, 사람만 미검출 -> ToF가 근접을 보고하면 "대상 미확인 근접")
    std::cout << "  -- ToF danger (tof_danger_mm = 500mm) + UNCONFIRMED_PROXIMITY --\n";
    runCases(engine, {
        {"tof 490mm (임계 바로 아래)",
         CameraInput{true, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 490.0, 0.1, false},
         ExceptionState::UNCONFIRMED_PROXIMITY, RiskLevel::DANGER},

        {"tof 500mm (임계 정확히)",
         CameraInput{true, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 500.0, 0.1, false},
         ExceptionState::UNCONFIRMED_PROXIMITY, RiskLevel::DANGER},

        {"tof 510mm (임계 바로 위)",
         CameraInput{true, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 510.0, 0.1, false},
         ExceptionState::UNCONFIRMED_PROXIMITY, RiskLevel::CAUTION},
    });

    // (3) ToF caution 경계: tof_distance_mm <= 1000 -> CAUTION, 초과면 SAFE
    //     1010mm에서는 ToF가 SAFE가 되므로 UNCONFIRMED_PROXIMITY도 함께 풀려 NONE이 된다.
    std::cout << "  -- ToF caution (tof_caution_mm = 1000mm) --\n";
    runCases(engine, {
        {"tof 990mm (임계 바로 아래)",
         CameraInput{true, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 990.0, 0.1, false},
         ExceptionState::UNCONFIRMED_PROXIMITY, RiskLevel::CAUTION},

        {"tof 1000mm (임계 정확히)",
         CameraInput{true, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 1000.0, 0.1, false},
         ExceptionState::UNCONFIRMED_PROXIMITY, RiskLevel::CAUTION},

        {"tof 1010mm (임계 바로 위)",
         CameraInput{true, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 1010.0, 0.1, false},
         ExceptionState::NONE,                  RiskLevel::SAFE},
    });

    // (4) SENSOR_FAULT / DEAD_RECKONING 트리거 자체 확인 (최소 CAUTION 유지)
    //     DR은 여기까지만 - 회전 중 폐색 지속 -> DANGER 에스컬레이션은 범위 밖.
    std::cout << "  -- SENSOR_FAULT / DEAD_RECKONING 트리거 + 최소 CAUTION 보정 --\n";
    runCases(engine, {
        {"ToF 고장 (tof_ok=false)",
         CameraInput{true, true, kFarA, kFarB, "", ""},
         SensorInput{true, false, 0.0, 0.1, false},
         ExceptionState::SENSOR_FAULT,   RiskLevel::CAUTION},

        {"IMU 고장 (imu_ok=false)",
         CameraInput{true, true, kFarA, kFarB, "", ""},
         SensorInput{false, true, 5000.0, 0.1, false},
         ExceptionState::SENSOR_FAULT,   RiskLevel::CAUTION},

        {"마커 폐색 (forklift_localized=false)",
         CameraInput{false, true, kFarA, kFarB, "", ""},
         SensorInput{true, true, 5000.0, 0.1, false},
         ExceptionState::DEAD_RECKONING, RiskLevel::CAUTION},

        {"IMU 추정 모드 (is_dead_reckoning=true)",
         CameraInput{true, true, kFarA, kFarB, "", ""},
         SensorInput{true, true, 5000.0, 0.1, true},
         ExceptionState::DEAD_RECKONING, RiskLevel::CAUTION},
    });
}

// ── 테스트 2: 우선순위 충돌 매트릭스 ────────────────────────
// detectException()의 우선순위: 충돌 > 센서고장 > DR > 미확인근접.
// 여러 조건이 동시에 걸렸을 때 상위 조건이 이기는지, 그 보정이 final_risk에도
// 맞게 반영되는지 함께 본다.
void testPriorityMatrix() {
    DangerJudgmentEngine engine(testJudgmentConfig(), std::chrono::milliseconds(500));

    std::cout << "\n[테스트 2] 우선순위 충돌 매트릭스 (충돌 > 센서고장 > DR > 미확인근접)\n";

    runCases(engine, {
        // C1: 충돌 vs ToF 고장 -> 충돌 우선. 보정으로 무조건 DANGER.
        {"C1 충돌2.1g + ToF고장",
         CameraInput{true, true, kFarA, kFarB, "", ""},
         SensorInput{true, false, 0.0, 2.1, false},
         ExceptionState::EMERGENCY_IMPACT, RiskLevel::DANGER},

        // C2: 충돌 vs 마커 폐색 -> 충돌 우선. 보정으로 무조건 DANGER.
        {"C2 충돌2.13g + 마커폐색",
         CameraInput{false, true, kFarA, kFarB, "", ""},
         SensorInput{true, true, 5000.0, 2.13, false},
         ExceptionState::EMERGENCY_IMPACT, RiskLevel::DANGER},

        // C3: ToF 고장 vs 마커 폐색 -> 센서고장 우선.
        //     카메라(폐색으로 판정보류 SAFE) + ToF(고장으로 판정보류 SAFE) -> 최소 CAUTION 보정.
        {"C3 ToF고장 + 마커폐색",
         CameraInput{false, true, kFarA, kFarB, "", ""},
         SensorInput{true, false, 0.0, 0.1, false},
         ExceptionState::SENSOR_FAULT,     RiskLevel::CAUTION},

        // C4: 마커 폐색 vs 미확인 근접 -> DR 우선.
        //     ToF 400mm가 DANGER이므로 최소 CAUTION 보정에 걸리지 않고 DANGER 그대로 유지되어야 한다.
        {"C4 마커폐색 + 사람미검출 + tof0.4",
         CameraInput{false, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 400.0, 0.1, false},
         ExceptionState::DEAD_RECKONING,   RiskLevel::DANGER},

        // C5: 충돌 vs 미확인 근접 -> 충돌 우선.
        {"C5 충돌2.13g + 사람미검출 + tof0.4",
         CameraInput{true, false, kFarA, kFarB, "", ""},
         SensorInput{true, true, 400.0, 2.13, false},
         ExceptionState::EMERGENCY_IMPACT, RiskLevel::DANGER},
    });
}

// ── 테스트 3: EMERGENCY(위험도) x EMERGENCY_IMPACT(예외) ────
// 이름이 비슷하지만 축이 다르다: 전자는 카메라 거리 기반 risk_level 3,
// 후자는 IMU 급가속도 기반 exception_state다. 한쪽이 다른 쪽을 덮어쓰면 안 된다.
//
// 특히 예외 보정은 위험도를 "올리기만" 해야 한다. EMERGENCY_IMPACT 보정이 예전처럼
// final_risk를 DANGER로 못박으면, 카메라상 충돌 임박(EMERGENCY)인 상태에서 충돌까지
// 의심되는 최악 상황이 오히려 3 -> 2로 내려간다.
void testEmergencyTierVsImpact() {
    std::cout << "\n[테스트 3] 거리 EMERGENCY(risk 3) x IMU EMERGENCY_IMPACT(exception)\n";

    // 300mm는 emergency_threshold_mm(400) 이내다. ToF는 5000mm로 두어 카메라 축만 본다.
    const WorldPoint kNearA{0.0, 0.0};
    const WorldPoint kNearB{300.0, 0.0};

    // [주의] 케이스마다 엔진을 새로 만든다. EMERGENCY 히스테리시스가 프레임 간 상태를
    //        남기므로 한 엔진을 공유하면 앞 케이스의 래치가 뒤 케이스 기대값을 흔든다.
    {
        DangerJudgmentEngine engine(testJudgmentConfig(), std::chrono::milliseconds(500));
        runCase(engine, {"카메라 300mm (충돌 없음)",
                         CameraInput{true, true, kNearA, kNearB, "", ""},
                         SensorInput{true, true, 5000.0, 0.1, false},
                         ExceptionState::NONE,             RiskLevel::EMERGENCY});
    }
    {
        DangerJudgmentEngine engine(testJudgmentConfig(), std::chrono::milliseconds(500));
        runCase(engine, {"카메라 300mm + 충돌2.1g",
                         CameraInput{true, true, kNearA, kNearB, "", ""},
                         SensorInput{true, true, 5000.0, 2.1, false},
                         // 예외는 EMERGENCY_IMPACT로 태깅되되 위험도는 3에서 안 내려가야 한다.
                         ExceptionState::EMERGENCY_IMPACT, RiskLevel::EMERGENCY});
    }
    {
        DangerJudgmentEngine engine(testJudgmentConfig(), std::chrono::milliseconds(500));
        runCase(engine, {"카메라 원거리 + 충돌2.1g (기존 동작)",
                         CameraInput{true, true, kFarA, kFarB, "", ""},
                         SensorInput{true, true, 5000.0, 2.1, false},
                         // 카메라가 EMERGENCY가 아닐 때는 예전과 똑같이 DANGER로 올라간다.
                         ExceptionState::EMERGENCY_IMPACT, RiskLevel::DANGER});
    }
    {
        DangerJudgmentEngine engine(testJudgmentConfig(), std::chrono::milliseconds(500));
        runCase(engine, {"카메라 300mm + ToF고장",
                         CameraInput{true, true, kNearA, kNearB, "", ""},
                         SensorInput{true, false, 0.0, 0.1, false},
                         // 최소 CAUTION 보정도 위험도를 낮추지 않는다.
                         ExceptionState::SENSOR_FAULT,     RiskLevel::EMERGENCY});
    }
}

void testCollisionRadiusContract() {
    std::cout << "\n[테스트 4] 충돌 반경 보정과 원시 거리 출력 계약\n";
    const CameraInput camera{true, true, {0.0, 0.0}, {1600.0, 0.0}, "", ""};
    const SensorInput sensor{true, true, 5000.0, 0.1, false};

    auto zero_radius = testJudgmentConfig();
    zero_radius.forklift_collision_radius_mm = 0.0;
    DangerJudgmentEngine centerDistanceEngine(zero_radius, std::chrono::milliseconds(500));
    const auto center = centerDistanceEngine.evaluate(camera, sensor);
    const bool center_ok = center.camera_risk == RiskLevel::CAUTION && center.distance_mm == 1600.0;
    if (!center_ok) ++failures;
    std::cout << (center_ok ? "  [OK]   " : "  [FAIL] ")
              << "반경 0mm이면 1600mm 중심점 거리를 그대로 CAUTION으로 판정\n";

    auto adjusted_radius = testJudgmentConfig();
    adjusted_radius.forklift_collision_radius_mm = 200.0;
    DangerJudgmentEngine adjustedEngine(adjusted_radius, std::chrono::milliseconds(500));
    const auto adjusted = adjustedEngine.evaluate(camera, sensor);
    const bool adjusted_ok = adjusted.camera_risk == RiskLevel::DANGER &&
                             adjusted.distance_mm == 1600.0;
    if (!adjusted_ok) ++failures;
    std::cout << (adjusted_ok ? "  [OK]   " : "  [FAIL] ")
              << "판정에는 유효거리 1400mm를 쓰고 결과에는 원시거리 1600mm를 유지\n";
}

} // namespace

int main() {
    std::cout << "=== DangerJudgmentEngine 예외처리 트리거 테스트 ===\n\n";

    testBoundaryValues();
    testPriorityMatrix();
    testEmergencyTierVsImpact();
    testCollisionRadiusContract();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
