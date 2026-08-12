// main.cpp
// sensor_uplink_publisher - IMU/ToF 센서를 주기적으로 읽어 MQTT로 서버에 올리는
// 단말 쪽 송신기. Qt 단말 앱(operator_terminal)과는 완전히 분리된 별도 프로세스/
// 실행 파일이다 - 서로 통신하지 않는다.
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈) / 트랙: forklift-device
//
// 서버 쪽 수신기(server/src/network/sensor_uplink_receiver.{hpp,cpp})와 짝을 이루는
// 반대편 끝이다. 프로토콜은 그쪽 헤더 주석에 정의된 것과 동일:
//   토픽: forklift/sensor/<terminal_id>  (QoS 0, retain 없음)
//   payload(JSON, 8개 필드 전부 필수 - 서버 파서가 하나라도 빠지면 메시지 전체를 버림):
//     {"camera_id":"cam0","tof_ok":true,"tof_distance_mm":420,"imu_ok":true,
//      "imu_accel_x_g":0.02,"imu_accel_y_g":-0.01,"imu_accel_z_g":1.01,
//      "ts_ms":1754380800123}
//
// 센서 읽기가 실패해도(tof_ok=false / imu_ok=false) 8개 필드는 항상 채워서 보낸다 -
// 필드를 생략하면 서버가 메시지 전체를 버리기 때문에, 실패한 값은 마지막으로 유효했던
// 값(프로세스 시작 직후라 유효값이 아직 없으면 0)을 placeholder로 그대로 실어 보낸다.
//
// camera_id는 Qt 프로세스에 묻지 않고 이 실행 파일 자체 설정 파일(config/sensor.json)
// 에서만 읽는다 - qt/config/terminal.json의 default_camera_id는 일부러 재사용하지
// 않았다: 그 값은 Qt 쪽 화면 표시용 기본 카메라 선택이라 의미가 다르고, 다른 트랙의
// 설정 파일 스키마에 이 프로세스가 몰래 얹혀가면 그쪽이 바뀔 때 여기가 조용히 깨진다.
// 대신 두 파일의 camera_id 값이 서로 어긋나지 않게 유지하는 건 배포 시 사람이 맞춰야
// 하는 부분이다(자동 동기화 없음).
//
// Linux 전용 (실제 하드웨어: LinuxI2CBus + Mpu6050Driver, VL53L1XKernelDriver).
// main_test.cpp(Mock 전용)와 달리 이 실행 파일은 실물 센서 없이는 의미가 없어
// Windows 등에서는 아예 컴파일을 막는다(test_mpu6050_real.cpp와 같은 관례).

#ifndef __linux__
#error "sensor_uplink_publisher는 라즈베리파이(Linux) 전용입니다."
#endif

#include "i2c_bus_linux.hpp"
#include "mpu6050_driver.hpp"
#include "vl53l1x_kernel_driver.hpp"
#include "sensor_collector.hpp"

#include <mosquitto.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

// risk_event 채널의 하트비트 주기(200ms)에 맞춘 제안값 - 팀 확정 아님.
// 바뀌면 이 상수만 고치면 된다.
constexpr int kPublishIntervalMs = 200;

constexpr const char* kDefaultConfigPath = "config/sensor.json";
// 로컬 테스트 단계 placeholder - 나중에 실제 서버 주소로 교체 예정.
constexpr const char* kDefaultBrokerHost = "localhost";
constexpr int kDefaultBrokerPort = 1883;
constexpr const char* kDefaultTerminalId = "TERM_01";

constexpr int kReconnectDelayMinSec = 3;
constexpr int kReconnectDelayMaxSec = 30;

struct PublisherConfig {
    std::string camera_id;                        // 필수. 설정 파일에만 있음 - 하드코딩/CLI 인자 없음.
    std::string terminal_id = kDefaultTerminalId;  // 설정 파일 또는 --terminal-id로 덮어씀.
    std::string broker_host = kDefaultBrokerHost;  // 설정 파일 또는 --broker-host로 덮어씀.
    int         broker_port = kDefaultBrokerPort;  // 설정 파일 또는 --broker-port로 덮어씀.
};

