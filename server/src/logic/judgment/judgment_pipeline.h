// judgment_pipeline.h
// 최근접 사람 선택 결과 -> 위험 판정 엔진 연결 glue - 공개 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 목적: README의 "camera_id 통합 지점 부재" 항목을 메운다.
//       nearest_person_selector.cpp가 고른 최근접 사람 1명(NearestPersonResult)을
//       danger_judgment_engine.h의 CameraInput으로 매핑하고, SensorInput을 붙여
//       DangerJudgmentEngine::evaluate()까지 호출하는 배선만 담당한다.
//
// 이 파일은 판정 로직을 갖지 않는다. 임계값 판단·예외 상태 결정·worst-case 퓨전은
// 전부 DangerJudgmentEngine 쪽에 있고, 선택 로직은 nearest_person_selector 쪽에 있다.
// 여기서는 값을 옮기고 호출만 한다(두 컴포넌트의 로직은 건드리지 않았다).
//
// 이번 범위:
//   - 활성 카메라 1대(단일 camera_id) 기준 처리만
//   - SensorInput(IMU/ToF)은 드라이버 연동 전이라 스텁 리더로 대체, 인터페이스만 확정
// 범위 밖:
//   - 핸드오버 구간(여러 camera_id 동시 중첩) 처리 -> 아래 JudgmentPipeline의 [TODO] 참고
//
// 구현은 judgment_pipeline.cpp에 있다. 이 헤더는 표준 헤더 + 두 컴포넌트 헤더만 쓰므로
// POSIX 의존성이 없다(gmtime_r/소켓 의존성은 엔진 .cpp와 ResultPublisher 쪽에만 있다).

#pragma once

#include <string>

#include "logic/judgment/danger_judgment_engine.h"
#include "logic/tracking/nearest_person_selector.h"  // NearestPersonResult (상류 결과 타입)

// 상류 결과 타입(NearestPersonResult)은 예전엔 이 헤더가 필드 구성만 베껴 미러링했지만,
// nearest_person_selector가 헤더/구현/main으로 분리된 뒤로는 그 헤더를 직접 include한다.
// -> 이제 미러 정의를 손으로 동기화할 필요가 없고, 테스트도 진짜 구현체와 링크된다.
// (WorldPoint는 nearest_person_selector.h가 danger_judgment_engine.h 쪽 정의를
//  재사용하므로 두 헤더를 같이 include해도 중복 정의가 생기지 않는다.)

// ============================================================
// 1. IMU/ToF 리더 인터페이스 (드라이버 연동 전)
// ============================================================

// SensorInput 한 프레임 분을 공급하는 쪽의 인터페이스.
// 지금은 스텁만 있지만, 실제 드라이버가 들어올 때 이 인터페이스만 구현하면
// JudgmentPipeline은 손대지 않아도 된다.
class ISensorReader {
public:
    virtual ~ISensorReader() = default;

    // 이번 프레임의 IMU/ToF 값. 읽기 실패는 예외를 던지지 말고
    // SensorInput.imu_ok / tof_ok = false로 표현한다
    // (엔진이 그걸 SENSOR_FAULT로 처리해 최소 CAUTION을 유지하는 구조라서).
    virtual SensorInput read() = 0;
};

// 드라이버 연동 전 스텁.
// [교체 지점] MPU6050(I2C 0x68) / VL53L0X(I2C 0x29) 드라이버 리더로 교체.
//             교체 대상은 이 클래스뿐이고 ISensorReader 시그니처는 그대로 쓴다.
class StubSensorReader : public ISensorReader {
public:
    SensorInput read() override;
};

// ============================================================
// 2. 판정 파이프라인
// ============================================================

// 한 프레임 처리 결과.
// JudgmentResult 외에 "이번 범위에서 처리하지 않기로 한 상황"을 호출부가 로깅할 수 있도록
// 플래그를 함께 돌려준다.
struct PipelineOutput {
    JudgmentResult result;

    // 최근접 사람이 활성 카메라가 아닌 다른 camera_id에서 왔음 -> 핸드오버 구간 의심.
    // 이번 범위에서는 판정을 바꾸지 않고 이 플래그로만 표시한다(아래 [TODO] 참고).
    bool camera_id_mismatch = false;
};

// 활성 카메라 1대 기준으로 상류 결과를 엔진 입력으로 바꿔 평가하는 파이프라인.
//
// [TODO] 핸드오버 구간(여러 camera_id가 같은 지게차/사람을 동시에 보는 구간) 처리는
//        이번 범위에서 제외했다. 계획된 방식:
//          1) 겹치는 camera_id별로 CameraInput을 각각 만들어 evaluate()를 따로 돌린다
//          2) 그 결과들 중 worst-case(더 위험한 쪽)를 최종 판정으로 채택한다
//             (엔진 내부의 worstOf()와 같은 원칙을 파이프라인 레벨에 한 번 더 적용)
//          3) camera_id/zone은 채택된 쪽 값을 그대로 내보낸다
//        지금은 활성 카메라 1대만 다루므로, 다른 camera_id가 섞여 들어오면
//        PipelineOutput.camera_id_mismatch로만 표시하고 판정은 평소대로 진행한다.
//        (사람을 버리는 쪽이 안전 측면에서 더 위험하므로 무시/드랍하지 않는다.)
class JudgmentPipeline {
public:
    // active_camera_id: 이 파이프라인이 담당하는 활성 카메라.
    //                   음수면 "미확정"으로 보고 하류 JSON에 camera_id=null로 나간다.
    // sensors: 호출부가 소유한다(파이프라인보다 오래 살아야 함).
    JudgmentPipeline(int active_camera_id, ISensorReader& sensors);

    // 한 프레임 처리: 상류 입력 -> CameraInput 매핑 -> SensorInput 읽기 -> evaluate().
    //
    // forklift            : 지게차 world 좌표 (박명수 파트 ArUco 결과)
    // forklift_localized  : 이번 프레임에 위 좌표를 실제로 얻었는지 (false=마커 폐색)
    // nearest             : nearest_person_selector가 고른 최근접 사람 1명
    PipelineOutput processFrame(const WorldPoint& forklift,
                                bool forklift_localized,
                                const NearestPersonResult& nearest);

    // 매핑만 수행 (엔진 호출·센서 읽기 없음). 매핑 규칙 단독 검증용으로 분리했다.
    CameraInput toCameraInput(const WorldPoint& forklift,
                              bool forklift_localized,
                              const NearestPersonResult& nearest) const;

    // 상류 결과가 활성 카메라에서 온 것인지 여부 (found=false면 판단 대상 아님 -> false).
    bool isCameraIdMismatch(const NearestPersonResult& nearest) const;

    int activeCameraId() const { return active_camera_id_; }

    // 임계값 보정(확정된 실험 4종 결과 반영)을 호출부에서 할 수 있도록 엔진을 노출한다.
    DangerJudgmentEngine&       engine()       { return engine_; }
    const DangerJudgmentEngine& engine() const { return engine_; }

private:
    int                  active_camera_id_;
    ISensorReader*       sensors_;   // non-owning
    DangerJudgmentEngine engine_;
};

// ============================================================
// 3. 출력 헬퍼
// ============================================================

// camera_id 표기 방식: danger_judgment_engine.h의 CameraInput 주석에 적힌 결정사항대로
// int를 std::to_string()으로 단순 변환한다. 음수(미확정)는 빈 문자열 -> 하류 JSON에서 null.
// 라벨링(cam_01 등)으로 바꿀 거면 이 함수 하나만 고치면 된다.
std::string cameraIdToString(int camera_id);
