// judgment_pipeline.cpp
// 최근접 사람 선택 결과 -> 위험 판정 엔진 연결 glue - 구현
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 선언·설계 메모는 judgment_pipeline.h 참고.
// 이 파일에는 판정 로직이 없다. 값 매핑 + evaluate() 호출만 한다.
//
// 빌드 (라즈베리파이 / Linux):
//   CMake: cmake -S . -B build && cmake --build build --target judgment_pipeline
//   또는 직접: g++ -std=c++17 -c judgment_pipeline.cpp
//              (엔진 구현이 필요한 링크 단계에서 danger_judgment_engine.cpp를 함께 넣는다)

#include "logic/judgment/judgment_pipeline.h"

#include <iostream>
#include <string>

// ============================================================
// 1. IMU/ToF 스텁 리더
// ============================================================

SensorInput StubSensorReader::read() {
    // [교체 지점] 드라이버 연동 전이라 값을 실제로 읽지 않는다.
    //             MPU6050/VL53L0X 리더가 들어오면 이 클래스를 통째로 교체한다.
    //
    // 스텁 값 선택 이유: "센서 정상 + ToF 원거리"로 둬서 이 파이프라인의 카메라 경로
    // (최근접 사람 -> 거리 -> 위험도)를 센서 예외에 가려지지 않게 확인할 수 있게 한다.
    // imu_ok/tof_ok를 false로 두면 엔진이 항상 SENSOR_FAULT -> 최소 CAUTION으로 올려버려서
    // 카메라 기준 판정이 전부 CAUTION 이상으로 묻힌다.
    //
    // [주의] 이 스텁은 "센서가 정상인데 아무 위험도 없다"고 보고하는 것이므로
    //        실제 배포 전에 반드시 교체돼야 한다. 교체를 잊고 배포하면
    //        ToF 근접 경보와 충돌 감지가 조용히 죽은 상태로 동작한다.
    //        (SensorInput의 기본값과 같은 값이지만, 의도를 명시적으로 남기려고 직접 채운다.)
    //
    // 스텁이 조용히 동작하면 교체를 잊고 넘어가기 쉬우므로 첫 호출 때 한 번 경고를 남긴다.
    // 판정 루프마다 찍으면 로그가 묻히니 함수 로컬 static으로 1회만 출력한다
    // (인스턴스별이 아니라 프로세스 전체 기준 1회 — 리더를 여러 개 만들어도 한 번만 찍힌다).
    // [주의] 멀티스레드에서 read()가 동시에 불리면 C++11 이후 함수 로컬 static 초기화는
    //        스레드 안전하지만 아래 대입은 그렇지 않다. 최악의 경우 경고가 두 번 찍힐 뿐
    //        판정에는 영향이 없어서 잠금은 두지 않았다.
    static bool warned = false;
    if (!warned) {
        warned = true;
        std::cerr << "[stub] StubSensorReader 사용 중 - 실제 IMU/ToF 드라이버로 교체하기 전까지"
                     " 센서 예외(SENSOR_FAULT/EMERGENCY_IMPACT)와 ToF 근접 경보가 발동하지 않습니다\n";
    }

    SensorInput sen;
    sen.imu_ok            = true;
    sen.tof_ok            = true;
    sen.tof_distance_mm    = tof_distance_mm_;
    sen.imu_accel_g       = 0.0;   // 충돌 의심 없음
    sen.is_dead_reckoning = false; // 마커 폐색 여부는 상류(ArUco)에서 오는 값이라 스텁에서 판단하지 않음
    return sen;
}

// ============================================================
// 2. 판정 파이프라인
// ============================================================

JudgmentPipeline::JudgmentPipeline(
    int active_camera_id, const std::string& terminal_id, ISensorReader& sensors,
    const forklift::config::DangerJudgmentConfig& judgment_config,
    std::chrono::milliseconds dead_reckoning_release_grace)
    : active_camera_id_(active_camera_id), terminal_id_(terminal_id), sensors_(&sensors),
      engine_(judgment_config, dead_reckoning_release_grace) {}

void JudgmentPipeline::setActiveCameraId(int camera_id) {
    if (camera_id == active_camera_id_) return;
    active_camera_id_ = camera_id;
    engine_.resetHysteresis();
}

