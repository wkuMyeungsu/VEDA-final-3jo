// test_sensor_uplink_receiver.cpp
// SensorUplinkReceiver(단말->서버 센서 업링크 수신기) 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   [테스트 1] hello 등록 후 정상 수신 - getLatest()가 보낸 값과 정확히 일치하는지
//   [테스트 2] 깨진 JSON/필드 누락을 받아도 죽지 않고, 그 줄만 버리고 다음 줄부터 정상 수신
//   [테스트 3] 마지막 수신 이후 timeout_ms를 넘기면 isStale()이 true로 바뀌는지
//   [테스트 4] 연결 전/데이터 전 isStale() 초기 상태 규약 (아래 [설계 결정] 참고)
//   [테스트 5] 단말이 끊고 나가도 재대기로 돌아가 다음 단말을 받고, 캐시는 유지되는지
//   [테스트 6] hello 없이 센서 줄부터 보내면 연결을 끊는지 (CameraAssignmentServer와 같은 규약)
//   [테스트 7] 과대 줄(개행 없이 계속 들어오는 입력)에서 재동기화되는지
//
// [설계 결정] isStale()의 초기 상태 = true (stale-until-proven-fresh)
//   판정 엔진은 센서값을 못 받을 때 SAFE로 낙관하지 않고 SENSOR_FAULT -> 최소 CAUTION을
//   유지하는 fail-safe 정책이다. "아직 한 번도 안 받음"을 fresh로 돌려주면 호출부가 기본값
//   스냅샷(tof_distance_mm=0)을 진짜 측정값으로 오해하게 되고, 그건 근거 없는 DANGER이거나
//   조용한 SAFE 둘 중 하나로 새어나간다. 그래서 "받은 게 없으면 항상 stale"로 통일하고,
//   "한 번도 못 받음"과 "받았는데 오래됨"의 구분은 hasSample()로 따로 노출했다.
//
// test_result_publisher.cpp와 같은 방식으로 소켓 mock 없이 로컬 loopback을 쓴다.
// 다만 방향이 반대라 테스트 쪽이 "보내는 단말"(client)이고 수신기가 서버다.
//
// 실행: ./test_sensor_uplink_receiver   (종료코드 0=성공, 1=실패)

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <cerrno>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "network/sensor_uplink_receiver.hpp"

namespace {

// 실제 운영 포트(9002 제안)와 겹치지 않게 별도 포트를 쓴다
// (test_result_publisher.cpp의 19001과 같은 관례).
constexpr uint16_t kTestPort = 19002;

using risk_transport::SensorUplinkReceiver;
using risk_transport::SensorUplinkSample;

// 테스트용 최소 TCP 단말. 수신기가 서버이므로 이쪽이 접속하고 보내는 쪽이다.
class FakeTerminal {
public:
    explicit FakeTerminal(uint16_t port) : port_(port) {}
    ~FakeTerminal() { close(); }

    // 수신기의 listen 소켓이 열릴 때까지 재시도하며 접속한다
    // (start() 직후에는 아직 bind/listen 전일 수 있다).
    bool connectWithin(int timeout_ms) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        do {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) return false;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port   = htons(port_);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                fd_ = fd;
                return true;
            }
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    bool sendLine(const std::string& line) { return sendRaw(line + "\n"); }