// ── 최소 JSON 파서 ───────────────────────────────────────────────────────
// server/src/network/sensor_uplink_receiver.cpp의 문자열 검색 파서와 같은 방식이다.
// 설정 파일은 중첩 없는 평면 오브젝트라 이 정도로 충분하고, 이 실행 파일 하나 때문에
// JSON 라이브러리를 forklift-device 트랙에 새로 들여올 이유가 없다.
std::size_t findValueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) return std::string::npos;
    auto colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) return std::string::npos;
    return json.find_first_not_of(" \t", colon_pos + 1);
}

bool extractString(const std::string& json, const std::string& key, std::string& out) {
    auto pos = findValueStart(json, key);
    if (pos == std::string::npos || json[pos] != '"') return false;
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return false;
    out = json.substr(pos + 1, end - pos - 1);
    return true;
}

bool extractInt(const std::string& json, const std::string& key, int& out) {
    auto pos = findValueStart(json, key);
    if (pos == std::string::npos) return false;
    const char* begin = json.c_str() + pos;
    char* end = nullptr;
    long v = std::strtol(begin, &end, 10);
    if (end == begin) return false;
    out = static_cast<int>(v);
    return true;
}

// 설정 파일을 읽어 config에 반영한다. camera_id는 여기서만 채워진다.
// 파일을 못 열거나 camera_id가 없으면 false - 상위에서 fatal로 처리한다.
bool loadConfigFile(const std::string& path, PublisherConfig& config, std::string& why) {
    std::ifstream file(path);
    if (!file) {
        why = "설정 파일을 열 수 없음: " + path;
        return false;
    }
    std::ostringstream buf;
    buf << file.rdbuf();
    const std::string content = buf.str();

    if (!extractString(content, "camera_id", config.camera_id) || config.camera_id.empty()) {
        why = "camera_id 누락 - " + path + "에 반드시 있어야 함";
        return false;
    }

    // 나머지 필드는 선택 - 없으면 기본값 유지(이후 CLI 인자로 다시 덮어쓸 수 있음).
    std::string terminal_id;
    if (extractString(content, "terminal_id", terminal_id)) config.terminal_id = terminal_id;

    std::string broker_host;
    if (extractString(content, "broker_host", broker_host)) config.broker_host = broker_host;

    int broker_port = 0;
    if (extractInt(content, "broker_port", broker_port)) config.broker_port = broker_port;

    return true;
}

void printUsageAndExit(const char* argv0) {
    std::cerr << "사용법: " << argv0
              << " [--config <path>] [--terminal-id <id>] [--broker-host <host>] [--broker-port <port>]\n";
    std::exit(1);
}

struct CliOverrides {
    std::string config_path = kDefaultConfigPath;
    std::string terminal_id;
    std::string broker_host;
    int         broker_port = 0;
    bool        has_terminal_id = false;
    bool        has_broker_host = false;
    bool        has_broker_port = false;
};

CliOverrides parseArgs(int argc, char** argv) {
    CliOverrides cli;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << flag << " 뒤에 값이 없습니다\n";
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "--config") {
            cli.config_path = next("--config");
        } else if (arg == "--terminal-id") {
            cli.terminal_id = next("--terminal-id");
            cli.has_terminal_id = true;
        } else if (arg == "--broker-host") {
            cli.broker_host = next("--broker-host");
            cli.has_broker_host = true;
        } else if (arg == "--broker-port") {
            cli.broker_port = std::atoi(next("--broker-port").c_str());
            cli.has_broker_port = true;
        } else {
            std::cerr << "알 수 없는 인자: " << arg << "\n";
            printUsageAndExit(argv[0]);
        }
    }
    return cli;
}

// 8개 필드를 항상 전부 채워서 직렬화한다(실패 시에도 last_valid에 남아있는
// placeholder 값을 그대로 싣는다 - 필드 생략 금지).
std::string buildPayloadJson(const std::string& camera_id, const CombinedSensorReading& last_valid,
                              std::int64_t ts_ms) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4);
    out << "{"
        << "\"camera_id\":\"" << camera_id << "\","
        << "\"tof_ok\":" << (last_valid.tof.valid ? "true" : "false") << ","
        << "\"tof_distance_mm\":" << last_valid.tof.distance_mm << ","
        << "\"imu_ok\":" << (last_valid.imu.valid ? "true" : "false") << ","
        << "\"imu_accel_x_g\":" << last_valid.imu.accel_x_g << ","
        << "\"imu_accel_y_g\":" << last_valid.imu.accel_y_g << ","
        << "\"imu_accel_z_g\":" << last_valid.imu.accel_z_g << ","
        << "\"ts_ms\":" << ts_ms
        << "}";
    return out.str();
}

