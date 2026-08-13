// sensor_collector_reader.cpp
// 선언·설계 메모는 sensor_collector_reader.h 참고.
//
// [주의] 이 어댑터는 로컬 개발/테스트 전용 결선이다. 실제 배포에서는 센서(forklift-device)와
//        판정 엔진(server)이 물리적으로 분리된 별도 RPi4(veda3)에서 돌기 때문에, 이 클래스처럼
//        SensorCollector를 프로세스 내에서 직접 물릴 수 없다. 실제 배포 시에는 단말→서버
//        네트워크 업링크로 센서값을 JSON 등으로 전송하고, 서버 측에서 그 값을 받아
//        ISensorReader 구현체(예: NetworkSensorReader)로 감싸야 한다.
//        (2026-08-05 협의 필요 사항, 아직 미정)

#include "input/sensor_collector_reader.h"

#include <cmath>

SensorInput SensorCollectorReader::read() {
    const CombinedSensorReading reading = collector_->pollOnce();

    SensorInput sen;
    sen.imu_ok = reading.imu.valid;
    sen.tof_ok = reading.tof.valid;

    sen.tof_distance_mm = reading.tof.distance_mm;

    sen.imu_accel_g = std::sqrt(reading.imu.accel_x_g * reading.imu.accel_x_g +
                                 reading.imu.accel_y_g * reading.imu.accel_y_g +
                                 reading.imu.accel_z_g * reading.imu.accel_z_g);

    sen.is_dead_reckoning = false;  // ArUco 폐색 여부는 이 클래스의 스코프 밖 (StubSensorReader와 동일)

    return sen;
}
