// test_camera_assignment_server_timeout.cpp
// CameraAssignmentServer - hello 타임아웃(kHelloTimeoutSec) 실제 대기 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   hello를 보내지 않고 접속만 유지한 소켓이, kHelloTimeoutSec(+ 여유분)가 지나면
//   실제로 서버에 의해 닫히는지(EOF)를 실시간 대기로 확인한다.
//
// test_camera_assignment_server.cpp와 분리한 이유: 이 테스트 하나만으로 스위트가
// kHelloTimeoutSec(5초)만큼 느려진다. 평소 빌드/CI 루프에 얹히지 않도록 CMake에서
// EXCLUDE_FROM_ALL로 뺐다 - `cmake --build build`(기본 all)에는 안 걸리고, 명시적으로
// `--target test_camera_assignment_server_timeout`을 지정할 때만 빌드된다.
//
// 빌드: g++ -std=c++17 test_camera_assignment_server_timeout.cpp \
//           -o test_camera_assignment_server_timeout -pthread
//       또는: cmake --build build --target test_camera_assignment_server_timeout
// 실행: ./test_camera_assignment_server_timeout   (종료코드 0=성공, 1=실패, 5초+ 소요)

#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "network/camera_assignment_server.hpp"

namespace {

// 실제 운영 포트(9001)·다른 테스트 포트(19002)와 겹치지 않게 별도 포트 사용.
constexpr uint16_t kTestPort = 19003;

// hello를 보내지 않는 최소 TCP 단말. 접속만 하고 서버가 끊을 때까지 대기한다.
class SilentTerminal {
public:
    explicit SilentTerminal(uint16_t port) : port_(port) {}
    ~SilentTerminal() { close(); }

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

    // 서버가 연결을 끊었는지(EOF) 확인한다. 끊겼으면 true, timeout_ms 안에 안 끊기면 false.
    bool waitForClose(int timeout_ms) {
        if (fd_ < 0) return true;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char scratch[64];
        while (std::chrono::steady_clock::now() < deadline) {
            timeval tv{0, 100 * 1000};
            ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ssize_t n = ::recv(fd_, scratch, sizeof(scratch), 0);
            if (n == 0) return true;   // EOF - 서버가 닫음
            if (n < 0) continue;       // 타임아웃(EAGAIN) - 계속 대기
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

void testHelloTimeoutClosesSilentConnection() {
    constexpr int kMarginMs = 1500;   // kHelloTimeoutSec(5s) + 여유분 - 스케줄링 지연 흡수용
    const int kWaitMs = risk_transport::CameraAssignmentServer::kHelloTimeoutSec * 1000 + kMarginMs;

    std::cout << "[테스트] hello 없이 " << risk_transport::CameraAssignmentServer::kHelloTimeoutSec
              << "초 접속 유지 -> 서버가 연결을 닫는지 (최대 " << kWaitMs << "ms 대기)\n";

    risk_transport::CameraAssignmentServer server("127.0.0.1", kTestPort);
    server.start();

    SilentTerminal term(kTestPort);
    if (!term.connectWithin(3000)) { check(false, "단말이 서버에 접속"); server.stop(); return; }

    // hello를 보내지 않는다 - 타임아웃 경로만 확인한다.
    auto t0 = std::chrono::steady_clock::now();
    bool closed = term.waitForClose(kWaitMs);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0).count();

    check(closed, "hello 없이 대기한 소켓이 타임아웃으로 닫힘 (실제 경과: " +
          std::to_string(elapsed_ms) + "ms)");
    // 너무 일찍 끊기면(타임아웃 로직이 아니라 다른 경로로 끊긴 것) 오탐이므로 하한도 같이 본다.
    check(elapsed_ms >= risk_transport::CameraAssignmentServer::kHelloTimeoutSec * 1000,
          "kHelloTimeoutSec(" + std::to_string(risk_transport::CameraAssignmentServer::kHelloTimeoutSec)
          + "s)보다 일찍 끊기지 않음");
    check(server.connectedCount() == 0, "타임아웃된 소켓은 매핑에도 남지 않음");

    server.stop();
}

} // namespace

int main() {
    std::cout << "=== CameraAssignmentServer - hello 타임아웃 실시간 대기 테스트 (5초+) ===\n\n";

    testHelloTimeoutClosesSilentConnection();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