    bool sendRaw(const std::string& payload) {
        if (fd_ < 0) return false;
        std::size_t sent = 0;
        while (sent < payload.size()) {
            ssize_t n = ::send(fd_, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    // 서버가 이 연결을 끊었는지 확인한다 (EOF면 true). 이 채널은 서버가 보내는 게 없으므로
    // 읽어서 0이 나오는 것 = 서버가 close() 했다는 뜻이다.
    bool closedByServerWithin(int timeout_ms) {
        if (fd_ < 0) return false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char scratch[64];
        while (std::chrono::steady_clock::now() < deadline) {
            timeval tv{0, 50 * 1000};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ssize_t n = ::recv(fd_, scratch, sizeof(scratch), 0);
            if (n == 0) return true;
            if (n < 0 && !(errno == EAGAIN || errno == EWOULDBLOCK)) return true;
        }
        return false;
    }

    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    uint16_t port_;
    int fd_ = -1;
};

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

// 문서 스키마 그대로의 센서 줄 한 개.
std::string makeSampleLine(const std::string& camera_id, int tof_mm, int64_t ts_ms) {
    return std::string("{\"camera_id\": \"") + camera_id + "\", \"tof_ok\": true, "
           "\"tof_distance_mm\": " + std::to_string(tof_mm) + ", \"imu_ok\": true, "
           "\"imu_accel_x_g\": 0.02, \"imu_accel_y_g\": -0.01, \"imu_accel_z_g\": 1.01, "
           "\"ts_ms\": " + std::to_string(ts_ms) + "}";
}

std::string makeHello(const std::string& terminal_id) {
    return "{\"type\":\"hello\",\"terminal_id\":\"" + terminal_id + "\"}";
}

// 수신 카운터가 원하는 값에 도달할 때까지 기다린다 (워커 스레드 타이밍 흡수용).
bool waitForReceived(const SensorUplinkReceiver& rx, std::size_t want, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (rx.receivedCount() >= want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return rx.receivedCount() >= want;
}

bool waitForParseFailures(const SensorUplinkReceiver& rx, std::size_t want, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (rx.parseFailureCount() >= want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return rx.parseFailureCount() >= want;
}

bool waitForTerminalId(const SensorUplinkReceiver& rx, const std::string& want, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (rx.terminalId() == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return rx.terminalId() == want;
}

bool nearlyEqual(double a, double b) { return std::fabs(a - b) < 1e-9; }

// hello 등록 -> 센서 줄 수신 -> getLatest()가 보낸 값 그대로인지.
void testReceivesLatestSample() {
    std::cout << "[테스트 1] hello 등록 후 정상 수신 - getLatest()가 보낸 값과 일치\n";

    SensorUplinkReceiver rx("127.0.0.1", kTestPort);
    rx.start();

    FakeTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말이 수신기에 접속"); rx.stop(); return; }

    check(term.sendLine(makeHello("TERM_01")), "hello 전송");
    check(waitForTerminalId(rx, "TERM_01", 2000),
          "hello의 terminal_id가 등록됨 (실제: '" + rx.terminalId() + "')");

    const int64_t kTs = 1754380800123LL;
    check(term.sendLine(makeSampleLine("cam0", 420, kTs)), "센서 줄 1건 전송");
    check(waitForReceived(rx, 1, 2000),
          "수신 카운터 1 (실제: " + std::to_string(rx.receivedCount()) + ")");

    SensorUplinkSample s;
    const bool got = rx.getLatest(s);
    check(got, "getLatest()가 스냅샷을 돌려줌");
    if (got) {
        check(s.camera_id == "cam0", "camera_id == cam0 (실제: '" + s.camera_id + "')");
        check(s.tof_ok, "tof_ok == true");
        check(s.tof_distance_mm == 420,
              "tof_distance_mm == 420 (실제: " + std::to_string(s.tof_distance_mm) + ")");
        check(s.imu_ok, "imu_ok == true");
        check(nearlyEqual(s.imu_accel_x_g, 0.02), "imu_accel_x_g == 0.02");
        check(nearlyEqual(s.imu_accel_y_g, -0.01), "imu_accel_y_g == -0.01 (음수 파싱)");
        check(nearlyEqual(s.imu_accel_z_g, 1.01), "imu_accel_z_g == 1.01");
        check(s.ts_ms == kTs, "ts_ms == " + std::to_string(kTs) +
              " (실제: " + std::to_string(s.ts_ms) + ")");
        check(s.received_at.time_since_epoch().count() != 0,
              "received_at(서버 수신 시각)이 채워짐");
    }

    // 두 번째 줄이 캐시를 덮어쓰는지 (최신값 캐시 = last-write-wins)
    check(term.sendLine(makeSampleLine("cam1", 1350, kTs + 500)), "센서 줄 2건째 전송");
    check(waitForReceived(rx, 2, 2000), "수신 카운터 2");
    if (rx.getLatest(s)) {
        check(s.tof_distance_mm == 1350 && s.camera_id == "cam1",
              "최신값으로 덮어써짐 (실제: " + s.camera_id + "/" +
              std::to_string(s.tof_distance_mm) + ")");
    }

    check(rx.parseFailureCount() == 0,
          "파싱 실패 0 (실제: " + std::to_string(rx.parseFailureCount()) + ")");
    rx.stop();
}

// 깨진 JSON/필드 누락을 받아도 죽지 않고, 그 줄만 버리고 계속 받아야 한다.
void testSurvivesMalformedLines() {
    std::cout << "\n[테스트 2] 깨진 JSON - 서버가 죽지 않고 그 줄만 버리고 계속 수신\n";

    SensorUplinkReceiver rx("127.0.0.1", kTestPort);
    rx.start();

    FakeTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말 접속"); rx.stop(); return; }
    term.sendLine(makeHello("TERM_01"));
    check(waitForTerminalId(rx, "TERM_01", 2000), "hello 등록");

    // 정상 1건으로 기준선을 만든다.
    term.sendLine(makeSampleLine("cam0", 500, 1754380800000LL));
    check(waitForReceived(rx, 1, 2000), "정상 줄 1건 수신 (기준선)");

    // 여러 종류의 깨진 줄을 몰아 보낸다.
    const std::vector<std::pair<std::string, std::string>> broken = {
        {"{\"camera_id\": \"cam0\", \"tof_ok\": tr",                 "잘린 JSON"},
        {"이건 JSON이 아니다",                                        "JSON 아님"},
        {"{}",                                                        "빈 오브젝트"},
        {"{\"camera_id\": \"cam0\", \"tof_ok\": true, \"imu_ok\": true}", "필드 누락(거리/가속도/ts)"},
        {"{\"camera_id\": \"cam0\", \"tof_ok\": true, \"tof_distance_mm\": \"멀다\", "
         "\"imu_ok\": true, \"imu_accel_x_g\": 0.0, \"imu_accel_y_g\": 0.0, "
         "\"imu_accel_z_g\": 1.0, \"ts_ms\": 1}",                     "숫자 자리에 문자열"},
        {"[1,2,3]",                                                   "오브젝트가 아닌 배열"},
    };
    for (const auto& b : broken) check(term.sendLine(b.first), "깨진 줄 전송 - " + b.second);

    check(waitForParseFailures(rx, broken.size(), 2000),
          "깨진 줄 " + std::to_string(broken.size()) + "건이 전부 파싱 실패로 집계 (실제: " +
          std::to_string(rx.parseFailureCount()) + ")");
    check(rx.receivedCount() == 1,
          "깨진 줄은 캐시에 반영되지 않음 (수신 카운터 여전히 1, 실제: " +
          std::to_string(rx.receivedCount()) + ")");

    SensorUplinkSample s;
    if (rx.getLatest(s)) {
        check(s.tof_distance_mm == 500,
              "캐시는 마지막 정상값 500 그대로 (실제: " +
              std::to_string(s.tof_distance_mm) + ")");
    }

    // 핵심: 깨진 줄 뒤에도 연결이 살아 있고 다음 정상 줄이 그대로 들어와야 한다.
    check(rx.isConnected(), "깨진 줄을 받아도 연결이 유지됨");
    check(term.sendLine(makeSampleLine("cam0", 777, 1754380801000LL)),
          "깨진 줄 뒤 정상 줄 전송");
    check(waitForReceived(rx, 2, 2000),
          "다음 줄부터 정상 수신 재개 (수신 카운터 2, 실제: " +
          std::to_string(rx.receivedCount()) + ")");
    if (rx.getLatest(s)) {
        check(s.tof_distance_mm == 777,
              "복구 후 값이 캐시에 반영됨 (실제: " + std::to_string(s.tof_distance_mm) + ")");
    }

    rx.stop();
}

// 수신 직후에는 fresh, timeout_ms를 넘기면 stale.
// 기본 타임아웃(1200ms)을 실제로 기다리면 테스트가 느려지므로 짧은 timeout 인자로 검증하고,
// 같은 시점에 기본 타임아웃은 아직 fresh인지도 함께 확인한다.
void testStaleAfterTimeout() {
    std::cout << "\n[테스트 3] 타임아웃 - 마지막 수신 이후 timeout_ms 초과 시 isStale() == true\n";

    SensorUplinkReceiver rx("127.0.0.1", kTestPort);
    rx.start();

    FakeTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말 접속"); rx.stop(); return; }
    term.sendLine(makeHello("TERM_01"));
    term.sendLine(makeSampleLine("cam0", 420, 1754380800123LL));
    check(waitForReceived(rx, 1, 2000), "센서 줄 1건 수신");

    check(!rx.isStale(), "수신 직후에는 기본 타임아웃(" +
          std::to_string(SensorUplinkReceiver::kDefaultStaleTimeoutMs) + "ms) 기준 fresh");
    check(!rx.isStale(200), "수신 직후에는 200ms 기준으로도 fresh");

    // 단말이 조용해지는 구간을 만든다 (연결은 유지한 채 전송만 멈춤).
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    check(rx.isStale(200), "마지막 수신 350ms 뒤 - 200ms 타임아웃 기준 stale");
    check(!rx.isStale(), "같은 시점에 기본 1200ms 기준으로는 아직 fresh");
    check(rx.hasSample(), "stale이어도 마지막 스냅샷은 남아 있음 (hasSample() == true)");

    SensorUplinkSample s;
    check(rx.getLatest(s) && s.tof_distance_mm == 420,
          "stale 상태에서도 getLatest()는 마지막 값을 그대로 돌려줌");

    // 새 줄이 오면 다시 fresh로 돌아와야 한다.
    term.sendLine(makeSampleLine("cam0", 430, 1754380800500LL));
    check(waitForReceived(rx, 2, 2000), "새 줄 수신");
    check(!rx.isStale(200), "새 줄 수신 후 200ms 기준으로 다시 fresh");

    rx.stop();
}

// 연결 전 / hello만 받고 데이터 전 상태의 isStale() 규약.
void testStaleBeforeAnyData() {
    std::cout << "\n[테스트 4] 초기 상태 규약 - 받은 게 없으면 항상 stale\n";

    SensorUplinkReceiver rx("127.0.0.1", kTestPort);

    // (1) start() 전 - 소켓조차 없음
    check(rx.isStale(), "start() 전 isStale() == true");
    check(!rx.hasSample(), "start() 전 hasSample() == false");
    SensorUplinkSample s;
    s.tof_distance_mm = -12345;   // getLatest() 실패 시 out을 건드리지 않는지 확인용 표식
    check(!rx.getLatest(s), "start() 전 getLatest() == false");
    check(s.tof_distance_mm == -12345, "getLatest() 실패 시 out 파라미터를 건드리지 않음");

    // (2) start() 후, 단말 미접속 - 죽지 않고 대기만 해야 한다
    rx.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(rx.isStale(), "단말 미접속 구간에도 isStale() == true");
    check(!rx.isConnected(), "단말 미접속이므로 isConnected() == false");
    check(rx.acceptedCount() == 0, "accept 0회");

    // (3) 접속 + hello까지만 - 아직 센서값이 0건이므로 여전히 stale이어야 한다.
    //     (hello는 "누가 붙었는지"만 알려줄 뿐 센서값이 아니다)
    FakeTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말 접속"); rx.stop(); return; }
    term.sendLine(makeHello("TERM_01"));
    check(waitForTerminalId(rx, "TERM_01", 2000), "hello 등록 완료");
    check(rx.isConnected(), "연결됨 (isConnected() == true)");
    check(rx.isStale(), "hello만 받고 센서값 0건 - 여전히 isStale() == true");
    check(!rx.hasSample(), "hello만으로는 hasSample() == false");
    check(!rx.getLatest(s), "hello만으로는 getLatest() == false");

    // (4) 첫 센서 줄이 들어온 순간에만 fresh로 바뀐다.
    term.sendLine(makeSampleLine("cam0", 900, 1754380800123LL));
    check(waitForReceived(rx, 1, 2000), "첫 센서 줄 수신");
    check(!rx.isStale(), "첫 센서 줄 수신 후에야 fresh로 전환");
    check(rx.hasSample(), "hasSample() == true");

    rx.stop();
}

// 단말이 끊고 나가도 재대기로 돌아가 다음 단말을 받아야 한다.
// (단말 재부팅/IP 변경 시나리오. 캐시는 유지되고 신선도만 떨어진다.)
void testRelistenAfterDisconnect() {
    std::cout << "\n[테스트 5] 단말 재접속 - 연결 종료 후 re-listen, 캐시는 유지\n";

    SensorUplinkReceiver rx("127.0.0.1", kTestPort);
    rx.start();

    {
        FakeTerminal first(kTestPort);
        if (!first.connectWithin(3000)) { check(false, "첫 단말 접속"); rx.stop(); return; }
        first.sendLine(makeHello("TERM_01"));
        first.sendLine(makeSampleLine("cam0", 610, 1754380800123LL));
        check(waitForReceived(rx, 1, 2000), "첫 단말에서 1건 수신");
    }   // 소멸자에서 close() -> 단말이 끊고 나감

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
    while (rx.isConnected() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    check(!rx.isConnected(), "연결 종료를 감지해 재대기로 복귀");

    SensorUplinkSample s;
    check(rx.getLatest(s) && s.tof_distance_mm == 610,
          "연결이 끊겨도 마지막 스냅샷은 캐시에 남음");
    check(rx.terminalId() == "TERM_01",
          "끊긴 뒤에도 terminal_id 유지 (캐시 값의 출처를 알 수 있어야 함)");

    FakeTerminal second(kTestPort);
    check(second.connectWithin(3000), "두 번째 단말이 같은 포트로 접속");
    second.sendLine(makeHello("TERM_02"));
    check(waitForTerminalId(rx, "TERM_02", 2000),
          "재접속 단말의 terminal_id로 갱신 (실제: '" + rx.terminalId() + "')");
    second.sendLine(makeSampleLine("cam1", 250, 1754380801000LL));
    check(waitForReceived(rx, 2, 2000), "재접속 단말에서 수신 재개");
    check(rx.getLatest(s) && s.tof_distance_mm == 250, "재접속 단말 값으로 캐시 갱신");
    check(rx.acceptedCount() == 2,
          "누적 accept 2회 (실제: " + std::to_string(rx.acceptedCount()) + ")");

    rx.stop();
}

// hello 없이 센서 줄부터 보내면 연결을 끊는다 (CameraAssignmentServer와 같은 규약).
// 센서 줄은 terminal_id를 싣지 않으므로, hello가 없으면 뒤에 오는 값이 어느 단말 것인지
// 영원히 알 수 없다 -> 줄 하나 버리고 넘어갈 문제가 아니라 연결 자체를 끊는다.
void testRejectsDataBeforeHello() {
    std::cout << "\n[테스트 6] hello 누락 - 첫 줄이 hello가 아니면 연결 종료\n";

    SensorUplinkReceiver rx("127.0.0.1", kTestPort);
    rx.start();

    FakeTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말 접속"); rx.stop(); return; }

    check(term.sendLine(makeSampleLine("cam0", 420, 1754380800123LL)),
          "hello 없이 센서 줄부터 전송");
    check(term.closedByServerWithin(2000), "서버가 연결을 끊음 (단말 쪽에서 EOF 관측)");
    check(rx.receivedCount() == 0,
          "hello 전 센서 줄은 캐시에 반영되지 않음 (실제: " +
          std::to_string(rx.receivedCount()) + ")");
    check(rx.isStale(), "여전히 stale");

    // 끊긴 뒤에도 수신기는 살아 있어야 하고, 제대로 된 단말은 곧바로 붙을 수 있어야 한다.
    FakeTerminal good(kTestPort);
    check(good.connectWithin(3000), "hello를 지키는 단말은 곧바로 재접속 가능");
    good.sendLine(makeHello("TERM_01"));
    good.sendLine(makeSampleLine("cam0", 333, 1754380801000LL));
    check(waitForReceived(rx, 1, 2000), "정상 단말에서 수신 성공");

    rx.stop();
}

// 개행 없이 계속 들어오는 과대 입력에서 연결을 끊지 않고 다음 개행부터 재동기화하는지.
void testResyncsAfterOversizedLine() {
    std::cout << "\n[테스트 7] 과대 줄 - 연결을 끊지 않고 다음 개행부터 재동기화\n";

    SensorUplinkReceiver rx("127.0.0.1", kTestPort);
    rx.start();

    FakeTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말 접속"); rx.stop(); return; }
    term.sendLine(makeHello("TERM_01"));
    check(waitForTerminalId(rx, "TERM_01", 2000), "hello 등록");

    // 개행 없이 8KB (kMaxLineBytes=4096 초과) -> 버려지고 재동기화 대상이 된다.
    check(term.sendRaw(std::string(8192, 'x')), "개행 없는 8KB 전송");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    check(rx.isConnected(), "과대 줄을 받아도 연결이 유지됨");

    // 개행으로 그 줄을 닫고 나면 그 다음 줄부터 정상 처리돼야 한다.
    check(term.sendRaw("\n"), "개행으로 과대 줄 종료");
    check(term.sendLine(makeSampleLine("cam0", 1200, 1754380802000LL)), "이후 정상 줄 전송");
    check(waitForReceived(rx, 1, 2000),
          "재동기화 후 정상 수신 (실제: " + std::to_string(rx.receivedCount()) + ")");

    SensorUplinkSample s;
    check(rx.getLatest(s) && s.tof_distance_mm == 1200, "재동기화 후 값이 캐시에 반영됨");

    rx.stop();
}

} // namespace

int main() {
    std::cout << "=== SensorUplinkReceiver(단말->서버 센서 업링크) 테스트 ===\n\n";

    testReceivesLatestSample();
    testSurvivesMalformedLines();
    testStaleAfterTimeout();
    testStaleBeforeAnyData();
    testRelistenAfterDisconnect();
    testRejectsDataBeforeHello();
    testResyncsAfterOversizedLine();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
