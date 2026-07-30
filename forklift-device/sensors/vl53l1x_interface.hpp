// vl53l1x_interface.hpp
// VL53L1X ToF 센서 인터페이스 (원래는 VL53L0X 대상으로 작성됐으나 센서 고장으로 교체됨)
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// [상태] 실제 센서는 VL53L1X로 교체됐고, 커널 캐릭터 디바이스 드라이버
//   (forklift-device/kernel/vl53l1x_driver.c)로 라즈베리파이에서 실측 검증까지 끝남.
//   /dev/vl53l1x0에서 거리값(mm)을 read()로 가져올 수 있고, 실제 구현체는
//   VL53L1XKernelDriver (vl53l1x_kernel_driver.hpp)이니 그쪽을 참고하면 됨.
//
// 지금 단계에서도 상위(danger_judgment_engine, 센서 퓨전) 로직이 이 인터페이스에만
// 의존하도록 해두면, MockTofSensor <-> VL53L1XKernelDriver 교체가 상위 코드 변경 없이 된다.

#pragma once
#include <cstdint>

struct TofSample {
    uint16_t distance_mm = 0;
    bool valid = false; // false면 SENSOR_FAULT 연동
};

class ITofSensor {
public:
    virtual ~ITofSensor() = default;
    virtual bool init() = 0;
    virtual TofSample readDistance() = 0;
};

// 센서 실물 없이 상위 센서 퓨전 로직을 테스트하기 위한 Mock.
// 실제 하드웨어 구현체는 VL53L1XKernelDriver (vl53l1x_kernel_driver.hpp) 참고.
class MockTofSensor : public ITofSensor {
public:
    bool init() override { return true; }

    void setNextDistance(uint16_t mm, bool valid = true) {
        next_mm_ = mm;
        next_valid_ = valid;
    }

    TofSample readDistance() override {
        return TofSample{next_mm_, next_valid_};
    }

private:
    uint16_t next_mm_ = 0;
    bool next_valid_ = true;
};
