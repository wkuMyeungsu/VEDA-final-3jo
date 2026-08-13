// sensor_collector_reader.h
// SensorCollector(forklift-device 트랙, 실제 IMU/ToF 드라이버) -> ISensorReader 어댑터
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// [주의] 이 어댑터는 로컬 개발/테스트 전용 결선이다. 실제 배포에서는 센서(forklift-device)와
//        판정 엔진(server)이 물리적으로 분리된 별도 RPi4(veda3)에서 돌기 때문에, 이 클래스처럼
//        SensorCollector를 프로세스 내에서 직접 물릴 수 없다. 실제 배포 시에는 단말→서버
//        네트워크 업링크로 센서값을 JSON 등으로 전송하고, 서버 측에서 그 값을 받아
//        ISensorReader 구현체(예: NetworkSensorReader)로 감싸야 한다.
//        (2026-08-05 협의 필요 사항, 아직 미정)
//
// sensor_collector.hpp의 "범위 밖" 메모("CombinedSensorReading을 판정 엔진(server 트랙)으로
// 보내는 부분은 아직 만들지 않음 -> 별도 인터페이스 연결 작업에서 다룬다")에서 말한
// 그 연결 작업이 이 파일이다. StubSensorReader의 [교체 지점] 메모대로, 이 클래스는
// ISensorReader 시그니처만 구현하고 JudgmentPipeline 쪽은 전혀 건드리지 않는다.
//
// 값 변환 (SensorCollector -> SensorInput):
//   - TofSample.distance_mm(정수, mm) -> tof_distance_mm(double, mm): 변환 없음
//   - ImuSample.accel_x/y/z_g(3축)   -> imu_accel_g(스칼라 "크기"): sqrt(x^2+y^2+z^2)
//     (danger_judgment_engine.h 주석: "IMU 가속도 크기 (g 단위, 급정지/충돌 감지용)".
//      정지 상태에서도 중력으로 약 1.0g가 나오므로, 임계값은 그 점을 감안해 엔진 쪽에서 잡는다.)
//   - ImuSample.valid / TofSample.valid -> imu_ok / tof_ok: 이름만 다르고 의미는 동일
//   - is_dead_reckoning: 이 센서 스코프 밖(ArUco 폐색 여부, 상류 판단) -> StubSensorReader와
//     동일하게 여기서는 항상 false로 둔다. 실제 값은 JudgmentPipeline 호출부가
//     CameraInput.forklift_localized 쪽에서 이미 다룬다.
//
// 소유권: SensorCollector& 는 이 클래스가 소유하지 않는다(non-owning). 호출부가
// SensorCollector(및 그 안에 주입되는 IImuSensor/ITofSensor 구현체)보다 오래 살아있어야
// 한다 - JudgmentPipeline이 ISensorReader&를 받는 방식과 동일한 관례.

#pragma once

#include "logic/judgment/judgment_pipeline.h"   // ISensorReader, SensorInput
#include "sensor_collector.hpp"  // SensorCollector, CombinedSensorReading (forklift-device/sensors)

class SensorCollectorReader : public ISensorReader {
public:
    explicit SensorCollectorReader(SensorCollector& collector) : collector_(&collector) {}

    SensorInput read() override;

private:
    SensorCollector* collector_;  // non-owning
};
