// danger_judgment_engine.cpp
// 위험 판정 엔진 - 더미 데이터 기반 프로토타입 (v2: 예외처리 보강)
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// v2 변경점 (기존 v1 대비):
//   1) CameraInput.available -> forklift_localized / person_detected 로 분리
//      - 기존엔 "마커 폐색"과 "사람이 화면에 없음"이 같은 플래그로 뭉뚱그려져 있었음
//   2) 새 예외 상태 UNCONFIRMED_PROXIMITY 추가
//      - 카메라엔 사람이 안 잡히는데 ToF만 근접 경보를 낼 때 사용
//      - "ToF 미구분 문제"(사람/벽/적재물 구분 불가)를 숨기지 않고 명시적으로 태깅
//      - 최종 위험도 자체는 기존과 동일하게 worst-case로 올라가되, exception_state로
//        "사람인지 확인 안 됨"을 하류(단말/로그)에 전달 -> 오탐 설명·후속 대응 가능
//
// 빌드 (라즈베리파이 / Linux, POSIX 전용):
//   g++ -std=c++17 danger_judgment_engine.cpp -o danger_engine -pthread
//   또는 CMake: cmake -S . -B build && cmake --build build
//
// 외부 라이브러리 의존성은 없으나, ResultPublisher가 POSIX 소켓 + std::thread를
// 사용하므로 -pthread 링크가 필요하고 Windows/MSVC 네이티브 빌드는 지원하지 않는다.
// (기존 순수 C++17 콘솔 빌드에서 TCP 결과 송신 기능이 추가된 버전)

#include <iostream>
#include <cmath>
#include <string>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ctime>
#include <thread>   // main()의 std::this_thread::sleep_for용
                    // (지금까지는 ResultPublisher.h가 간접 include해줘서 우연히 컴파일됐음)

#include "ResultPublisher.h"  // 판정 결과를 TCP로 하류(단말/서버)에 송신

// ============================================================
// 1. 데이터 구조 정의
// ============================================================

// 바닥 평면 위 실제 좌표 (미터 단위)
// [교체 지점] 현재는 더미 값. 좌표정합 PoC 완료 후
//              cv::perspectiveTransform(호모그래피 변환) 출력값으로 교체.
struct WorldPoint {
    double x = 0.0;
    double y = 0.0;
};

// 위험 단계 3단계 (숫자가 클수록 위험)
enum class RiskLevel {
    SAFE = 0,      // 안전
    CAUTION = 1,   // 주의
    DANGER = 2     // 위험
};

// 예외 상태 (센서 퓨전 특이 상황)
enum class ExceptionState {
    NONE,
    SENSOR_FAULT,          // 센서 고장 (IMU/ToF 응답 없음, 값 이상)
    DEAD_RECKONING,        // ArUco 마커 폐색 중 IMU 추정값 사용 중
    EMERGENCY_IMPACT,      // 급격한 가속도 변화 -> 충돌 의심
    UNCONFIRMED_PROXIMITY  // [신규] 카메라엔 사람 미검출인데 ToF만 근접 경보 -> 대상 미확인
};

// 카메라(ONVIF 메타데이터) 기반 입력
// [교체 지점] 실제로는 OnvifMetadataReassembler + pugixml 파서에서 나온
//              BoundingBox::groundX()/groundY()를 호모그래피 변환한 결과가 들어옴.
//              지게차 좌표는 박명수 파트(ArUco)에서 넘어옴.
struct CameraInput {
    // 예전 필드명은 "available" 하나였는데, 서로 다른 두 상황(마커 폐색 vs 사람 미검출)이
    // 뭉뚱그려져 있어서 분리함.
    bool       forklift_localized = true;  // 지게차 world 좌표를 이번 프레임에 얻었는지 (false=마커 폐색)
    bool       person_detected = true;     // 이번 프레임에 사람이 검출됐는지 (false=화면에 없음/미검출)
    // world 좌표는 하류로 내보내지 않고(팀 협의로 JSON에서 제외됨) 내부 거리 계산에만 쓴다.
    WorldPoint forklift;                   // 지게차 world 좌표 (forklift_localized=false면 stale/무의미)
    WorldPoint person;                     // 사람 world 좌표 (person_detected=false면 stale/무의미)

