// test_result_publisher.cpp
// ResultPublisher 송신 큐 스트레스 테스트
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   기존 ResultPublisher는 pending_ 단일 슬롯(last-write-wins) 구조라, 워커 스레드가
//   소비하기 전에 publish()가 여러 번 불리면 중간 값이 통째로 유실됐다
//   (실측: 9회 publish 중 3건만 전송). 큐 기반으로 교체한 뒤 아래 2가지를 검증한다.
//
//   [테스트 1] 큐 용량 이내(20건)면 드랍 없이 순서대로 전부 전송되는지
//   [테스트 2] 큐 용량 초과(150건)면 "가장 오래된 것부터" 버려지고
//              최신 100건이 순서대로 남는지 + 드랍 카운터가 정확한지
//
// 두 테스트 모두 publisher.start() 전에 publish()를 몰아친 뒤 start()하는 방식이라
// 워커 스레드 타이밍에 의존하지 않고 결정적으로 동작한다.
//
// 빌드: g++ -std=c++17 test_result_publisher.cpp -o test_result_publisher -pthread
// 실행: ./test_result_publisher   (종료코드 0=성공, 1=실패)

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "ResultPublisher.h"

namespace {

constexpr uint16_t kTestPort = 19001;  // 실제 운영 포트(9000)와 겹치지 않게 별도 포트 사용

// 테스트용 최소 TCP 수신 서버.
// 지정한 개수만큼 개행 구분 줄을 받거나 타임아웃되면 반환한다.
class LineReceiver {
public:
    explicit LineReceiver(uint16_t port) : port_(port) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "[test] bind 실패 (포트 " << port_ << " 사용 중?)\n";
            ::close(listen_fd_);
            listen_fd_ = -1;
            return;
        }
        ::listen(listen_fd_, 1);
    }

    ~LineReceiver() {
        if (conn_fd_ >= 0) ::close(conn_fd_);
        if (listen_fd_ >= 0) ::close(listen_fd_);
    }

    bool valid() const { return listen_fd_ >= 0; }

    // expected_lines 만큼 모이거나 timeout_ms가 지날 때까지 수신한다.
    std::vector<std::string> receive(std::size_t expected_lines, int timeout_ms) {
        std::vector<std::string> lines;
        if (listen_fd_ < 0) return lines;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

        if (conn_fd_ < 0) {
            timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
            ::setsockopt(listen_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            conn_fd_ = ::accept(listen_fd_, nullptr, nullptr);
            if (conn_fd_ < 0) return lines;
        }

        std::string buf;
        char chunk[4096];
        while (lines.size() < expected_lines && std::chrono::steady_clock::now() < deadline) {
            timeval tv{0, 200 * 1000};  // 200ms
            ::setsockopt(conn_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            ssize_t n = ::recv(conn_fd_, chunk, sizeof(chunk), 0);
            if (n > 0) {
                buf.append(chunk, static_cast<std::size_t>(n));
                std::size_t pos;
                while ((pos = buf.find('\n')) != std::string::npos) {
                    lines.push_back(buf.substr(0, pos));
                    buf.erase(0, pos + 1);
                }
            } else if (n == 0) {
                break;  // 연결 종료
            }
        }
        return lines;
    }

private:
    uint16_t port_;
    int listen_fd_ = -1;
    int conn_fd_ = -1;
};

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

std::string makeMessage(int i) {
    return "{\"seq\":" + std::to_string(i) + "}";
}

// 큐 용량 이내: 드랍 없이 순서대로 전부 도착해야 한다.
void testNoDropWithinCapacity() {
    std::cout << "[테스트 1] 20건 연속 publish - 드랍 없이 순서대로 전송\n";

    LineReceiver receiver(kTestPort);
    if (!receiver.valid()) { check(false, "수신 서버 준비"); return; }

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);

    // start() 전에 몰아넣어서 워커 타이밍과 무관하게 큐에 20건이 쌓이도록 한다.
    const int kCount = 20;
    for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));

    publisher.start();
    auto lines = receiver.receive(kCount, 5000);
    publisher.stop();

    check(publisher.droppedCount() == 0,
          "드랍 카운터 0 (실제: " + std::to_string(publisher.droppedCount()) + ")");
    check(lines.size() == static_cast<std::size_t>(kCount),
          "수신 건수 " + std::to_string(kCount) + " (실제: " + std::to_string(lines.size()) + ")");

    bool ordered = (lines.size() == static_cast<std::size_t>(kCount));
    for (std::size_t i = 0; ordered && i < lines.size(); ++i) {
        if (lines[i] != makeMessage(static_cast<int>(i) + 1)) ordered = false;
    }
    check(ordered, "1..20 순서 그대로 도착");
}

