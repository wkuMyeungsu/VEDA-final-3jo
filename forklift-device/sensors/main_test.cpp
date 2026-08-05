// main_test.cpp
// 센서 실물 없이 드라이버 로직을 검증하는 테스트 프로그램
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// 빌드 (Windows/Linux 공통, 순수 표준 C++17 + Mock만 사용):
//   g++ -std=c++17 main_test.cpp -o driver_test
//
// 실제 라즈베리파이에서 진짜 센서로 테스트하려면 i2c_bus_linux.hpp의
// LinuxI2CBus를 대신 생성해서 Mpu6050Driver에 넘기면 됨 (드라이버 코드 변경 없음).

#include <iostream>
#include <cassert>
#include <cmath>
#include "i2c_bus_mock.hpp"
#include "mpu6050_driver.hpp"
#include "vl53l1x_interface.hpp"

namespace {
bool nearlyEqual(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) < eps;
}
}

// 시나리오 1: 정상 상황 - 알려진 raw 값을 넣고 물리값 변환이 맞는지 검증
void test_normal_read() {
    std::cout << "[시나리오 1] 정상 IMU 읽기 + 물리값 변환 검증\n";

    MockI2CBus bus;
    // WHO_AM_I 정상값 세팅 (실제 MPU6050이면 0x68 리턴)
    bus.setRegister(mpu6050::kDefaultAddr, mpu6050::REG_WHO_AM_I, 0x68);

    // ACCEL_XOUT_H(0x3B)부터 14바이트: accel x,y,z / temp / gyro x,y,z
    // accel_x raw = 16384 (0x4000) -> 1.0g
    // gyro_x  raw = 131   (0x0083) -> 1.0 deg/s
    std::vector<uint8_t> raw14 = {
        0x40, 0x00,   // ACCEL_XOUT (16384 -> 1.0g)
        0x00, 0x00,   // ACCEL_YOUT (0)
        0x00, 0x00,   // ACCEL_ZOUT (0)
        0x00, 0x00,   // TEMP (미사용)
        0x00, 0x83,   // GYRO_XOUT (131 -> 1.0 dps)
        0x00, 0x00,   // GYRO_YOUT (0)
        0x00, 0x00,   // GYRO_ZOUT (0)
    };
    bus.setRegisters(mpu6050::kDefaultAddr, mpu6050::REG_ACCEL_XOUT_H, raw14);

    mpu6050::Mpu6050Driver imu(bus);
    bool init_ok = imu.init();
    uint8_t who = imu.whoAmI();
    auto sample = imu.readSample();

    std::cout << "  init()      : " << (init_ok ? "OK" : "FAIL") << "\n";
    std::cout << "  WHO_AM_I    : 0x" << std::hex << int(who) << std::dec
               << (who == 0x68 ? "  (정상)" : "  (예상과 다름)") << "\n";
    std::cout << "  accel_x_g   : " << sample.accel_x_g << " g (기대값 1.0)\n";
    std::cout << "  accel_x_ms2 : " << sample.accel_x_ms2 << " m/s^2 (기대값 ~9.807)\n";
    std::cout << "  gyro_x_dps  : " << sample.gyro_x_dps << " deg/s (기대값 1.0)\n";

    assert(init_ok);
    assert(who == 0x68);
    assert(sample.valid);
    assert(nearlyEqual(sample.accel_x_g, 1.0f));
    assert(nearlyEqual(sample.accel_x_ms2, 9.80665f, 0.01f));
    assert(nearlyEqual(sample.gyro_x_dps, 1.0f));
    std::cout << "  -> PASS\n\n";
}

// 시나리오 2: SENSOR_FAULT 상황 - 버스 응답 없음 -> valid=false로 상위에 전달되는지 확인
void test_sensor_fault() {
    std::cout << "[시나리오 2] SENSOR_FAULT (버스 응답 없음) 처리 검증\n";

    MockI2CBus bus;
    bus.simulateFault(true); // 센서 고장/미연결 상황 재현

    mpu6050::Mpu6050Driver imu(bus);
    auto sample = imu.readSample();

    std::cout << "  sample.valid: " << (sample.valid ? "true" : "false")
               << " (기대값 false)\n";
    assert(!sample.valid);
    std::cout << "  -> PASS (상위 danger_judgment_engine의 SENSOR_FAULT 분기로 연결 가능)\n\n";
}

// 시나리오 3: ToF Mock으로 센서 퓨전 임계값 로직 흉내 (실제 판정 엔진과 동일한 조건식 사용 예시)
void test_tof_mock_fusion_example() {
    std::cout << "[시나리오 3] ToF Mock + 센서 퓨전 임계값 예시\n";

    MockTofSensor tof;
    tof.init();
    tof.setNextDistance(400, true); // 400mm = 0.4m 근접 상황 재현

    auto tof_sample = tof.readDistance();
    bool danger_by_tof = tof_sample.valid && tof_sample.distance_mm < 500; // 예: 0.5m 미만 DANGER 가정

    std::cout << "  distance_mm : " << tof_sample.distance_mm << "\n";
    std::cout << "  danger_by_tof: " << (danger_by_tof ? "true" : "false") << "\n";
    assert(danger_by_tof);
    std::cout << "  -> PASS (실제 임계값은 danger_judgment_engine.cpp 기준과 맞춰야 함, 여기선 예시값)\n\n";
}

int main() {
    test_normal_read();
    test_sensor_fault();
    test_tof_mock_fusion_example();
    std::cout << "=== 전체 테스트 통과 (센서 실물 없이 Mock으로 검증 완료) ===\n";
    return 0;
}
