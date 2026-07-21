// vl53l0x_stub.hpp
// VL53L0X ToF 센서 인터페이스 (구조만 정의, 실제 레지스터 구현은 보류)
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// [중요 - 확실한 사실] VL53L0X는 MPU6050처럼 레지스터 몇 개 읽어서 끝나는 칩이 아님.
//   SPAD(Single Photon Avalanche Diode) 캘리브레이션, 타이밍 버짓 설정 등
//   ST 공식 초기화 시퀀스(수백 줄)를 거쳐야 정확한 거리값이 나옴.
//   제조사(ST)도 자체 API(VL53L0X_API) 없이 raw 레지스터 접근을 권장하지 않음.
//
// [제안] 처음부터 재구현하지 말고 기존 검증된 경량 포팅 라이브러리를 가져다 붙이는 걸 추천:
//   - Pololu VL53L0X 라이브러리 (Arduino용이지만 I2C 레지스터 시퀀스는 이식 가능)
//   - vl53l0x-linux 계열 C 포팅 (라즈베리파이 대상으로 이미 존재)
//   둘 다 라이센스·의존성 확인 후 팀 논의해서 결정하는 걸 권장 (임의로 단정하지 않음).
//
// 지금 단계에서는 상위(danger_judgment_engine, 센서 퓨전) 로직이 이 인터페이스에만
// 의존하도록 해두면, 나중에 실제 구현을 갈아끼워도 상위 코드는 안 바뀐다.

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

// TODO(server 트랙 통합 전까지): 실제 구현체 결정 후 VL53L0XDriver 클래스로 교체
// 현재는 Mock으로만 존재 -> 센서 퓨전 로직 테스트용
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
