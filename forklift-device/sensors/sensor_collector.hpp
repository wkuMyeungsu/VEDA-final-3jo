// sensor_collector.hpp
// IMU + ToF 센서를 하나로 묶어서 폴링하는 얇은 수집기.
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// IImuSensor&, ITofSensor&를 주입받기 때문에 Mock/실제 구현체(Mpu6050Driver,
// VL53L1XKernelDriver) 어느 쪽을 넘겨도 그대로 동작한다.
//
// 범위 밖: CombinedSensorReading을 판정 엔진(server 트랙)으로 보내는 부분은
// 아직 만들지 않음 -> 별도 인터페이스 연결 작업에서 다룬다.

#pragma once
#include "imu_sensor.hpp"
#include "vl53l1x_interface.hpp"

struct CombinedSensorReading {
    ImuSample imu;
    TofSample tof;
};

class SensorCollector {
public:
    SensorCollector(IImuSensor& imu, ITofSensor& tof)
        : imu_(imu), tof_(tof) {}

    CombinedSensorReading pollOnce() {
        return CombinedSensorReading{imu_.readSample(), tof_.readDistance()};
    }

private:
    IImuSensor& imu_;
    ITofSensor& tof_;
};
