// vl53l1x_kernel_driver.hpp
// VL53L1XKernelDriver: forklift-device/kernel/vl53l1x_driver.c 캐릭터 디바이스를
// 읽어서 ITofSensor로 노출하는 얇은 유저스페이스 어댑터.
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// 주의: Linux 전용 (i2c_bus_linux.hpp와 같은 관례로 #ifdef __linux__ 로 감쌈).
//       Windows에서는 이 파일이 통째로 비어있는 것처럼 컴파일되고, 로직 검증은
//       MockTofSensor로 하면 됨.
//
// 커널 드라이버 동작(vl53l1x_driver.c 기준):
//   - open()마다 한 줄만 읽힘 (offset 기반 EOF) -> 새 측정값이 필요하면 매번 재open
//   - read() 성공 시 "<mm>\n" (정상) 또는 "-1\n" (무효 측정, 에러 아님)
//   - I2C 통신 자체가 실패하면 read()가 -1을 반환하고 errno가 설정됨
//
// 입력: 디바이스 경로 (기본 "/dev/vl53l1x0")
// 처리: open -> read(mm 단위 정수 문자열) -> close, 파싱해서 TofSample로 변환
// 출력: TofSample (distance_mm 음수 문자열("-1") 또는 open/read 실패 시 valid=false)

#pragma once
#include "vl53l1x_interface.hpp"

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <string>

class VL53L1XKernelDriver : public ITofSensor {
public:
    explicit VL53L1XKernelDriver(std::string device_path = "/dev/vl53l1x0")
        : device_path_(std::move(device_path)) {}

    // 디바이스 노드가 열리는지만 확인 (probe 실패 시 open()에서 errno 발생)
    bool init() override {
        int fd = open(device_path_.c_str(), O_RDONLY);
        if (fd < 0) return false;
        close(fd);
        return true;
    }

    TofSample readDistance() override {
        TofSample sample{};

        int fd = open(device_path_.c_str(), O_RDONLY);
        if (fd < 0) {
            sample.valid = false;
            return sample;
        }

        char buf[16] = {0};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);

        if (n <= 0) {
            sample.valid = false; // I2C 에러 등: 드라이버가 read() 자체를 실패시킴
            return sample;
        }
        buf[n] = '\0';

        long mm = strtol(buf, nullptr, 10);
        if (mm < 0) {
            sample.valid = false; // "-1\n": 무효 측정 (에러는 아님)
            return sample;
        }

        sample.distance_mm = static_cast<uint16_t>(mm);
        sample.valid = true;
        return sample;
    }

private:
    std::string device_path_;
};

#endif // __linux__
