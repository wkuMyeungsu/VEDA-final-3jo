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

#include "common/types.hpp"
#include "common/latency_stamps.hpp"  // LatencyStamps (서버 내부 지연 계측, JudgmentResult에 얹는다)

// ============================================================
// 1. 데이터 구조 정의
// ============================================================

// 바닥 평면 위 실제 좌표 (미터 단위)
// [교체 지점] 현재는 더미 값. 좌표정합 PoC 완료 후
//              cv::perspectiveTransform(호모그래피 변환) 출력값으로 교체.
using WorldPoint = forklift::common::WorldPoint;

// 위험 단계 4단계 (숫자가 클수록 위험)
//
// [확장 2026-08-03] EMERGENCY(3) 추가.
//   FPGA(gpio-control/PROTOCOL.md)와 단말 Qt(RiskTypes::RiskLevel)는 이미 risk_level
//   0~3을 전제로 설계돼 있었는데 서버 enum이 3단계뿐이라 3에 도달할 방법이 없었다.
//   이름은 단말 Qt 쪽(Emergency)을 따랐다. FPGA 문서는 같은 값 3을 CRITICAL로 부르며,
//   전진 차단 릴레이(cutoff_trigger)가 이 값에서 걸린다.
//
// [주의] EMERGENCY는 "충돌 임박 거리"를 뜻하는 거리 기반 단계이고,
//        ExceptionState::EMERGENCY_IMPACT(IMU 급가속도 -> 충돌 의심)와는 완전히 별개 축이다.
//        이름만 비슷할 뿐 필드(risk_level vs exception_state)도, 입력(카메라 거리 vs IMU)도 다르다.
enum class RiskLevel {
    SAFE = 0,      // 안전
    CAUTION = 1,   // 주의
    DANGER = 2,    // 위험
    EMERGENCY = 3  // 비상 (충돌 임박 거리) - FPGA 문서상 명칭은 CRITICAL
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

    // 이 판정을 보낼 단말(운전자석) 식별자. CameraInput을 거치지 않고
    // JudgmentPipeline이 evaluate() 호출 후 직접 채운다(생성자 인자로 받은 값을 그대로 대입) -
    // 단말이 여러 대일 때 "이 메시지가 어느 단말 것인지" 구분하기 위해 도입됨 (2026-08-06 팀 합의).
    // 빈 문자열 -> JSON null (camera_id/zone과 동일 규약).
    std::string    terminal_id;

    // 서버 내부 지연 계측 스탬프 (t0_ingest/t1_judge_in은 호출부가 채우고, t2_send는
    // ResultDispatcher::submit()이 찍는다). evaluate()는 이 필드를 건드리지 않는다 -
    // 기본값(에폭 0시)인 채로 반환되며, JudgmentPipeline::processFrame()이 반환 후에 채운다.
    // 맨 끝에 추가한 이유: 기존 aggregate 초기화(JudgmentResult r{};)와 필드 순서를
    // 건드리지 않기 위해서다. JSON 직렬화(toJson) 스키마에는 포함하지 않는다(하류 계약 밖).
    LatencyStamps  latency;
};

// ============================================================
// 2. 위험 판정 엔진
// ============================================================

class DangerJudgmentEngine {
public:
    // 거리 임계값 (미터) - 확정된 실험 4종 결과로 추후 보정 예정
    double caution_threshold_m = 3.0;  // 이 거리 이내면 주의
    double danger_threshold_m  = 1.5;  // 이 거리 이내면 위험

    // [목업 상수 - 실측값으로 교체 필요] 충돌 임박 판정 거리.
    // 이 거리 이내면 EMERGENCY(3). 0.4m는 근거 있는 측정값이 아니라 자리표시용 값이며,
    // 좌표정합 PoC + 제동거리 실측(확정된 실험 4종)이 끝나면 그 값으로 교체한다.
    // 교체 시 danger_threshold_m(1.5) 아래를 유지해야 한다(같거나 크면 EMERGENCY가
    // DANGER 구간을 통째로 삼킨다).
    double emergency_threshold_m = 0.4;

    // [목업 상수 - 실측값으로 교체 필요] EMERGENCY 해제 마진 (히스테리시스).
    // 한 번 EMERGENCY에 들어가면 거리가 (emergency_threshold_m + 이 값)을 넘어야 내려온다.
    // 임계값 근처에서 좌표가 미세하게 흔들릴 때 3<->2가 프레임마다 진동하는 걸 막는다
    // (FPGA는 위험도 상승을 즉시 반영하므로 진동이 그대로 LED/부저/전진차단으로 나간다).
    // 0.1m 역시 자리표시용이며, 좌표정합 오차 실측값이 나오면 그 오차보다 크게 잡아야 한다.
    double emergency_release_margin_m = 0.1;

