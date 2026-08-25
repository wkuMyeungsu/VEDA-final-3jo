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

#include <chrono>
#include <cstdint>
#include <string>

#include "common/types.hpp"
#include "common/latency_stamps.hpp"  // LatencyStamps (서버 내부 지연 계측, JudgmentResult에 얹는다)
#include "config_loader/safety_server_config.hpp"

// ============================================================
// 1. 데이터 구조 정의
// ============================================================

// 바닥 평면 위 실제 좌표(mm 단위).
// main.cpp에서 ArUco 마커 중심과 사람 발 위치를 같은 호모그래피로 변환한 값이 들어온다.
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
    bool       forklift_localized = false; // 지게차 world 좌표를 이번 프레임에 얻었는지 (false=마커 폐색)
    bool       person_detected = false;    // 이번 프레임에 사람이 검출됐는지 (false=화면에 없음/미검출)
    // world 좌표는 하류로 내보내지 않고(팀 협의로 JSON에서 제외됨) 내부 거리 계산에만 쓴다.
    WorldPoint forklift;                   // 지게차 world 좌표 (forklift_localized=false면 stale/무의미)
    WorldPoint person;                     // 사람 world 좌표 (person_detected=false면 stale/무의미)

    // camera_id는 camera_list.json의 물리 CCTV 식별자를 상류에서 그대로 전달한다.
    // 숫자 채널을 camera_id 문자열로 변환하거나 임의의 카메라를 생성하지 않는다.
    std::string camera_id;

    // [TODO] zone 매핑 미확정(김진석) — 값 확정 전까지 항상 null
    std::string zone;
};

// IMU/ToF 센서 기반 입력. 현재 운영 경로는 단말이 MQTT로 올린 최신 센서값을 사용하며,
// 직접 연결 드라이버나 테스트 스텁도 같은 구조를 채우도록 인터페이스를 통일했다.
struct SensorInput {
    // 센서 값을 안전한 거리로 기본 설정하지 않는다. 실제 리더가 채우지 않은 값은
    // imu_ok/tof_ok=false로 남아 SENSOR_FAULT fail-safe 경로를 타야 한다.
    bool   imu_ok = false;
    bool   tof_ok = false;
    double tof_distance_mm{};
    double imu_accel_g{};
    bool   is_dead_reckoning = false;

    // 센서 업링크 관측 context. 판정에는 영향을 주지 않고, 어떤 입력 샘플이 이
    // 결과의 근거였는지 사후 조인하는 데만 사용한다. 로컬/스텁 리더는 빈 값으로 둔다.
    std::string sensor_message_id;
    std::string sensor_producer_run_id;
    std::int64_t sensor_sequence = -1;
    std::int64_t sensor_ts_ms = 0;
    std::int64_t sensor_age_ms = -1;
};

// 한 프레임의 판정 결과
struct JudgmentResult {
    RiskLevel      camera_risk;
    RiskLevel      tof_risk;
    RiskLevel      final_risk;
    ExceptionState exception;
    double         distance_mm;      // 참고용 (카메라 기준 유클리드 거리, 폐색/미검출 시 -1)

    // world 좌표/bbox는 단말로 내보내지 않기로 팀 협의됨 -> 결과 구조체에서도 제거.
    // (거리 계산에 필요한 원본 좌표는 CameraInput에만 남아 있다.)

    std::string    camera_id;       // cam.camera_id 그대로 전달 (현재는 상류 미연결 -> 빈 문자열)
    std::string    zone;            // cam.zone 그대로 전달 (매핑 미확정 -> 빈 문자열 -> JSON null)

    // 이 판정을 보낼 단말(운전자석) 식별자. CameraInput을 거치지 않고
    // JudgmentPipeline이 evaluate() 호출 후 직접 채운다(생성자 인자로 받은 값을 그대로 대입) -
    // 단말이 여러 대일 때 "이 메시지가 어느 단말 것인지" 구분하기 위해 도입됨 (2026-08-06 팀 합의).
    // 빈 문자열 -> JSON null (camera_id/zone과 동일 규약).
    std::string    terminal_id;
    std::string    stream_id;
    std::string    source_camera_id;
    int            channel = -1;

