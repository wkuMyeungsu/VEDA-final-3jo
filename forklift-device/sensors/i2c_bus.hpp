// i2c_bus.hpp
// I2C 버스 접근 추상 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// 목적:
//   실제 센서(MPU6050/VL53L0X)가 아직 도착하지 않은 상태에서도
//   드라이버 로직(레지스터 파싱, 물리값 변환, 예외처리 연동)을
//   개발·테스트할 수 있도록 I2C 버스 접근을 인터페이스로 분리했다.
//
//   IRealI2CBus  -> 실제 /dev/i2c-1 (라즈베리파이, Linux 전용)
//   MockI2CBus   -> 가짜 레지스터 값을 리턴 (Windows PC 포함 어디서든 컴파일/실행 가능)
//
//   센서가 도착하면 드라이버 코드는 한 줄도 안 바꾸고
//   생성자에 넘기는 구현체만 Mock -> Real로 교체하면 된다.

#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>

// I2C 버스에 대한 최소 인터페이스: 레지스터 읽기/쓰기
class II2CBus {
public:
    virtual ~II2CBus() = default;

    // slave_addr 장치의 reg 레지스터부터 length 바이트를 읽어 out에 채운다.
    // 반환값: 성공 시 true
    virtual bool readRegisters(uint8_t slave_addr, uint8_t reg,
                                uint8_t* out, size_t length) = 0;

    // slave_addr 장치의 reg 레지스터에 value 1바이트를 쓴다.
    virtual bool writeRegister(uint8_t slave_addr, uint8_t reg, uint8_t value) = 0;
};

// 버스 접근 실패 시 던지는 예외 (SENSOR_FAULT 예외처리와 연결하기 위함)
class I2CBusException : public std::runtime_error {
public:
    explicit I2CBusException(const std::string& msg) : std::runtime_error(msg) {}
};