    // ToF 근접 임계값
    // [주의] ToF 경로는 아직 3단계(SAFE/CAUTION/DANGER)로 남겨 뒀다. ToF는 사람/벽/적재물을
    //        구분하지 못해(UNCONFIRMED_PROXIMITY 참고) 단독으로 EMERGENCY까지 올리는 게
    //        타당한지 미확정이라, 이번 확장은 카메라 거리 경로에만 적용했다.
    double tof_caution_m = 1.0;
    double tof_danger_m  = 0.5;

    // IMU 급정지/충돌 판정 임계값 (g)
    // [2026-08-12] IMU 실측(시나리오 A~D, 정상 주행 15개 시행)의 최대 동적
    // 가속도(0.7082g, B_line_1m 시행2)에 안전계수 3.0을 곱한 보수적 추정치.
    // 실제 충돌 데이터는 없음(장비 손상 위험으로 재현 불가) - 정상 주행 대비
    // 3배 이상 튀면 충돌로 판정한다는 논리. 안전계수 3.0은 시행 간 진동 편차가
    // 큰 점(같은 시나리오에서 0.24~0.71g로 약 3배 차이)을 반영해 2.5에서
    // 상향 조정함. 근거: analyze_imu.py 결과(result_1a_conservative.csv)
    double impact_accel_threshold_g = 2.1246;

    // 한 프레임 처리: 카메라 입력 + 센서 입력 -> 최종 판정
    //
    // [주의] EMERGENCY 히스테리시스 때문에 이 호출은 프레임 간 상태(in_emergency_)를 남긴다.
    //        같은 엔진 인스턴스를 여러 스레드에서 동시에 호출하면 안 된다(판정 루프는 단일 스레드).
    //        const를 유지한 건 기존 호출부/테스트가 const 참조로 엔진을 쓰고 있어서다.
    JudgmentResult evaluate(const CameraInput& cam, const SensorInput& sen) const;

    // 히스테리시스 상태를 초기화한다(EMERGENCY 래치 해제).
    // 판정 대상이 바뀌거나(카메라 전환 등) 테스트에서 이전 프레임 영향을 지울 때 쓴다.
    void resetHysteresis() { in_emergency_ = false; }

    // 지금 EMERGENCY 래치가 걸려 있는지 (디버깅·테스트용)
    bool inEmergencyLatch() const { return in_emergency_; }

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

    // EMERGENCY 히스테리시스 래치. 진입은 emergency_threshold_m 이하에서,
    // 해제는 (emergency_threshold_m + emergency_release_margin_m) 초과에서만 일어난다.
    // 거리 판정 자체가 불가능한 프레임(마커 폐색/사람 미검출)에서는 해제된다 - 자세한 근거는
    // danger_judgment_engine.cpp의 evaluate() 주석 참고.
    // evaluate()가 const라 mutable로 둔다(위 evaluate() 주석의 스레드 주의사항과 한 쌍).
    mutable bool in_emergency_ = false;
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

// "미확정/미연결이면 null"로 나가야 하는 문자열 필드(camera_id, zone, terminal_id)를 직렬화한다.
// 빈 문자열("")과 값 없음을 하류에서 구분할 수 있도록 빈 값은 null로 통일한다.
std::string toJsonOrNull(const std::string& s);

// 판정 결과를 JSON 한 줄(line-delimited)로 직렬화.
// ResultPublisher::publish()가 const std::string&를 받으므로 반환 타입은 std::string.
// 외부 JSON 라이브러리 없이 표준 C++17만으로 수동 직렬화한다.
//
// 필드명·구성은 네트워크·단말 파트(Qt RiskMetadata::fromJson)와 확정된 스키마를 따른다.
// 확정된 출력 필드는 아래 7개가 전부다:
//   utc_time / camera_id / zone / terminal_id / exception_state / distance_m / risk_level
// - terminal_id는 이 판정을 받을 단말 식별자다(2026-08-06 추가). camera_id/zone과 같은 규약으로
//   빈 문자열이면 null로 나간다.
// - risk_level은 정수(0=SAFE/1=CAUTION/2=DANGER/3=EMERGENCY)다. 단말이 toInt()로 읽는
//   계약이므로 문자열로 내보내면 안 된다. enum 값이 곧 계약 값이라 EMERGENCY도 별도 처리
//   없이 3으로 나간다. 콘솔 로그용 문자열 표기는 toString(RiskLevel) 쪽에 남아 있다.
// - world 좌표·bbox는 단말에서 쓰지 않기로 협의되어 제외.
// - camera_risk / tof_risk는 서버 내부 디버깅용이므로 콘솔 로그(printResult)에만 남기고 제외.
// - 테스트 시나리오 이름표도 프로덕션 스키마에 없으므로 제외.
std::string toJson(const JudgmentResult& r);
