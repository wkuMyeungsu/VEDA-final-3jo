// i2c_bus_linux.hpp
// 실제 라즈베리파이 I2C 버스(/dev/i2c-1) 구현
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// 주의: Linux 전용. Windows(MSVC)에서는 컴파일 자체가 안 되도록
//       #ifdef __linux__ 로 감싸뒀다 -> 팀 관례대로 Windows에서 더미(Mock)로
//       로직 검증 후 이 파일만 라즈베리파이에서 활성화하면 된다.
//
// 입력: I2C 슬레이브 주소(7bit), 레지스터 주소
// 처리: open("/dev/i2c-1") -> ioctl(I2C_SLAVE) -> write(reg) -> read(length)
// 출력: 레지스터 값 (성공 시 true, 실패 시 false + I2CBusException)

#pragma once
#include "i2c_bus.hpp"

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <string>

class LinuxI2CBus : public II2CBus {
public:
    // bus_path 예: "/dev/i2c-1" (라즈베리파이 기본 I2C 버스)
    explicit LinuxI2CBus(const std::string& bus_path = "/dev/i2c-1") {
        fd_ = open(bus_path.c_str(), O_RDWR);
        if (fd_ < 0) {
            throw I2CBusException("I2C 버스 열기 실패: " + bus_path +
                                   " (I2C 활성화됐는지, 권한 있는지 확인 필요)");
        }
    }

    ~LinuxI2CBus() override {
        if (fd_ >= 0) close(fd_);
    }

    bool readRegisters(uint8_t slave_addr, uint8_t reg,
                        uint8_t* out, size_t length) override {
        if (!selectSlave(slave_addr)) return false;

        // 읽을 레지스터 주소 먼저 씀 (레지스터 포인터 설정)
        if (write(fd_, &reg, 1) != 1) return false;

        // 이어서 length 바이트 읽기
        ssize_t n = read(fd_, out, length);
        return n == static_cast<ssize_t>(length);
    }

    bool writeRegister(uint8_t slave_addr, uint8_t reg, uint8_t value) override {
        if (!selectSlave(slave_addr)) return false;

        uint8_t buf[2] = {reg, value};
        return write(fd_, buf, 2) == 2;
    }

private:
    bool selectSlave(uint8_t slave_addr) {
        if (ioctl(fd_, I2C_SLAVE, slave_addr) < 0) {
            return false;
        }
        return true;
    }

    int fd_ = -1;
};

#endif // __linux__
