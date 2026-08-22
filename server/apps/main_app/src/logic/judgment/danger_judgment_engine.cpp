// danger_judgment_engine.cpp
// 위험 판정 엔진 구현 - 공통 설정 기반 거리·센서 판정과 예외처리
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 자료구조·enum·클래스 선언은 danger_judgment_engine.h,
// 운영 임계값은 기동 시 읽은 danger_judgment 설정에서 전달받는다.
//
// 빌드 (라즈베리파이 / Linux, POSIX 전용):
//   CMake: cmake -S . -B build && cmake --build build
//   또는 직접: g++ -std=c++17 danger_judgment_engine.cpp danger_judgment_engine_main.cpp \
//              -o danger_engine -pthread
//
// 이 파일 자체는 소켓/스레드를 쓰지 않지만 nowIso8601Ms()가 gmtime_r을 쓰므로 POSIX 전용이다.
// (-pthread가 필요한 쪽은 ResultPublisher를 링크하는 main / 테스트 실행파일이다.)

#include "logic/judgment/danger_judgment_engine.h"

#include <algorithm>
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

DangerJudgmentEngine::DangerJudgmentEngine(
    const forklift::config::DangerJudgmentConfig& config,
    double collision_radius_mm,
    std::chrono::milliseconds dead_reckoning_release_grace,
    bool ignore_sensor_input)
    : caution_threshold_mm(config.caution_threshold_mm),
      danger_threshold_mm(config.danger_threshold_mm),
      emergency_threshold_mm(config.emergency_threshold_mm),
      emergency_release_margin_mm(config.emergency_release_margin_mm),
      tof_caution_mm(config.tof_caution_mm),
      tof_danger_mm(config.tof_danger_mm),
      forklift_collision_radius_mm(collision_radius_mm),
      impact_accel_threshold_g(config.impact_accel_threshold_g),
      dead_reckoning_release_grace_ms(dead_reckoning_release_grace),
      ignore_sensor_input_(ignore_sensor_input) {}

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
        const double effective_distance_mm = std::max(0.0, dist - forklift_collision_radius_mm);
        result.camera_risk = classifyByDistance(effective_distance_mm);
    } else {
        // 판정 보류 -> 예외 단계에서 보정 (SAFE로 두되, 아래 예외 로직이 실제 위험 여부를 반영)
        result.camera_risk = RiskLevel::SAFE;

        // 거리를 못 재는 프레임에서는 EMERGENCY 래치도 푼다.
        // 유지하는 쪽이 더 안전해 보이지만, 한번 걸리면 사람이 다시 잡힐 때까지 풀 방법이
        // 없어 EMERGENCY가 무기한 고착된다(전진 차단이 걸린 채로 남는다). 폐색/미검출 구간은
        // 예외 상태(DEAD_RECKONING/SENSOR_FAULT)가 최소 CAUTION을 보장하는 기존 설계에 맡긴다.
        // -> DANGER도 같은 이유로 유지되지 않으므로 EMERGENCY만 예외를 두지 않는다.
        in_emergency_ = false;
    }
    result.distance_mm = dist;

    // ── 2) ToF 기반 판정 ────────────────────────────
    if (ignore_sensor_input_) {
        // 센서 제외 테스트 모드에서는 ToF/IMU 입력을 읽거나 판정에 반영하지 않는다.
        // 센서값을 "정상"으로 위조하는 것이 아니라 카메라 판정만 남긴다.
        result.tof_risk = RiskLevel::SAFE;
    } else if (sen.tof_ok) {
        result.tof_risk = classifyByTof(sen.tof_distance_mm);
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
            // 충돌 의심 -> 최소 DANGER 보장.
            // [변경 2026-08-03] 예전엔 fused = DANGER로 못박았는데, RiskLevel에 EMERGENCY(3)가
            // 생긴 뒤로는 그 대입이 "카메라상 충돌 임박(EMERGENCY)인데 충돌까지 의심되는"
            // 최악 상황의 위험도를 오히려 3 -> 2로 낮춘다. 위험도는 worst-case로만 움직여야
            // 하므로 atLeast로 바꿨다. 카메라가 EMERGENCY가 아닐 때의 동작은 예전과 동일하고,
            // exception_state = EMERGENCY_IMPACT 태깅 자체는 전혀 건드리지 않았다.
            fused = atLeast(fused, RiskLevel::DANGER);
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

RiskLevel DangerJudgmentEngine::classifyByDistance(double dist_mm) const {
    // ── EMERGENCY 구간만 히스테리시스를 적용한다 ──────────────────
    // 진입: dist <= emergency_threshold_mm
    // 해제: dist >  emergency_threshold_mm + emergency_release_margin_mm
    // 사이 구간(0.4 < dist <= 0.5)은 "직전에 EMERGENCY였는지"에 따라 갈린다.
    //
    // 위험도가 올라가는 방향은 지연 없이 즉시, 내려오는 방향만 신중하게 —
    // FPGA warning_latch(PROTOCOL.md 1.1절)가 시간 축에서 쓰는 원칙과 같고,
    // 여기서는 거리 축에서 같은 원칙을 적용한 것이다.
    //
    // [주의] SAFE<->CAUTION<->DANGER 경계에는 아직 히스테리시스가 없다(예전부터 없었음).
    //        같은 방식을 나머지 경계로 넓히려면 아래 진입/해제 구조를 경계별로 반복하면 되고,
    //        그때는 마지막 단계를 하나만 기억하는 in_emergency_ 대신 직전 RiskLevel 전체를
    //        들고 있어야 한다.
    const double emergency_enter_mm   = emergency_threshold_mm;
    const double emergency_release_mm = emergency_threshold_mm + emergency_release_margin_mm;

    if (in_emergency_) {
        if (dist_mm <= emergency_release_mm) return RiskLevel::EMERGENCY;  // 해제 조건 미달
        in_emergency_ = false;                                           // 충분히 멀어짐 -> 해제
    } else if (dist_mm <= emergency_enter_mm) {
        in_emergency_ = true;
        return RiskLevel::EMERGENCY;
    }

    if (dist_mm <= danger_threshold_mm)  return RiskLevel::DANGER;
    if (dist_mm <= caution_threshold_mm) return RiskLevel::CAUTION;
    return RiskLevel::SAFE;
}

RiskLevel DangerJudgmentEngine::classifyByTof(double dist_mm) const {
    if (dist_mm <= tof_danger_mm)  return RiskLevel::DANGER;
    if (dist_mm <= tof_caution_mm) return RiskLevel::CAUTION;
    return RiskLevel::SAFE;
}

ExceptionState DangerJudgmentEngine::detectException(const CameraInput& cam, const SensorInput& sen,
                                                     RiskLevel tof_risk) const {
    // 우선순위: 충돌 의심 > 센서 고장 > dead-reckoning > 대상 미확인 근접
    if (!ignore_sensor_input_ && sen.imu_accel_g >= impact_accel_threshold_g) {
        return ExceptionState::EMERGENCY_IMPACT;
    }
    if (!ignore_sensor_input_ && (!sen.imu_ok || !sen.tof_ok)) {
        return ExceptionState::SENSOR_FAULT;
    }

    // ── DEAD_RECKONING 진입/해제 히스테리시스 ─────────────────────
    // 진입은 즉시(안전 방향이라 늦추면 안 됨), 해제만 dead_reckoning_release_grace_ms
    // 동안 미확보 프레임이 한 번도 없어야 반영한다. classifyByDistance()의 in_emergency_
    // 히스테리시스와 같은 원칙이고, 축만 거리 대신 시간이다.
    const bool raw_dead_reckoning = !cam.forklift_localized ||
                                    (!ignore_sensor_input_ && sen.is_dead_reckoning);
    if (raw_dead_reckoning) {
        dead_reckoning_active_ = true;
        last_dead_reckoning_time_ = std::chrono::steady_clock::now();
        return ExceptionState::DEAD_RECKONING;
    }
    if (dead_reckoning_active_) {
        const auto elapsed = std::chrono::steady_clock::now() - last_dead_reckoning_time_;
        if (elapsed < dead_reckoning_release_grace_ms) {
            return ExceptionState::DEAD_RECKONING;  // 해제 유예 중 - 아직 grace 시간 미달
        }
        dead_reckoning_active_ = false;  // grace 시간 경과 -> 해제, 다음 판정으로 진행
    }

    // [신규] 지게차 좌표는 정상인데 카메라에 사람이 안 잡히고, ToF는 근접(CAUTION 이상)을 보고 -> 미확인 근접
    if (!ignore_sensor_input_ && !cam.person_detected && tof_risk != RiskLevel::SAFE) {
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
        case RiskLevel::SAFE:      return "SAFE";
        case RiskLevel::CAUTION:   return "CAUTION";
        case RiskLevel::DANGER:    return "DANGER";
        case RiskLevel::EMERGENCY: return "EMERGENCY";
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
              << "| dist=" << std::fixed << std::setprecision(2) << std::setw(6) << r.distance_mm
              // 폭 9 = 가장 긴 이름("EMERGENCY") 기준. 좁으면 그 줄만 열이 밀린다.
              << "| cam=" << std::setw(9) << toString(r.camera_risk)
              << "| tof=" << std::setw(9) << toString(r.tof_risk)
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
       << "\"zone\":" << toJsonOrNull(r.zone) << ','
       << "\"terminal_id\":" << toJsonOrNull(r.terminal_id) << ','
       << "\"stream_id\":" << toJsonOrNull(r.stream_id) << ','
       << "\"camera_id\":" << toJsonOrNull(r.source_camera_id.empty() ? r.camera_id : r.source_camera_id) << ','
       << "\"channel\":" << r.channel << ','
       << "\"exception_state\":\"" << toString(r.exception) << "\",";
    // distance_mm은 sentinel(-1)을 유효 측정값으로 오독하지 않도록 음수면 null, 아니면 숫자 그대로.
    os << "\"distance_mm\":";
    if (r.distance_mm < 0) os << "null"; else os << r.distance_mm;
    // risk_level은 문자열이 아니라 정수(0=SAFE/1=CAUTION/2=DANGER/3=EMERGENCY)로 내보낸다.
    // 단말(Qt RiskMetadata::fromJson)이 toInt()로 읽으므로 문자열을 보내면
    // 항상 0(Safe)으로 떨어져 위험 경보가 통째로 유실된다.
    // enum 값이 곧 위험도 순서라 static_cast<int>가 그대로 계약 값이 된다.
    os << ','
       << "\"risk_level\":" << static_cast<int>(r.final_risk);
    // 분산 관측용 확장 필드는 구형 단말이 모르는 키를 무시할 수 있도록 optional로
    // 붙인다. 기존 7개 필드의 의미·순서·타입은 그대로 유지한다.
    if (!r.server_run_id.empty()) {
        os << ",\"server_run_id\":\"" << r.server_run_id << "\"";
    }
    if (!r.decision_id.empty()) {
        os << ",\"decision_id\":\"" << r.decision_id << "\"";
    }
    if (r.publish_seq > 0) {
        os << ",\"publish_seq\":" << r.publish_seq;
    }
    if (!r.send_reason.empty()) {
        os << ",\"send_reason\":\"" << r.send_reason << "\"";
    }
    if (!r.sensor_message_id.empty()) {
        os << ",\"sensor_message_id\":\"" << r.sensor_message_id << "\"";
    }
    if (!r.sensor_producer_run_id.empty()) {
        os << ",\"sensor_producer_run_id\":\"" << r.sensor_producer_run_id << "\"";
    }
    if (r.sensor_sequence >= 0) {
        os << ",\"sensor_sequence\":" << r.sensor_sequence;
    }
    if (r.sensor_ts_ms > 0) {
        os << ",\"sensor_ts_ms\":" << r.sensor_ts_ms;
    }
    if (r.sensor_age_ms >= 0) {
        os << ",\"sensor_age_ms\":" << r.sensor_age_ms;
    }
    os << ",\"schema_version\":1"
       << '}';
    return os.str();
}
