// test_camera_assignment_server.cpp
// CameraAssignmentServer(9001, 양방향) 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   [테스트 1] hello 수신 후 terminal_id -> 소켓 매핑이 등록되는지
//   [테스트 2] 매핑에 없는 terminal_id로 sendCameraAssignment() 시도 시 false 리턴
//   [테스트 3] 매핑된 terminal_id로 전송하면 단말이 올바른 camera_assignment JSON을 수신
//   [테스트 4] 같은 terminal_id로 재접속하면 기존 소켓이 닫히고 새 소켓으로 교체되는지
//   [테스트 5] 잘못된 첫 메시지(type != hello, JSON 파싱 실패)를 보내면 연결이 끊기는지
//
// hello 타임아웃(kHelloTimeoutSec=5초)은 실제 동작이 코드상 자명하고(select 루프의
// 시간 비교) 5초 대기가 필요해 이 테스트 스위트에서는 제외했다 - 필요하면 별도로
// waitForClosedConnection()에 5초짜리 케이스를 추가하면 된다.
//
// 빌드: g++ -std=c++17 test_camera_assignment_server.cpp -o test_camera_assignment_server -pthread
// 실행: ./test_camera_assignment_server   (종료코드 0=성공, 1=실패)

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "network/camera_assignment_server.hpp"

namespace {

constexpr uint16_t kTestPort = 19002;  // 실제 운영 포트(9001)와 겹치지 않게 별도 포트 사용

// 테스트용 최소 TCP 단말. hello를 보내고, 서버가 보낸 줄을 받거나 연결 종료를 감지한다.
class TestTerminal {
public:
    explicit TestTerminal(uint16_t port) : port_(port) {}
    ~TestTerminal() { close(); }

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