    // camera_id 표기 방식: nearest_person_selector.cpp의 int를 std::to_string()으로 단순 변환.
    // 라벨링(cam_01 등) 필요 시 여기서 변경.
    // [TODO] 현재 이 값을 채워주는 통합 지점이 아직 없다(각 파일이 독립 main으로 동작 중).
    //        값을 임의로 만들지 않고 빈 문자열로 둔다.
    std::string camera_id;

    // [TODO] zone 매핑 미확정(김진석) — 값 확정 전까지 항상 null
    std::string zone;
};

// IMU/ToF 센서 기반 입력 (I2C로 직접 읽음)
// [교체 지점] 현재는 더미 값. 실제로는 MPU6050(I2C 0x68)/VL53L0X(I2C 0x29)
//              드라이버 읽기 값으로 교체 (라즈베리파이에서만 가능).
struct SensorInput {
    bool   imu_ok = true;             // IMU 응답 정상 여부
    bool   tof_ok = true;             // ToF 응답 정상 여부
    double tof_distance_m = 5.0;      // ToF가 측정한 최근접 거리(m)
    double imu_accel_g = 0.0;         // IMU 가속도 크기 (g 단위, 급정지/충돌 감지용)
    bool   is_dead_reckoning = false; // 마커 폐색 등으로 IMU 추정 모드 진입 여부
};

// 한 프레임의 판정 결과
struct JudgmentResult {
    RiskLevel      camera_risk;
    RiskLevel      tof_risk;
    RiskLevel      final_risk;
    ExceptionState exception;
    double         distance_m;      // 참고용 (카메라 기준 유클리드 거리, 폐색/미검출 시 -1)

    // world 좌표/bbox는 단말로 내보내지 않기로 팀 협의됨 -> 결과 구조체에서도 제거.
    // (거리 계산에 필요한 원본 좌표는 CameraInput에만 남아 있다.)

    std::string    camera_id;       // cam.camera_id 그대로 전달 (현재는 상류 미연결 -> 빈 문자열)
    std::string    zone;            // cam.zone 그대로 전달 (매핑 미확정 -> 빈 문자열 -> JSON null)
};

// ============================================================
// 2. 위험 판정 엔진
// ============================================================

class DangerJudgmentEngine {
public:
    // 거리 임계값 (미터) - 확정된 실험 4종 결과로 추후 보정 예정
    double caution_threshold_m = 3.0;  // 이 거리 이내면 주의
    double danger_threshold_m  = 1.5;  // 이 거리 이내면 위험

    // ToF 근접 임계값
    double tof_caution_m = 1.0;
    double tof_danger_m  = 0.5;

    // IMU 급정지/충돌 판정 임계값 (g)
    double impact_accel_threshold_g = 2.0;