    // 서버 내부 지연 계측 스탬프 (t0_ingest/t1_judge_in은 호출부가 채우고, t2_send는
    // ResultDispatcher::submit()이 찍는다). evaluate()는 이 필드를 건드리지 않는다 -
    // 기본값(에폭 0시)인 채로 반환되며, JudgmentPipeline::processFrame()이 반환 후에 채운다.
    // 맨 끝에 추가한 이유: 기존 aggregate 초기화(JudgmentResult r{};)와 필드 순서를
    // 건드리지 않기 위해서다. JSON 직렬화(toJson) 스키마에는 포함하지 않는다(하류 계약 밖).
    LatencyStamps  latency;

    // 분산 관측 context. 기존 위험 필드와 독립적인 optional 확장이다. 비어 있으면
    // 구형 입력/직접 호출이며, 채워진 경우 SQLite·MQTT risk payload에서 조인 키로 쓴다.
    std::string decision_id;
    std::string server_run_id;
    std::uint64_t publish_seq = 0;
    std::string send_reason;
    std::string sensor_message_id;
    std::string sensor_producer_run_id;
    std::int64_t sensor_sequence = -1;
    std::int64_t sensor_ts_ms = 0;
    std::int64_t sensor_age_ms = -1;
};

// ============================================================
// 2. 위험 판정 엔진
// ============================================================

class DangerJudgmentEngine {
public:
    explicit DangerJudgmentEngine(
        const forklift::config::DangerJudgmentConfig& config,
        double forklift_collision_radius_mm,
        std::chrono::milliseconds dead_reckoning_release_grace,
        bool ignore_sensor_input = false);

    // 한 프레임 처리: 카메라 입력 + 센서 입력 -> 최종 판정
    //
    // [주의] EMERGENCY 히스테리시스 때문에 이 호출은 프레임 간 상태(in_emergency_)를 남긴다.
    //        같은 엔진 인스턴스를 여러 스레드에서 동시에 호출하면 안 된다(판정 루프는 단일 스레드).
    //        const를 유지한 건 기존 호출부/테스트가 const 참조로 엔진을 쓰고 있어서다.
    JudgmentResult evaluate(const CameraInput& cam, const SensorInput& sen) const;

    // 히스테리시스 상태를 초기화한다(EMERGENCY 래치 + DEAD_RECKONING 해제 유예 상태 해제).
    // 판정 대상이 바뀌거나(카메라 전환 등) 테스트에서 이전 프레임 영향을 지울 때 쓴다.
    // dead_reckoning_active_도 같이 지우는 이유는 in_emergency_와 같다 - 직전 카메라 기준으로
    // 걸린 상태를 시야도 좌표계도 다른 새 카메라의 첫 프레임에 그대로 물려주면 안 된다.
    void resetHysteresis() {
        in_emergency_ = false;
        dead_reckoning_active_ = false;
        last_camera_level_ = RiskLevel::SAFE;
    }

    // 지금 EMERGENCY 래치가 걸려 있는지 (디버깅·테스트용)
    bool inEmergencyLatch() const { return in_emergency_; }

private:
    // 아래 판정값은 생성할 때 공통 JSON 설정에서 한 번만 주입받는다.
    // 코드나 호출부에서 운영값을 다시 덮어쓸 수 없도록 상수 멤버로 보관한다.
    const double caution_threshold_mm;
    const double danger_threshold_mm;
    const double emergency_threshold_mm;
    const double emergency_release_margin_mm;
    const double tof_caution_mm;
    const double tof_danger_mm;
    const double forklift_collision_radius_mm;
    const double impact_accel_threshold_g;
    const std::chrono::milliseconds dead_reckoning_release_grace_ms;
    const bool ignore_sensor_input_;

