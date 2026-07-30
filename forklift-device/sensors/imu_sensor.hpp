// imu_sensor.hpp
// IMU (가속도계+자이로) 센서 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// ImuSample은 원래 mpu6050_driver.hpp 안 mpu6050 네임스페이스에 있었으나,
// MPU6050 고유 데이터가 아니라 일반 IMU 측정값이라 여기 인터페이스 쪽으로 옮김.
//
// vl53l1x_interface.hpp(ITofSensor)와 같은 패턴: 상위(센서 퓨전) 로직이
// 이 인터페이스에만 의존하도록 해두면, 구현체(Mpu6050Driver 등)를 갈아끼워도
// 상위 코드는 안 바뀐다.

#pragma once
#include <cstdint>

struct ImuSample {
    // 물리 단위로 변환된 값
    float accel_x_g, accel_y_g, accel_z_g;      // 단위: g
    float accel_x_ms2, accel_y_ms2, accel_z_ms2; // 단위: m/s^2 (dead-reckoning 적분용)
    float gyro_x_dps, gyro_y_dps, gyro_z_dps;    // 단위: deg/s
    bool valid = false; // false면 읽기 실패 -> 상위 로직에서 SENSOR_FAULT로 연결
};

class IImuSensor {
public:
    virtual ~IImuSensor() = default;
    virtual bool init() = 0;
    virtual ImuSample readSample() = 0;
};