CameraInput JudgmentPipeline::toCameraInput(const WorldPoint& forklift,
                                            bool forklift_localized,
                                            const NearestPersonResult& nearest) const {
    CameraInput cam;

    // ── 지게차 좌표: 상류(ArUco) 값을 그대로 전달 ──────────────
    // forklift_localized=false면 이 좌표는 stale이지만, 엔진이 그 경우 거리 계산을
    // 건너뛰고 DEAD_RECKONING으로 처리하므로 여기서 별도로 지우지 않는다.
    cam.forklift_localized = forklift_localized;
    cam.forklift           = forklift;

    // ── 사람 좌표: 최근접 선택 결과 1명 ────────────────────────
    // selector가 이미 missed_frames>0인 트랙을 후보에서 뺐으므로,
    // found=false는 "이번 프레임에 유효하게 검출된 사람이 없음"과 같은 의미다.
    // -> CameraInput.person_detected에 그대로 대응된다.
    cam.person_detected = nearest.found;
    cam.person          = nearest.found ? nearest.position : WorldPoint{};

    // nearest.distance_mm은 일부러 넘기지 않는다.
    // 엔진이 forklift/person으로 같은 유클리드 거리를 다시 계산하므로(공식 동일),
    // 거리의 단일 출처를 엔진 쪽에 두고 값이 두 군데서 갈라지는 걸 막는다.

    // ── 식별 정보 ────────────────────────────────────────────
    // 이 파이프라인 인스턴스가 담당하는 카메라가 곧 판정 대상 카메라이므로
    // nearest.camera_id가 아니라 활성 카메라 id를 쓴다. 사람 미검출(found=false,
    // camera_id=-1)일 때도 "어느 카메라에서 안 보였는지"가 하류에 남는다.
    // 둘이 어긋나는 경우(핸드오버 의심)는 isCameraIdMismatch()로 따로 알린다.
    cam.camera_id = cameraIdToString(active_camera_id_);

    // [TODO] zone 매핑 미확정(김진석) — camera_id -> zone 룩업이 정해지면 여기서 채운다.
    //        확정 전까지는 빈 문자열로 둬서 하류 JSON에 null로 나가게 한다.
    cam.zone = "";

    return cam;
}

bool JudgmentPipeline::isCameraIdMismatch(const NearestPersonResult& nearest) const {
    // 사람이 없으면 비교 대상이 없다. 활성 카메라가 미확정(음수)일 때도 판단하지 않는다.
    if (!nearest.found || active_camera_id_ < 0) return false;
    return nearest.camera_id != active_camera_id_;
}

PipelineOutput JudgmentPipeline::processFrame(const WorldPoint& forklift,
                                              bool forklift_localized,
                                              const NearestPersonResult& nearest) {
    // t0_ingest: 이 프레임의 상류 결과(카메라 검출 + 지게차 좌표)가 파이프라인에 들어온 시각.
    // 아래 매핑(toCameraInput)과 센서 읽기(sensors_->read())가 여기 포함되므로, 나중에
    // 이 구간이 실제 큐잉/전처리를 갖게 되더라도 코드를 옮길 필요 없이 그대로 잡힌다.
    const auto t0 = LatencyStamps::Clock::now();

    PipelineOutput out;

    const CameraInput cam = toCameraInput(forklift, forklift_localized, nearest);
    const SensorInput sen = sensors_->read();

    // t1_judge_in: 판정 연산(engine_.evaluate) 시작 직전.
    const auto t1 = LatencyStamps::Clock::now();
    out.result              = engine_.evaluate(cam, sen);
    out.result.terminal_id  = terminal_id_;
    out.result.latency.t0_ingest   = t0;
    out.result.latency.t1_judge_in = t1;

    out.camera_id_mismatch  = isCameraIdMismatch(nearest);

    return out;
}

// ============================================================
// 3. 출력 헬퍼
// ============================================================

std::string cameraIdToString(int camera_id) {
    if (camera_id < 0) return "";   // 미확정/무효 -> 하류 JSON에서 null (toJsonOrNull 규칙)
    return std::to_string(camera_id);
}
