// test_network_sensor_reader.cpp
// NetworkSensorReader = SensorUplinkReceiver(MQTT 캐시) -> SensorInput(판정 엔진 입력) 융합 접착부 검증.
//
// 수신기 자체(test_sensor_uplink_receiver)와 판정 엔진(test_exception_trigger)은 각각
// 검증돼 있고, 여기는 그 사이 "누가 isStale()을 읽어 imu_ok/tof_ok로 접는가"를 검증한다.
// 이 접착이 어긋나면 센서 끊김이 SENSOR_FAULT fail-safe 대신 정상값으로 판정된다.
//
// [테스트 1] 데이터 없음 -> read()가 imu_ok/tof_ok=false (fail-safe)
// [테스트 2] 실제 브로커로 페이로드 발행 -> 단위 변환(3축 g 합성)·컨텍스트 필드 전달 확인
// [테스트 3] 마지막 수신 후 stale timeout 경과 -> 다시 fail-safe
//
// [테스트 방식] test_sensor_uplink_receiver와 동일하게 실제 로컬 브로커(127.0.0.1:1883)를
// 쓴다. 발행은 mosquitto_pub CLI(std::system)로 한다 - 테스트 코드에 mosquitto 클라이언트
// 코드를 복제하지 않기 위함. 브로커가 없으면 실패하므로 ENABLE_NETWORK_INTEGRATION_TESTS로
// 기본 제외(동일 옵션).
//
// 실행: ./test_network_sensor_reader   (종료코드 0=성공, 1=실패)

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

#include "network/network_sensor_reader.hpp"

namespace {

using risk_transport::SensorUplinkReceiver;

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

std::string uniqueTerminalId(const std::string& suffix) {
    return "TEST_" + std::to_string(static_cast<long long>(::getpid())) + "_" + suffix;
}

bool publishViaCli(const std::string& terminal_id, const std::string& payload) {
    const std::string command =
        "mosquitto_pub -h 127.0.0.1 -p 1883 -t forklift/sensor/" + terminal_id +
        " -m '" + payload + "'";
    return std::system(command.c_str()) == 0;
}

bool waitUntil(int timeout_ms, const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return condition();
}

}  // namespace

int main() {
    // [테스트 1] start() 전 - 캐시에 아무것도 없으면 정상값으로 오해하지 않아야 한다.
    {
        std::cout << "[데이터 없음 -> fail-safe]\n";
        const std::string terminal = uniqueTerminalId("NOREADER");
        SensorUplinkReceiver receiver({terminal});
        NetworkSensorReader reader(receiver, terminal);
        const auto sen = reader.read();
        check(!sen.imu_ok && !sen.tof_ok,
              "데이터가 없으면 imu_ok/tof_ok가 모두 false (SENSOR_FAULT 유도)");
    }

    // [테스트 2] 실제 수신 -> 값이 SensorInput으로 옮겨 담긴다.
    {
        std::cout << "\n[정상 수신 -> 필드 매핑]\n";
        const std::string terminal = uniqueTerminalId("LIVE");
        SensorUplinkReceiver receiver({terminal});
        receiver.start();
        NetworkSensorReader reader(receiver, terminal);

        const std::string payload =
            "{\"camera_id\": \"CAM_01\", \"tof_ok\": true, \"tof_distance_mm\": 2500, "
            "\"imu_ok\": true, \"imu_accel_x_g\": 0.02, \"imu_accel_y_g\": -0.01, "
            "\"imu_accel_z_g\": 1.01, \"ts_ms\": 1234567890, "
            "\"message_id\": \"m7\", \"producer_run_id\": \"run-1\", \"sequence\": 7}";
        bool received = false;
        received = waitUntil(5000, [&] {
            publishViaCli(terminal, payload);
            risk_transport::SensorUplinkSample probe;
            return receiver.getLatest(terminal, probe);
        });
        check(received, "브로커 발행 후 수신기가 샘플을 캐시함");

        const auto sen = reader.read();
        check(sen.imu_ok && sen.tof_ok, "정상 수신 시 imu_ok/tof_ok가 true");
        constexpr double expected_g = std::sqrt(0.02 * 0.02 + 0.01 * 0.01 + 1.01 * 1.01);
        check(std::abs(sen.imu_accel_g - expected_g) < 1e-6, "IMU 3축 합성 크기 변환");
        check(sen.tof_distance_mm == 2500, "ToF 거리(mm) 그대로 전달");
        check(sen.sensor_message_id == "m7" && sen.sensor_sequence == 7 &&
                  sen.sensor_producer_run_id == "run-1",
              "상관 컨텍스트 필드 전달");
        check(sen.sensor_age_ms >= 0, "수신 경과시간 계산");
        receiver.stop();
    }

    // [테스트 3] stale -> fail-safe 복귀.
    {
        std::cout << "\n[stale -> fail-safe]\n";
        const std::string terminal = uniqueTerminalId("STALE");
        SensorUplinkReceiver receiver({terminal});
        receiver.start();
        NetworkSensorReader reader(receiver, terminal, /*stale_timeout_ms=*/300);

        const std::string payload =
            "{\"camera_id\": \"CAM_01\", \"tof_ok\": true, \"tof_distance_mm\": 1000, "
            "\"imu_ok\": true, \"imu_accel_x_g\": 0, \"imu_accel_y_g\": 0, "
            "\"imu_accel_z_g\": 1, \"ts_ms\": 1}";
        waitUntil(5000, [&] {
            publishViaCli(terminal, payload);
            risk_transport::SensorUplinkSample probe;
            return receiver.getLatest(terminal, probe);
        });
        check(reader.read().tof_ok, "직후에는 정상값");

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        const auto sen = reader.read();
        check(!sen.imu_ok && !sen.tof_ok, "timeout 경과 후 read()가 fail-safe로 접음");
        receiver.stop();
    }

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