    // 한 프레임 처리: 카메라 입력 + 센서 입력 -> 최종 판정
    JudgmentResult evaluate(const CameraInput& cam, const SensorInput& sen) const {
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

private:
    static double euclideanDistance(const WorldPoint& a, const WorldPoint& b) {
        double dx = a.x - b.x;
        double dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    RiskLevel classifyByDistance(double dist_m) const {
        if (dist_m <= danger_threshold_m)  return RiskLevel::DANGER;
        if (dist_m <= caution_threshold_m) return RiskLevel::CAUTION;
        return RiskLevel::SAFE;
    }

    RiskLevel classifyByTof(double dist_m) const {
        if (dist_m <= tof_danger_m)  return RiskLevel::DANGER;
        if (dist_m <= tof_caution_m) return RiskLevel::CAUTION;
        return RiskLevel::SAFE;
    }

    ExceptionState detectException(const CameraInput& cam, const SensorInput& sen,
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

    // 둘 중 더 위험한 단계 반환 (worst-case 우선순위 원칙)
    static RiskLevel worstOf(RiskLevel a, RiskLevel b) {
        return static_cast<int>(a) >= static_cast<int>(b) ? a : b;
    }

    // r이 minimum보다 낮으면 minimum까지 끌어올림
    static RiskLevel atLeast(RiskLevel r, RiskLevel minimum) {
        return static_cast<int>(r) >= static_cast<int>(minimum) ? r : minimum;
    }
};

// ============================================================
// 3. 출력 헬퍼
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

// 현재 UTC 시각을 ISO8601 + 밀리초 문자열로 반환 (예: "2026-07-28T10:15:30.123Z").
// gmtime_r 사용(POSIX 전용) - 이 파일은 ResultPublisher.h 포함으로 이미 POSIX 전용이라 무방.
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

// "미확정/미연결이면 null"로 나가야 하는 문자열 필드(camera_id, zone)를 직렬화한다.
// 빈 문자열("")과 값 없음을 하류에서 구분할 수 있도록 빈 값은 null로 통일한다.
std::string toJsonOrNull(const std::string& s) {
    if (s.empty()) return "null";
    return '"' + s + '"';
}

// 판정 결과를 JSON 한 줄(line-delimited)로 직렬화.
// ResultPublisher::publish()가 const std::string&를 받으므로 반환 타입은 std::string.
// 외부 JSON 라이브러리 없이 표준 C++17만으로 수동 직렬화한다.
//
// 필드명·구성은 네트워크·단말 파트(Qt RiskMetadata::fromJson)와 확정된 스키마를 따른다.
// 확정된 출력 필드는 아래 6개가 전부다:
//   utc_time / camera_id / zone / exception_state / distance_m / risk_level
// - world 좌표·bbox는 단말에서 쓰지 않기로 협의되어 제외.
// - camera_risk / tof_risk는 서버 내부 디버깅용이므로 콘솔 로그(printResult)에만 남기고 제외.
// - 테스트 시나리오 이름표도 프로덕션 스키마에 없으므로 제외.
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
    os << ','
       << "\"risk_level\":\"" << toString(r.final_risk) << "\""
       << '}';
    return os.str();
}

// ============================================================
// 4. 테스트 시나리오 (더미 데이터)
// ============================================================

int main() {
    DangerJudgmentEngine engine;

    // 판정 결과 TCP 송신기.
    // [교체 지점] 호스트/포트는 현재 로컬 더미값. 실제 배포 시 단말/수신 서버 주소로 교체
    //             (예: 환경변수 또는 설정파일에서 로드).
    risk_transport::ResultPublisher publisher("127.0.0.1", 9000);
    publisher.onStateChange([](risk_transport::LinkState s) {
        const char* name = s == risk_transport::LinkState::CONNECTED    ? "CONNECTED"
                         : s == risk_transport::LinkState::CONNECTING   ? "CONNECTING"
                                                                        : "DISCONNECTED";
        std::cerr << "[publisher] link state -> " << name << "\n";
    });
    publisher.start();

    // 판정 루프 한 스텝: 평가 -> 콘솔 출력 -> JSON 직렬화 후 TCP 송신.
    // (수신 서버가 없어도 publisher는 백그라운드에서 재접속을 시도하며 논블로킹으로 동작)
    // 시나리오 이름(name)은 콘솔 로그에만 쓰고, JSON 본문에는 넣지 않는다(프로덕션 스키마에 없음).
    auto step = [&](const std::string& name, const CameraInput& cam, const SensorInput& sen) {
        JudgmentResult r = engine.evaluate(cam, sen);
        printResult(name, r);
        publisher.publish(toJson(r));
    };

    std::cout << "=== 위험 판정 엔진 - 더미 데이터 테스트 (v2) ===\n\n";

    // CameraInput 인자 순서:
    //   forklift_localized, person_detected, forklift, person, camera_id, zone
    // camera_id/zone은 아직 상류 배선이 없어 전 시나리오 공통으로 빈 문자열이다.

    // 시나리오 1: 정상 - 안전 거리
    step("1. 정상-안전",
         CameraInput{true, true, {0.0, 0.0}, {5.0, 5.0}, "", ""},   // 거리 약 7.07m
         SensorInput{true, true, 5.0, 0.1, false});

    // 시나리오 2: 정상 - 주의 거리
    step("2. 정상-주의",
         CameraInput{true, true, {0.0, 0.0}, {2.0, 1.5}, "", ""},   // 거리 약 2.50m
         SensorInput{true, true, 3.0, 0.1, false});

    // 시나리오 3: 정상 - 위험 거리
    step("3. 정상-위험",
         CameraInput{true, true, {0.0, 0.0}, {1.0, 1.0}, "", ""},   // 거리 약 1.41m
         SensorInput{true, true, 2.0, 0.1, false});

    // 시나리오 4: 카메라는 안전인데 ToF는 위험 -> worst-case로 위험 채택
    step("4. 카메라SAFE/ToF위험",
         CameraInput{true, true, {0.0, 0.0}, {10.0, 10.0}, "", ""}, // 카메라 거리 멀어서 SAFE
         SensorInput{true, true, 0.3, 0.1, false});          // ToF 근접 -> DANGER

    // 시나리오 5: 센서 고장 (ToF 응답 없음) -> 최소 CAUTION 유지
    step("5. ToF 고장",
         CameraInput{true, true, {0.0, 0.0}, {8.0, 8.0}, "", ""},   // 카메라 상 안전
         SensorInput{true, false, 0.0, 0.1, false});         // ToF 고장

    // 시나리오 6: 마커 폐색 -> dead-reckoning 모드, 최소 CAUTION 유지
    step("6. 마커폐색(DR)",
         CameraInput{false, true, {0.0, 0.0}, {0.0, 0.0}, "", ""},  // 지게차 좌표 없음(폐색)
         SensorInput{true, true, 4.0, 0.1, true});           // IMU 추정 모드

    // 시나리오 7: 급정지/충돌 의심 -> 카메라·ToF와 무관하게 무조건 DANGER
    step("7. 충돌의심(급가속도)",
         CameraInput{true, true, {0.0, 0.0}, {9.0, 9.0}, "", ""},   // 카메라 상 안전
         SensorInput{true, true, 5.0, 3.5, false});          // 급격한 가속도 변화

    // 시나리오 8 [신규]: 지게차 좌표는 정상인데 사람이 카메라에 안 잡힘 + ToF만 근접 경보
    //                    -> UNCONFIRMED_PROXIMITY, 최종 위험도는 ToF 기준으로 DANGER 유지
    step("8. 사람미검출+ToF근접",
         CameraInput{true, false, {0.0, 0.0}, {0.0, 0.0}, "", ""},  // person_detected=false
         SensorInput{true, true, 0.4, 0.1, false});          // ToF 근접(0.4m) -> DANGER

    // 시나리오 9 [신규]: 지게차 좌표 정상, 사람도 안 잡히고, ToF도 멀리(SAFE) -> 그냥 정상 SAFE
    //                    (진짜로 주변에 아무도 없는 정상 상황과 구분되어야 함)
    step("9. 사람미검출+ToF안전",
         CameraInput{true, false, {0.0, 0.0}, {0.0, 0.0}, "", ""},
         SensorInput{true, true, 5.0, 0.1, false});          // ToF도 멀리 -> SAFE

    std::cout << "\n=== 테스트 종료 ===\n";

    // 백그라운드 송신 스레드가 pending 결과를 flush할 시간을 잠깐 준 뒤 정리.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    publisher.stop();  // 소멸자에서도 호출되지만 명시적으로 정리
    return 0;
}