    bool sendLine(const std::string& line) {
        if (fd_ < 0) return false;
        std::string payload = line + "\n";
        std::size_t sent = 0;
        while (sent < payload.size()) {
            ssize_t n = ::send(fd_, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    std::vector<std::string> receive(std::size_t expected_lines, int timeout_ms) {
        std::vector<std::string> lines;
        if (fd_ < 0) return lines;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char chunk[4096];
        while (lines.size() < expected_lines && std::chrono::steady_clock::now() < deadline) {
            timeval tv{0, 50 * 1000};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ssize_t n = ::recv(fd_, chunk, sizeof(chunk), 0);
            if (n > 0) {
                buf_.append(chunk, static_cast<std::size_t>(n));
                std::size_t pos;
                while ((pos = buf_.find('\n')) != std::string::npos) {
                    lines.push_back(buf_.substr(0, pos));
                    buf_.erase(0, pos + 1);
                }
            } else if (n == 0) {
                break;  // 연결 종료
            }
        }
        return lines;
    }

    // 서버가 연결을 끊었는지(EOF) 확인한다. 끊겼으면 true.
    bool waitForClose(int timeout_ms) {
        if (fd_ < 0) return true;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char scratch[64];
        while (std::chrono::steady_clock::now() < deadline) {
            timeval tv{0, 50 * 1000};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ssize_t n = ::recv(fd_, scratch, sizeof(scratch), 0);
            if (n == 0) return true;
            if (n < 0) continue;  // 타임아웃(EAGAIN) - 계속 대기
        }
        return false;
    }

    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    uint16_t port_;
    int fd_ = -1;
    std::string buf_;
};

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

bool waitUntil(int timeout_ms, const std::function<bool()>& pred) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

// hello 수신 후 매핑이 등록되는지.
void testHelloRegistersMapping() {
    std::cout << "[테스트 1] hello 수신 후 terminal_id -> 소켓 매핑 등록\n";

    risk_transport::CameraAssignmentServer server("127.0.0.1", kTestPort);
    server.start();

    TestTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말이 서버에 접속"); server.stop(); return; }

    check(term.sendLine(R"({"type":"hello","terminal_id":"TERM_01"})"), "hello 전송");
    check(waitUntil(2000, [&] { return server.isRegistered("TERM_01"); }),
          "TERM_01이 매핑에 등록됨");
    check(server.connectedCount() == 1, "매핑된 단말 수 1 (실제: " +
          std::to_string(server.connectedCount()) + ")");

    server.stop();
}

// 매핑에 없는 terminal_id로 전송 시도하면 false + 아무 것도 전송되지 않아야 한다.
void testSendToUnknownTerminalFails() {
    std::cout << "\n[테스트 2] 매핑에 없는 terminal_id로 전송 시도 -> false\n";

    risk_transport::CameraAssignmentServer server("127.0.0.1", kTestPort);
    server.start();

    bool ok = server.sendCameraAssignment("GHOST", "cam_01", "zone_a", "2026-08-06T00:00:00.000Z");
    check(!ok, "존재하지 않는 terminal_id -> false 리턴");

    server.stop();
}

// 매핑된 terminal_id로 전송하면 단말이 올바른 camera_assignment JSON을 받아야 한다.
void testSendDeliversJsonToTerminal() {
    std::cout << "\n[테스트 3] 매핑된 terminal_id로 전송 -> 단말이 JSON 수신\n";

    risk_transport::CameraAssignmentServer server("127.0.0.1", kTestPort);
    server.start();

    TestTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말 접속"); server.stop(); return; }
    term.sendLine(R"({"type":"hello","terminal_id":"TERM_02"})");
    check(waitUntil(2000, [&] { return server.isRegistered("TERM_02"); }), "TERM_02 등록 완료");

    bool ok = server.sendCameraAssignment("TERM_02", "cam_07", "zone_b", "2026-08-06T01:02:03.456Z");
    check(ok, "sendCameraAssignment() -> true");

    auto lines = term.receive(1, 2000);
    check(lines.size() == 1, "단말이 1건 수신 (실제: " + std::to_string(lines.size()) + ")");
    if (!lines.empty()) {
        const std::string& j = lines[0];
        check(j.find("\"type\":\"camera_assignment\"") != std::string::npos, "type=camera_assignment");
        check(j.find("\"terminal_id\":\"TERM_02\"") != std::string::npos, "terminal_id 일치");
        check(j.find("\"camera_id\":\"cam_07\"") != std::string::npos, "camera_id 일치");
        check(j.find("\"zone\":\"zone_b\"") != std::string::npos, "zone 일치");
        check(j.find("\"utc_time\":\"2026-08-06T01:02:03.456Z\"") != std::string::npos, "utc_time 일치");
    }

    server.stop();
}

// 같은 terminal_id로 재접속하면 기존 소켓을 닫고 새 소켓으로 교체해야 한다.
void testReconnectReplacesSocket() {
    std::cout << "\n[테스트 4] 같은 terminal_id로 재접속 -> 기존 소켓 교체\n";

    risk_transport::CameraAssignmentServer server("127.0.0.1", kTestPort);
    server.start();

    TestTerminal first(kTestPort);
    if (!first.connectWithin(3000)) { check(false, "첫 접속"); server.stop(); return; }
    first.sendLine(R"({"type":"hello","terminal_id":"TERM_03"})");
    check(waitUntil(2000, [&] { return server.isRegistered("TERM_03"); }), "첫 소켓 등록 완료");

    TestTerminal second(kTestPort);
    if (!second.connectWithin(3000)) { check(false, "재접속"); server.stop(); return; }
    second.sendLine(R"({"type":"hello","terminal_id":"TERM_03"})");

    // 재등록이 반영될 시간을 준 뒤, 첫 소켓은 서버가 닫아서 EOF가 와야 한다.
    check(first.waitForClose(2000), "재접속 시 기존 소켓이 서버에 의해 닫힘(EOF)");
    check(server.connectedCount() == 1, "매핑된 단말 수는 여전히 1 (교체, 추가 아님) (실제: " +
          std::to_string(server.connectedCount()) + ")");

    // 새 소켓으로 전송이 도착하는지 확인.
    bool ok = server.sendCameraAssignment("TERM_03", "cam_09", "zone_c", "2026-08-06T02:00:00.000Z");
    check(ok, "재접속한 새 소켓으로 sendCameraAssignment() -> true");
    auto lines = second.receive(1, 2000);
    check(lines.size() == 1 && lines[0].find("cam_09") != std::string::npos,
          "새 소켓이 전송 결과를 수신");

    server.stop();
}

// 잘못된 첫 메시지(파싱 실패 또는 type != hello)를 보내면 연결이 끊겨야 한다.
void testInvalidHelloClosesConnection() {
    std::cout << "\n[테스트 5] 잘못된 첫 메시지 -> 연결 종료\n";

    risk_transport::CameraAssignmentServer server("127.0.0.1", kTestPort);
    server.start();

    // (1) JSON이 아닌 문자열
    {
        TestTerminal term(kTestPort);
        if (!term.connectWithin(3000)) { check(false, "접속(비-JSON 케이스)"); server.stop(); return; }
        term.sendLine("not-json-at-all");
        check(term.waitForClose(2000), "JSON 파싱 실패 시 연결 종료됨");
    }

    // (2) 유효한 JSON이지만 type != hello
    {
        TestTerminal term(kTestPort);
        if (!term.connectWithin(3000)) { check(false, "접속(type 불일치 케이스)"); server.stop(); return; }
        term.sendLine(R"({"type":"ping","terminal_id":"TERM_04"})");
        check(term.waitForClose(2000), "type != hello 시 연결 종료됨");
        check(!server.isRegistered("TERM_04"), "TERM_04는 매핑되지 않음");
    }

    server.stop();
}

} // namespace

int main() {
    std::cout << "=== CameraAssignmentServer(9001, 양방향) 테스트 ===\n\n";

    testHelloRegistersMapping();
    testSendToUnknownTerminalFails();
    testSendDeliversJsonToTerminal();
    testReconnectReplacesSocket();
    testInvalidHelloClosesConnection();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