    static double euclideanDistance(const WorldPoint& a, const WorldPoint& b);

    RiskLevel classifyByDistance(double dist_mm) const;
    RiskLevel classifyByTof(double dist_mm) const;

    ExceptionState detectException(const CameraInput& cam, const SensorInput& sen,
                                   RiskLevel tof_risk) const;

    // 둘 중 더 위험한 단계 반환 (worst-case 우선순위 원칙)
    static RiskLevel worstOf(RiskLevel a, RiskLevel b);

    // r이 minimum보다 낮으면 minimum까지 끌어올림
    static RiskLevel atLeast(RiskLevel r, RiskLevel minimum);

    // EMERGENCY 히스테리시스 래치. 진입은 emergency_threshold_mm 이하에서,
    // 해제는 (emergency_threshold_mm + emergency_release_margin_mm) 초과에서만 일어난다.
    // 거리 판정 자체가 불가능한 프레임(마커 폐색/사람 미검출)에서는 해제된다 - 자세한 근거는
    // danger_judgment_engine.cpp의 evaluate() 주석 참고.
    // evaluate()가 const라 mutable로 둔다(위 evaluate() 주석의 스레드 주의사항과 한 쌍).
    mutable bool in_emergency_ = false;

    // SAFE/CAUTION/DANGER 경계 히스테리시스용 직전 카메라 판정 단계. 하락(안전 방향)
    // 은 release margin을 넘겼을 때만 한 단계 내려간다. in_emergency_와 같은 이유로 mutable.
    mutable RiskLevel last_camera_level_ = RiskLevel::SAFE;

    // DEAD_RECKONING 해제 히스테리시스 래치. 진입(raw 조건 참)은 즉시 걸리고,
    // 해제는 raw 조건이 거짓으로 돌아온 뒤에도 dead_reckoning_release_grace_ms만큼
    // last_dead_reckoning_time_로부터 시간이 지나야 풀린다. in_emergency_와 같은 이유로
    // mutable(단일 스레드 전제, evaluate()가 const).
    mutable bool dead_reckoning_active_ = false;

    // 가장 최근에 raw 조건(!forklift_localized || is_dead_reckoning)이 참이었던(위치 미확보)
    // 시각. dead_reckoning_active_ 해제 여부를 이 시각 기준으로 판단한다 - 위치를 다시
    // 놓치면 이 값이 매 프레임 갱신되어 유예 타이머가 계속 리셋된다.
    // steady_clock 사용 이유는 LatencyStamps와 동일(서버 프로세스 내부 상대 구간 계측 -
    // latency_stamps.hpp 주석 참고).
    mutable std::chrono::steady_clock::time_point last_dead_reckoning_time_{};
};

// ============================================================
// 3. 출력 / 직렬화 헬퍼
// ============================================================

std::string toString(RiskLevel r);
std::string toString(ExceptionState e);


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
//   utc_time / camera_id / zone / terminal_id / exception_state / distance_mm / risk_level
// - terminal_id는 이 판정을 받을 단말 식별자다(2026-08-06 추가). camera_id/zone과 같은 규약으로
//   빈 문자열이면 null로 나간다.
// - risk_level은 정수(0=SAFE/1=CAUTION/2=DANGER/3=EMERGENCY)다. 단말이 toInt()로 읽는
//   계약이므로 문자열로 내보내면 안 된다. enum 값이 곧 계약 값이라 EMERGENCY도 별도 처리
//   없이 3으로 나간다. 콘솔 로그용 문자열 표기는 toString(RiskLevel) 쪽에 남아 있다.
// - world 좌표·bbox는 단말에서 쓰지 않기로 협의되어 제외.
// - camera_risk / tof_risk는 서버 내부 디버깅용이므로 콘솔 로그(printResult)에만 남기고 제외.
// - 테스트 시나리오 이름표도 프로덕션 스키마에 없으므로 제외.
std::string toJson(const JudgmentResult& r);