// 큐 용량 초과: 가장 오래된 것부터 버려지고 최신 100건만 순서대로 남아야 한다.
void testDropsOldestOnOverflow() {
    std::cout << "\n[테스트 2] 150건 연속 publish - 오래된 것부터 드랍, 최신 100건 유지\n";

    LineReceiver receiver(kTestPort);
    if (!receiver.valid()) { check(false, "수신 서버 준비"); return; }

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);

    const int kCount = 150;
    const int kCapacity = 100;
    for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));

    check(publisher.droppedCount() == static_cast<std::size_t>(kCount - kCapacity),
          "드랍 카운터 " + std::to_string(kCount - kCapacity) +
          " (실제: " + std::to_string(publisher.droppedCount()) + ")");

    publisher.start();
    auto lines = receiver.receive(kCapacity, 5000);
    publisher.stop();

    check(lines.size() == static_cast<std::size_t>(kCapacity),
          "수신 건수 " + std::to_string(kCapacity) + " (실제: " + std::to_string(lines.size()) + ")");
    check(!lines.empty() && lines.front() == makeMessage(kCount - kCapacity + 1),
          "가장 오래된 50건이 버려지고 51번부터 도착 (실제 첫 건: " +
          (lines.empty() ? std::string("없음") : lines.front()) + ")");
    check(!lines.empty() && lines.back() == makeMessage(kCount),
          "마지막 건은 150번");
}

// 드랍 로그 rate limit: 카운터는 매번 정확히 증가하되 stderr는 첫 1건 + 100건마다 요약.
// publish()만 호출하므로(워커 미기동) 로그 줄 수가 결정적이다.
void testDropLogRateLimit() {
    std::cout << "\n[테스트 3] 드랍 로그 rate limit - 첫 1건 + 100건마다 요약\n";

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);

    // 큐 용량 100 + 드랍 250건 = 350건 publish
    const int kCount = 350;
    const std::size_t kExpectedDrops = 250;

    std::ostringstream captured;
    std::streambuf* saved = std::cerr.rdbuf(captured.rdbuf());
    for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));
    std::cerr.rdbuf(saved);

    std::size_t log_lines = 0;
    for (char c : captured.str()) if (c == '\n') ++log_lines;

    check(publisher.droppedCount() == kExpectedDrops,
          "드랍 카운터는 매번 증가해 " + std::to_string(kExpectedDrops) +
          " (실제: " + std::to_string(publisher.droppedCount()) + ")");
    // 로그 시점: 1번째, 101번째, 201번째 -> 3줄
    check(log_lines == 3,
          "stderr 3줄만 출력 (실제: " + std::to_string(log_lines) + "줄, rate limit 없으면 " +
          std::to_string(kExpectedDrops) + "줄)");

    std::cout << "  --- 실제 stderr 출력 ---\n";
    std::istringstream lines(captured.str());
    std::string line;
    while (std::getline(lines, line)) std::cout << "  | " << line << "\n";
}

} // namespace

int main() {
    std::cout << "=== ResultPublisher 큐 스트레스 테스트 ===\n\n";

    testNoDropWithinCapacity();
    testDropsOldestOnOverflow();
    testDropLogRateLimit();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
