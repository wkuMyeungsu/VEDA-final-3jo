// i2c_bus_mock.hpp
// 가짜 I2C 버스 구현 - 센서 실물 도착 전까지 드라이버/판정 로직 검증용
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// 특징:
//   - Windows/Linux 어디서든 컴파일·실행 가능 (하드웨어 의존성 없음)
//   - setRegisters()로 원하는 시나리오 값을 미리 채워두고 드라이버가
//     그 값을 정상적으로 파싱하는지 검증
//   - simulateFault(true)로 "센서 응답 없음" 상황을 재현
//     -> SENSOR_FAULT 예외처리 로직과 바로 연결해서 테스트 가능

#pragma once
#include "i2c_bus.hpp"
#include <map>
#include <cstring>

class MockI2CBus : public II2CBus {
public:
    // slave_addr 장치의 reg 위치부터 values를 순서대로 채워둔다.
    // 예: MPU6050 ACCEL_XOUT_H(0x3B)부터 14바이트 burst read를 흉내낼 때 사용
    void setRegisters(uint8_t slave_addr, uint8_t reg_start,
                       const std::vector<uint8_t>& values) {
        for (size_t i = 0; i < values.size(); ++i) {
            registers_[slave_addr][static_cast<uint8_t>(reg_start + i)] = values[i];
        }
    }

    void setRegister(uint8_t slave_addr, uint8_t reg, uint8_t value) {
        registers_[slave_addr][reg] = value;
    }

    // true로 설정하면 이후 모든 읽기/쓰기가 실패 -> SENSOR_FAULT 시나리오 재현
    void simulateFault(bool fault) { fault_ = fault; }

    bool readRegisters(uint8_t slave_addr, uint8_t reg,
                        uint8_t* out, size_t length) override {
        if (fault_) return false;

        auto slave_it = registers_.find(slave_addr);
        for (size_t i = 0; i < length; ++i) {
            uint8_t r = static_cast<uint8_t>(reg + i);
            if (slave_it != registers_.end() && slave_it->second.count(r)) {
                out[i] = slave_it->second.at(r);
            } else {
                out[i] = 0x00; // 설정 안 된 레지스터는 0으로 채움 (센서 미연결과 유사)
            }
        }
        return true;
    }

    bool writeRegister(uint8_t slave_addr, uint8_t reg, uint8_t value) override {
        if (fault_) return false;
        registers_[slave_addr][reg] = value;
        return true;
    }

private:
    std::map<uint8_t, std::map<uint8_t, uint8_t>> registers_;
    bool fault_ = false;
};