std::atomic<bool> g_stop{false};
void onStopSignal(int) { g_stop = true; }

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, onStopSignal);
    std::signal(SIGTERM, onStopSignal);

    const CliOverrides cli = parseArgs(argc, argv);

    PublisherConfig config;
    std::string why;
    if (!loadConfigFile(cli.config_path, config, why)) {
        std::cerr << "[sensor_uplink_publisher] 설정 로드 실패: " << why << "\n";
        return 1;
    }
    if (cli.has_terminal_id) config.terminal_id = cli.terminal_id;
    if (cli.has_broker_host) config.broker_host = cli.broker_host;
    if (cli.has_broker_port) config.broker_port = cli.broker_port;

    std::cout << "[sensor_uplink_publisher] camera_id=" << config.camera_id
              << " terminal_id=" << config.terminal_id
              << " broker=" << config.broker_host << ":" << config.broker_port
              << " interval_ms=" << kPublishIntervalMs << "\n";

    // 실제 하드웨어 구성. main_test.cpp와 달리 Mock을 쓰지 않는다 - 이 실행 파일은
    // 라즈베리파이에서 실물 센서와 함께 도는 게 전제다.
    LinuxI2CBus i2c_bus("/dev/i2c-1");
    mpu6050::Mpu6050Driver imu(i2c_bus);
    VL53L1XKernelDriver tof("/dev/vl53l1x0");

    if (!imu.init()) {
        std::cerr << "[sensor_uplink_publisher] IMU init() 실패 - 배선/권한 확인 필요"
                     " (계속 진행, 매 주기 imu_ok=false로 보고됨)\n";
    }
    if (!tof.init()) {
        std::cerr << "[sensor_uplink_publisher] ToF init() 실패 - /dev/vl53l1x0 확인 필요"
                     " (계속 진행, 매 주기 tof_ok=false로 보고됨)\n";
    }

    SensorCollector collector(imu, tof);

    mosquitto_lib_init();
    mosquitto* mosq = mosquitto_new(/*id=*/nullptr, /*clean_session=*/true, /*userdata=*/nullptr);
    if (!mosq) {
        std::cerr << "[sensor_uplink_publisher] mosquitto_new 실패\n";
        mosquitto_lib_cleanup();
        return 1;
    }
    mosquitto_reconnect_delay_set(mosq, kReconnectDelayMinSec, kReconnectDelayMaxSec, true);

    if (mosquitto_connect(mosq, config.broker_host.c_str(), config.broker_port, /*keepalive=*/60)
        != MOSQ_ERR_SUCCESS) {
        std::cerr << "[sensor_uplink_publisher] 브로커 연결 실패 (" << config.broker_host << ":"
                  << config.broker_port << ") - 재접속은 백그라운드 루프가 계속 시도함\n";
    }
    mosquitto_loop_start(mosq);

    const std::string topic = "forklift/sensor/" + config.terminal_id;

    // 마지막으로 유효했던 값의 스냅샷. 초기값은 값-초기화(aggregate {})로 전부 0/false가
    // 되므로, 시작 직후 첫 실패에서도 placeholder(0)가 자연스럽게 채워진다.
    CombinedSensorReading last_valid{};

    while (!g_stop.load()) {
        const CombinedSensorReading reading = collector.pollOnce();

        if (reading.imu.valid) {
            last_valid.imu = reading.imu;
        } else {
            last_valid.imu.valid = false; // accel 값은 마지막 유효값을 그대로 둔다
        }

        if (reading.tof.valid) {
            last_valid.tof = reading.tof;
        } else {
            last_valid.tof.valid = false; // distance_mm은 마지막 유효값을 그대로 둔다
        }

        const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();

        const std::string payload = buildPayloadJson(config.camera_id, last_valid, ts_ms);
        mosquitto_publish(mosq, /*mid=*/nullptr, topic.c_str(),
                           static_cast<int>(payload.size()), payload.c_str(),
                           /*qos=*/0, /*retain=*/false);

        std::this_thread::sleep_for(std::chrono::milliseconds(kPublishIntervalMs));
    }

    std::cout << "\n[sensor_uplink_publisher] 종료 신호 수신 - 정리 중\n";
    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}
