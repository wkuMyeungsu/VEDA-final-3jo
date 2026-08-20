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
// 카메라·스트림·단말 식별자는 모두 문자열/목록 기반으로 전달한다. 특정 카메라
// 번호를 생성자에 박아 두지 않으며, 담당 스트림은 ArUco 핸드오버 결과로 갱신한다.
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
    // Stub 거리도 공통 JSON에서 주입받아야 하므로 기본 생성을 허용하지 않는다.
    explicit StubSensorReader(double tof_distance_mm) : tof_distance_mm_(tof_distance_mm) {}
    SensorInput read() override;
private:
    double tof_distance_mm_;
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

// 상류 결과를 엔진 입력으로 바꿔 평가하는 단말별 파이프라인.
class JudgmentPipeline {
public:
    // terminal_id: 이 파이프라인이 결과를 보낼 단말 식별자. processFrame()이 evaluate() 호출
    //              후 결과에 그대로 채워 넣는다(값 검증 없이 통과). 빈 문자열이면 하류 JSON에
    //              terminal_id=null로 나간다(camera_id와 동일 규약).
    // sensors: 호출부가 소유한다(파이프라인보다 오래 살아야 함).
    JudgmentPipeline(const std::string& terminal_id, ISensorReader& sensors,
                     const forklift::config::DangerJudgmentConfig& judgment_config,
                     double collision_radius_mm,
                     std::chrono::milliseconds dead_reckoning_release_grace);

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

    const std::string& activeStreamId() const { return active_stream_id_; }
    const std::string& activeCameraId() const { return active_camera_id_; }
    int activeChannel() const { return active_channel_; }
    const std::string& terminalId() const { return terminal_id_; }

    // 담당 스트림을 바꾼다(핸드오버). stream_id가 바뀌면 camera_id와 channel도
    // 함께 갱신하고, 새 시야의 첫 판정에 이전 시야의 히스테리시스를 물리지 않는다.
    //
    // 카메라가 바뀌면 엔진의 EMERGENCY 히스테리시스 래치도 같이 푼다. 직전 카메라
    // 기준 거리로 걸린 래치를, 시야도 좌표계도 다른 새 카메라의 첫 프레임에 그대로
    // 물려주면 근거 없는 EMERGENCY가 유지된다(danger_judgment_engine.h의
    // resetHysteresis() 주석이 말하는 "판정 대상이 바뀌는 경우"가 바로 이것이다).
    // 값이 실제로 달라질 때만 리셋하므로 같은 값으로 여러 번 불러도 안전하다.
    //
    // [주의] 이 함수와 processFrame()은 같은 스레드에서 불러야 한다. 엔진 히스테리시스가
    //        원래부터 단일 스레드 전제라(엔진 헤더 evaluate() 주석) 여기에도 락을 두지
    //        않았다. main.cpp에서는 둘 다 appsink 콜백 스레드에서만 호출된다.
    void setActiveStream(const std::string& stream_id, const std::string& camera_id, int channel);

    // 테스트와 진단에서 현재 래치 상태를 읽을 수 있게 엔진의 읽기 전용 참조만 제공한다.
    // 판정 임계값은 공통 JSON에서 생성 시 주입되며 호출부에서 변경할 수 없다.
    const DangerJudgmentEngine& engine() const { return engine_; }

private:
    std::string          active_stream_id_;
    std::string          active_camera_id_;
    int                  active_channel_ = -1;
    std::string          terminal_id_;
    ISensorReader*       sensors_;   // non-owning
    DangerJudgmentEngine engine_;
};
