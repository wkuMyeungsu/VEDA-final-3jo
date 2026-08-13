// network_sensor_reader.cpp
// 선언·설계 메모는 network_sensor_reader.hpp 참고.

#include "network/network_sensor_reader.hpp"

#include <cmath>

using risk_transport::SensorUplinkReceiver;
using risk_transport::SensorUplinkSample;

NetworkSensorReader::NetworkSensorReader(SensorUplinkReceiver& receiver, std::string terminal_id,
                                         int stale_timeout_ms)
    : receiver_(&receiver), terminal_id_(std::move(terminal_id)), stale_timeout_ms_(stale_timeout_ms) {}

SensorInput NetworkSensorReader::read() {
    SensorInput sen;

    SensorUplinkSample sample;
    const bool got = receiver_->getLatest(terminal_id_, sample);
    if (!got || receiver_->isStale(terminal_id_, stale_timeout_ms_)) {
        // 한 번도 못 받았거나 마지막 수신이 오래됨 -> fail-safe로 SENSOR_FAULT 유도.
        sen.imu_ok = false;
        sen.tof_ok = false;
        return sen;
    }

    sen.imu_ok = sample.imu_ok;
    sen.tof_ok = sample.tof_ok;
    sen.tof_distance_mm = sample.tof_distance_mm;
    sen.imu_accel_g = std::sqrt(sample.imu_accel_x_g * sample.imu_accel_x_g +
                                 sample.imu_accel_y_g * sample.imu_accel_y_g +
                                 sample.imu_accel_z_g * sample.imu_accel_z_g);
    sen.is_dead_reckoning = false;   // ArUco 폐색 여부는 이 클래스의 스코프 밖 (StubSensorReader와 동일)

    return sen;
}
