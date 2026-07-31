// danger_judgment_engine.h
// 위험 판정 엔진 - 공개 인터페이스 (데이터 구조 + 엔진 클래스 + 출력/직렬화 헬퍼)
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
// 구현은 danger_judgment_engine.cpp, 실행용 main()은 danger_judgment_engine_main.cpp에 있다.
// 이 헤더 자체는 표준 헤더만 쓰므로 POSIX 의존성이 없다.
// (nowIso8601Ms()의 gmtime_r, ResultPublisher의 소켓 의존성은 각 .cpp 쪽에 남아 있다.)

#pragma once

#include <string>

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
    JudgmentResult evaluate(const CameraInput& cam, const SensorInput& sen) const;

private:
    static double euclideanDistance(const WorldPoint& a, const WorldPoint& b);

    RiskLevel classifyByDistance(double dist_m) const;
    RiskLevel classifyByTof(double dist_m) const;

    ExceptionState detectException(const CameraInput& cam, const SensorInput& sen,
                                   RiskLevel tof_risk) const;

    // 둘 중 더 위험한 단계 반환 (worst-case 우선순위 원칙)
    static RiskLevel worstOf(RiskLevel a, RiskLevel b);

    // r이 minimum보다 낮으면 minimum까지 끌어올림
    static RiskLevel atLeast(RiskLevel r, RiskLevel minimum);
};

// ============================================================
// 3. 출력 / 직렬화 헬퍼
// ============================================================

std::string toString(RiskLevel r);
std::string toString(ExceptionState e);

// 한 프레임 판정 결과를 콘솔에 한 줄로 (시나리오 이름 + 내부 디버깅 값 포함)
void printResult(const std::string& scenario, const JudgmentResult& r);

// 현재 UTC 시각을 ISO8601 + 밀리초 문자열로 반환 (예: "2026-07-28T10:15:30.123Z").
// gmtime_r 사용(POSIX 전용).
std::string nowIso8601Ms();

// "미확정/미연결이면 null"로 나가야 하는 문자열 필드(camera_id, zone)를 직렬화한다.
// 빈 문자열("")과 값 없음을 하류에서 구분할 수 있도록 빈 값은 null로 통일한다.
std::string toJsonOrNull(const std::string& s);

// 판정 결과를 JSON 한 줄(line-delimited)로 직렬화.
// ResultPublisher::publish()가 const std::string&를 받으므로 반환 타입은 std::string.
// 외부 JSON 라이브러리 없이 표준 C++17만으로 수동 직렬화한다.
//
// 필드명·구성은 네트워크·단말 파트(Qt RiskMetadata::fromJson)와 확정된 스키마를 따른다.
// 확정된 출력 필드는 아래 6개가 전부다:
//   utc_time / camera_id / zone / exception_state / distance_m / risk_level
// - risk_level은 정수(0=SAFE/1=CAUTION/2=DANGER)다. 단말이 toInt()로 읽는 계약이므로
//   문자열로 내보내면 안 된다. 콘솔 로그용 문자열 표기는 toString(RiskLevel) 쪽에 남아 있다.
// - world 좌표·bbox는 단말에서 쓰지 않기로 협의되어 제외.
// - camera_risk / tof_risk는 서버 내부 디버깅용이므로 콘솔 로그(printResult)에만 남기고 제외.
// - 테스트 시나리오 이름표도 프로덕션 스키마에 없으므로 제외.
std::string toJson(const JudgmentResult& r);
