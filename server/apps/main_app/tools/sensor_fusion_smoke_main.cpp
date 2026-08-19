// sensor_fusion_smoke_main.cpp
// [로컬 하드웨어 스모크 테스트] 실제 I2C 센서(MPU6050 + VL53L1X)가 연결된
// 라즈베리파이에서만 의미가 있다. danger_engine 실행 경로(danger_judgment_engine_main.cpp,
// ResultPublisher/ResultDispatcher/EventLogger/LatencyLogger, server-단말 TCP/JSON)와는
// 완전히 무관한 별도 실행파일이며, 그쪽 어떤 코드도 건드리지 않는다.
//
// 목적: "진짜 IMU/ToF 센서값이 SensorCollector -> SensorCollectorReader를 거쳐
// DangerJudgmentEngine::evaluate()까지 정상 전달되는가"만 확인한다. JudgmentPipeline은
// 쓰지 않는다. 카메라 입력은 하드웨어 센서 경로만 분리해서 보기 위한 고정값이다.
//
// 입력: 실제 I2C 버스(/dev/i2c-1)의 MPU6050(IMU) + 커널 캐릭터 디바이스(/dev/vl53l1x0)의
//       VL53L1X(ToF). 카메라 쪽은 테스트용 CameraInput 고정값.
// 처리: SensorCollectorReader::read()로 매 회 실제 센서값을 SensorInput으로 변환 ->
//       DangerJudgmentEngine::evaluate(cam, sen) 호출.
// 출력: 매 회 SensorInput 필드(imu_ok/tof_ok/tof_distance_mm/imu_accel_g)와
//       JudgmentResult(camera_risk/tof_risk/final_risk/exception)를 콘솔에 출력.
//
// 이 파일은 자동 CTest에 등록하지 않는 하드웨어 진단 도구다.
// 실행하려면 배포 장비의 센서 드라이버/헤더를 포함한 별도 수동 빌드가 필요하다.
// 실제 I2C 하드웨어 필요, 라즈베리파이에서 실행: sudo ./sensor_fusion_smoke_main
//   (권한 거부되면 sudo 필요 - I2C 디바이스 파일 접근 권한)

#include <iomanip>
#include <iostream>
#include <thread>
#include <chrono>

#include "logic/judgment/danger_judgment_engine.h"
#include "input/sensor_collector_reader.h"

#include "i2c_bus_linux.hpp"
#include "mpu6050_driver.hpp"
#include "vl53l1x_kernel_driver.hpp"
#include "sensor_collector.hpp"

#ifndef __linux__
#error "이 스모크 테스트는 Linux(라즈베리파이) 전용입니다 (LinuxI2CBus/VL53L1XKernelDriver가 __linux__ 가드 안에서만 컴파일됨)."
#endif

int main() {
    std::cout << "=== 센서 퓨전 스모크 테스트 (실제 IMU/ToF -> DangerJudgmentEngine) ===\n\n";

    // IMU: 실제 I2C 버스(/dev/i2c-1) 위의 MPU6050. ToF는 별도 커널 캐릭터 디바이스
    // (/dev/vl53l1x0)를 직접 여닫으므로 II2CBus를 주입받지 않는다.
    LinuxI2CBus i2c_bus("/dev/i2c-1");
    mpu6050::Mpu6050Driver imu(i2c_bus);
    VL53L1XKernelDriver tof("/dev/vl53l1x0");

    if (!imu.init()) {
        std::cerr << "IMU init() 실패 - I2C 통신 자체가 안 됨. 배선/권한 확인 필요\n";
        return 1;
    }
    if (!tof.init()) {
        std::cerr << "ToF init() 실패 - /dev/vl53l1x0 열기 실패. 커널 드라이버 로드 여부 확인 필요\n";
        return 1;
    }
    std::cout << "IMU/ToF init() 성공\n\n";

    SensorCollector collector(imu, tof);
    SensorCollectorReader reader(collector);

    const forklift::config::DangerJudgmentConfig judgment_config{
        3000.0, 1500.0, 400.0, 100.0, 1000.0, 500.0, 2.0, 0.0};
    DangerJudgmentEngine engine(judgment_config, std::chrono::milliseconds(500));

    // 센서 하드웨어 경로에 집중하기 위한 안전 거리 CameraInput 고정값이다.
    // CameraInput 인자 순서: forklift_localized, person_detected, forklift,
    // person, camera_id, zone. 카메라 쪽은 이번 테스트 범위 밖이라 값을 바꾸지 않는다.
    const CameraInput cam{true, true, {0.0, 0.0}, {5000.0, 5000.0}, "", ""};

    constexpr int kIterations = 10;
    for (int i = 0; i < kIterations; ++i) {
        const SensorInput sen = reader.read();
        const JudgmentResult r = engine.evaluate(cam, sen);

        std::cout << "[" << i << "] SensorInput: "
                  << "imu_ok=" << (sen.imu_ok ? "true" : "false") << " "
                  << "tof_ok=" << (sen.tof_ok ? "true" : "false") << " "
                  << "tof_distance_mm=" << std::fixed << std::setprecision(3) << sen.tof_distance_mm << " "
                  << "imu_accel_g=" << sen.imu_accel_g << "\n";
        std::cout << "     JudgmentResult: "
                  << "camera_risk=" << toString(r.camera_risk) << " "
                  << "tof_risk=" << toString(r.tof_risk) << " "
                  << "final_risk=" << toString(r.final_risk) << " "
                  << "exception=" << toString(r.exception) << "\n\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::cout << "=== 스모크 테스트 종료 (" << kIterations << "회) ===\n";
    return 0;
}
