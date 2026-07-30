// test_mpu6050_real.cpp
// MPU6050 실측 스모크 테스트 - LinuxI2CBus로 실제 하드웨어 읽기
// main_test.cpp는 Mock 전용(이식성 유지 목적)이라 이 파일로 분리함
// Linux 전용 (LinuxI2CBus가 __linux__ 가드 안에서만 컴파일됨)
//
// 빌드: g++ -std=c++17 test_mpu6050_real.cpp -o test_mpu6050_real
// 실행: sudo ./test_mpu6050_real   (권한 거부되면 sudo 필요)

#include "i2c_bus_linux.hpp"
#include "mpu6050_driver.hpp"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

#ifndef __linux__
#error "이 테스트는 Linux(라즈베리파이) 전용입니다."
#endif

int main() {
    std::cout << "=== MPU6050 실측 스모크 테스트 ===\n";

    LinuxI2CBus bus("/dev/i2c-1");
    mpu6050::Mpu6050Driver imu(bus);

    // 1. WHO_AM_I 확인
    uint8_t who = imu.whoAmI();
    std::cout << "WHO_AM_I = 0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(who) << std::dec << "  ";
    if (who == 0x68)      std::cout << "(정품 MPU6050)\n";
    else if (who == 0x72) std::cout << "(클론 GY-521, 알려진 동작 - 레지스터 맵 정상)\n";
    else                  std::cout << "(예상치 못한 값 - 배선/주소 확인 필요, 계속 진행)\n";

    // 2. 초기화 (sleep 모드 해제)
    if (!imu.init()) {
        std::cerr << "init() 실패 - I2C 통신 자체가 안 됨. 배선/권한 확인 필요\n";
        return 1;
    }
    std::cout << "init() 성공\n\n";

    // 3. 샘플 10회 읽기
    int fail_count = 0;
    for (int i = 0; i < 10; i++) {
        auto s = imu.readSample();
        if (!s.valid) {
            std::cout << "[" << i << "] 읽기 실패\n";
            fail_count++;
        } else {
            std::cout << "[" << i << "] "
                      << "accel(g)=(" << std::fixed << std::setprecision(3)
                      << s.accel_x_g << ", " << s.accel_y_g << ", " << s.accel_z_g << ")  "
                      << "gyro(dps)=(" << s.gyro_x_dps << ", " << s.gyro_y_dps << ", " << s.gyro_z_dps << ")\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n=== 결과: " << (10 - fail_count) << "/10 성공 ===\n";
    std::cout << "\n참고: 센서를 평평하게 두고 측정했다면 accel_z가 약 ±1.0g 근처,\n"
                 "      accel_x/y는 0g 근처여야 정상입니다 (중력 방향 확인).\n";

    return fail_count == 10 ? 1 : 0;
}
