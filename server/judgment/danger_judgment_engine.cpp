// danger_judgment_engine.cpp
// 위험 판정 엔진 구현 - 더미 데이터 기반 프로토타입 (v2: 예외처리 보강)
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 자료구조·enum·클래스 선언은 danger_judgment_engine.h,
// 실행용 main()과 더미 시나리오는 danger_judgment_engine_main.cpp에 있다.
//
// 빌드 (라즈베리파이 / Linux, POSIX 전용):
//   CMake: cmake -S . -B build && cmake --build build
//   또는 직접: g++ -std=c++17 danger_judgment_engine.cpp danger_judgment_engine_main.cpp \
//              -o danger_engine -pthread
//
// 이 파일 자체는 소켓/스레드를 쓰지 않지만 nowIso8601Ms()가 gmtime_r을 쓰므로 POSIX 전용이다.
// (-pthread가 필요한 쪽은 ResultPublisher를 링크하는 main / 테스트 실행파일이다.)

#include "danger_judgment_engine.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// ============================================================
// 1. 위험 판정 엔진
// ============================================================

JudgmentResult DangerJudgmentEngine::evaluate(const CameraInput& cam, const SensorInput& sen) const {
    JudgmentResult result;

    // ── 0) 판정에 관여하지 않는 식별 정보는 그대로 통과시킨다 ────────
    result.camera_id = cam.camera_id;
    result.zone      = cam.zone;

    // ── 1) 카메라 기반 판정 ──────────────────────────
    // 지게차 좌표가 없거나(마커 폐색) 사람이 안 잡히면 카메라 기준 거리 계산 자체가 불가능
    double dist = -1.0;
    if (cam.forklift_localized && cam.person_detected) {
        dist = euclideanDistance(cam.forklift, cam.person);
        result.camera_risk = classifyByDistance(dist);
    } else {
        // 판정 보류 -> 예외 단계에서 보정 (SAFE로 두되, 아래 예외 로직이 실제 위험 여부를 반영)
        result.camera_risk = RiskLevel::SAFE;
    }
    result.distance_m = dist;

    // ── 2) ToF 기반 판정 ────────────────────────────
    if (sen.tof_ok) {
        result.tof_risk = classifyByTof(sen.tof_distance_m);
    } else {
        result.tof_risk = RiskLevel::SAFE; // 판정 불가 -> 예외 단계에서 보정
    }

    // ── 3) 예외 상태 판별 (ToF 판정 결과가 필요하므로 2번 이후에 수행) ──
    result.exception = detectException(cam, sen, result.tof_risk);

    // ── 4) worst-case 우선순위 센서 퓨전 ─────────────
    RiskLevel fused = worstOf(result.camera_risk, result.tof_risk);

    // ── 5) 예외 상태에 따른 보정 ─────────────────────
    switch (result.exception) {
        case ExceptionState::EMERGENCY_IMPACT:
            // 충돌 의심 -> 무조건 최고 위험 단계
            fused = RiskLevel::DANGER;
            break;
        case ExceptionState::SENSOR_FAULT:
        case ExceptionState::DEAD_RECKONING:
            // 판정 신뢰도가 떨어지는 상황 -> 보수적으로 최소 "주의" 유지
            fused = atLeast(fused, RiskLevel::CAUTION);
            break;
        case ExceptionState::UNCONFIRMED_PROXIMITY:
            // ToF가 뭔가를 감지했지만 카메라로 사람인지 확인이 안 된 상태.
            // 안전 우선 원칙상 fused(이미 ToF 기준 위험도 반영됨)는 그대로 유지하고,
            // exception_state 태그로만 "확인 필요"를 하류에 전달한다.
            // [확실하지 않음] 이걸 단순 태깅에 그칠지, 별도 경고 레벨(예: 약한 경고)로
            // 낮출지는 팀 논의 필요 (오탐 시 운전자 경고 피로도 문제).
            break;
        case ExceptionState::NONE:
        default:
            break;
    }

    result.final_risk = fused;
    return result;
}

