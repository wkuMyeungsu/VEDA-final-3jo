// mpu6050_driver.hpp
// MPU6050 (가속도계+자이로) 드라이버
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// 확실한 사실 (MPU6050 공식 레지스터 맵 기준, 데이터시트 확인됨):
//   - 기본 I2C 주소 0x68 (AD0 핀 GND) / AD0 VCC면 0x69
//   - 전원 켜지면 sleep 모드 상태라 PWR_MGMT_1(0x6B)에 0x00을 써서 깨워야 함
//   - ACCEL_XOUT_H(0x3B)부터 14바이트 연속 읽기로
//     [accel x,y,z][temp][gyro x,y,z] 전부 한 번에 가져올 수 있음 (각 2바이트, big-endian)
//   - 기본 설정(±2g, ±250dps) 기준 변환 계수: accel 16384 LSB/g, gyro 131 LSB/(deg/s)
//
// 불확실한 부분 (실측 필요, 임의로 단정하지 않음):
//   - 실제 배선의 AD0 핀 상태(주소 0x68 vs 0x69)는 배선 확정 후 i2cdetect로 확인 필요
//   - 진동/노이즈에 따른 실제 dead-reckoning 오차는 "IMU dead-reckoning 정확도" 실험으로 측정 예정

#pragma once
#include "i2c_bus.hpp"
#include <cstdint>

namespace mpu6050 {

constexpr uint8_t kDefaultAddr = 0x68;

// 레지스터 주소
constexpr uint8_t REG_PWR_MGMT_1  = 0x6B;
constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
constexpr uint8_t REG_ACCEL_CONFIG= 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H= 0x3B; // 여기서부터 14바이트 burst read
constexpr uint8_t REG_WHO_AM_I    = 0x75; // 정상이면 0x68 리턴

// 기본 설정(±2g, ±250dps) 기준 변환 계수
constexpr float kAccelScale_g_per_lsb = 1.0f / 16384.0f;   // g 단위
constexpr float kGyroScale_dps_per_lsb = 1.0f / 131.0f;    // deg/s 단위
constexpr float kGravity_ms2 = 9.80665f;

struct ImuSample {
    // 물리 단위로 변환된 값
    float accel_x_g, accel_y_g, accel_z_g;      // 단위: g
    float accel_x_ms2, accel_y_ms2, accel_z_ms2; // 단위: m/s^2 (dead-reckoning 적분용)
    float gyro_x_dps, gyro_y_dps, gyro_z_dps;    // 단위: deg/s
    bool valid = false; // false면 읽기 실패 -> 상위 로직에서 SENSOR_FAULT로 연결
};

class Mpu6050Driver {
public:
    // bus: 실제(LinuxI2CBus) 또는 가짜(MockI2CBus) 구현체를 주입받는다.
    //      -> 센서 실물이 와도 이 클래스 코드는 변경할 필요 없음
    explicit Mpu6050Driver(II2CBus& bus, uint8_t addr = kDefaultAddr)
        : bus_(bus), addr_(addr) {}

    // 입력: 없음 (센서 하드웨어 초기화)
    // 처리: PWR_MGMT_1에 0x00 써서 sleep 모드 해제
    // 출력: 성공 시 true
    bool init() {
        return bus_.writeRegister(addr_, REG_PWR_MGMT_1, 0x00);
    }

    // WHO_AM_I 레지스터로 배선/주소가 맞는지 확인 (0x68이어야 정상)
    // 입력: 없음  처리: WHO_AM_I 1바이트 읽기  출력: 레지스터 값 (읽기 실패 시 0x00)
    uint8_t whoAmI() {
        uint8_t val = 0x00;
        bus_.readRegisters(addr_, REG_WHO_AM_I, &val, 1);
        return val;
    }

    // 입력: 없음 (내부적으로 ACCEL_XOUT_H부터 14바이트 burst read)
    // 처리: raw 16bit 값 6개 파싱 -> g, m/s^2, deg/s 단위로 환산
    // 출력: ImuSample (valid=false면 버스 읽기 실패, 상위에서 SENSOR_FAULT 처리)
    ImuSample readSample() {
        ImuSample sample{};
        uint8_t raw[14] = {0};

        if (!bus_.readRegisters(addr_, REG_ACCEL_XOUT_H, raw, 14)) {
            sample.valid = false;
            return sample;
        }

        int16_t ax = combine(raw[0], raw[1]);
        int16_t ay = combine(raw[2], raw[3]);
        int16_t az = combine(raw[4], raw[5]);
        // raw[6],raw[7] = 온도, 이번 판정 로직엔 미사용
        int16_t gx = combine(raw[8], raw[9]);
        int16_t gy = combine(raw[10], raw[11]);
        int16_t gz = combine(raw[12], raw[13]);

        sample.accel_x_g = ax * kAccelScale_g_per_lsb;
        sample.accel_y_g = ay * kAccelScale_g_per_lsb;
        sample.accel_z_g = az * kAccelScale_g_per_lsb;

        sample.accel_x_ms2 = sample.accel_x_g * kGravity_ms2;
        sample.accel_y_ms2 = sample.accel_y_g * kGravity_ms2;
        sample.accel_z_ms2 = sample.accel_z_g * kGravity_ms2;

        sample.gyro_x_dps = gx * kGyroScale_dps_per_lsb;
        sample.gyro_y_dps = gy * kGyroScale_dps_per_lsb;
        sample.gyro_z_dps = gz * kGyroScale_dps_per_lsb;

        sample.valid = true;
        return sample;
    }

private:
    static int16_t combine(uint8_t high, uint8_t low) {
        return static_cast<int16_t>((high << 8) | low);
    }

    II2CBus& bus_;
    uint8_t addr_;
};

} // namespace mpu6050