double DangerJudgmentEngine::euclideanDistance(const WorldPoint& a, const WorldPoint& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

RiskLevel DangerJudgmentEngine::classifyByDistance(double dist_m) const {
    if (dist_m <= danger_threshold_m)  return RiskLevel::DANGER;
    if (dist_m <= caution_threshold_m) return RiskLevel::CAUTION;
    return RiskLevel::SAFE;
}

RiskLevel DangerJudgmentEngine::classifyByTof(double dist_m) const {
    if (dist_m <= tof_danger_m)  return RiskLevel::DANGER;
    if (dist_m <= tof_caution_m) return RiskLevel::CAUTION;
    return RiskLevel::SAFE;
}

ExceptionState DangerJudgmentEngine::detectException(const CameraInput& cam, const SensorInput& sen,
                                                     RiskLevel tof_risk) const {
    // 우선순위: 충돌 의심 > 센서 고장 > dead-reckoning > 대상 미확인 근접
    if (sen.imu_accel_g >= impact_accel_threshold_g) {
        return ExceptionState::EMERGENCY_IMPACT;
    }
    if (!sen.imu_ok || !sen.tof_ok) {
        return ExceptionState::SENSOR_FAULT;
    }
    if (!cam.forklift_localized || sen.is_dead_reckoning) {
        return ExceptionState::DEAD_RECKONING;
    }
    // [신규] 지게차 좌표는 정상인데 카메라에 사람이 안 잡히고, ToF는 근접(CAUTION 이상)을 보고 -> 미확인 근접
    if (!cam.person_detected && tof_risk != RiskLevel::SAFE) {
        return ExceptionState::UNCONFIRMED_PROXIMITY;
    }
    return ExceptionState::NONE;
}

RiskLevel DangerJudgmentEngine::worstOf(RiskLevel a, RiskLevel b) {
    return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
}

RiskLevel DangerJudgmentEngine::atLeast(RiskLevel r, RiskLevel minimum) {
    return static_cast<int>(r) >= static_cast<int>(minimum) ? r : minimum;
}

// ============================================================
// 2. 출력 / 직렬화 헬퍼
// ============================================================

std::string toString(RiskLevel r) {
    switch (r) {
        case RiskLevel::SAFE:    return "SAFE";
        case RiskLevel::CAUTION: return "CAUTION";
        case RiskLevel::DANGER:  return "DANGER";
    }
    return "UNKNOWN";
}

std::string toString(ExceptionState e) {
    switch (e) {
        case ExceptionState::NONE:                 return "NONE";
        case ExceptionState::SENSOR_FAULT:         return "SENSOR_FAULT";
        case ExceptionState::DEAD_RECKONING:       return "DEAD_RECKONING";
        case ExceptionState::EMERGENCY_IMPACT:     return "EMERGENCY_IMPACT";
        case ExceptionState::UNCONFIRMED_PROXIMITY:return "UNCONFIRMED_PROXIMITY";
    }
    return "UNKNOWN";
}

void printResult(const std::string& scenario, const JudgmentResult& r) {
    std::cout << std::left << std::setw(26) << scenario
              << "| dist=" << std::fixed << std::setprecision(2) << std::setw(6) << r.distance_m
              << "| cam=" << std::setw(8) << toString(r.camera_risk)
              << "| tof=" << std::setw(8) << toString(r.tof_risk)
              << "| exc=" << std::setw(22) << toString(r.exception)
              << "| FINAL=" << toString(r.final_risk)
              << "\n";
}

std::string nowIso8601Ms() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);

    std::ostringstream os;
    os << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return os.str();
}

std::string toJsonOrNull(const std::string& s) {
    if (s.empty()) return "null";
    return '"' + s + '"';
}

std::string toJson(const JudgmentResult& r) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2);
    os << '{'
       << "\"utc_time\":\"" << nowIso8601Ms() << "\","
       << "\"camera_id\":" << toJsonOrNull(r.camera_id) << ','
       << "\"zone\":" << toJsonOrNull(r.zone) << ','
       << "\"exception_state\":\"" << toString(r.exception) << "\",";
    // distance_m은 sentinel(-1)을 유효 측정값으로 오독하지 않도록 음수면 null, 아니면 숫자 그대로.
    os << "\"distance_m\":";
    if (r.distance_m < 0) os << "null"; else os << r.distance_m;
    // risk_level은 문자열이 아니라 정수(0=SAFE/1=CAUTION/2=DANGER)로 내보낸다.
    // 단말(Qt RiskMetadata::fromJson)이 toInt()로 읽으므로 문자열을 보내면
    // 항상 0(Safe)으로 떨어져 위험 경보가 통째로 유실된다.
    // enum 값이 곧 위험도 순서라 static_cast<int>가 그대로 계약 값이 된다.
    os << ','
       << "\"risk_level\":" << static_cast<int>(r.final_risk)
       << '}';
    return os.str();
}
