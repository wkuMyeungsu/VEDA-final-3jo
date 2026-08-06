// test_result_publisher.cpp
// ResultPublisher(서버 송신기) + ResultDispatcher(전송 정책) 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   [테스트 1] 큐 용량 이내(20건)면 드랍 없이 순서대로 전부 전송되는지
//   [테스트 2] 큐 용량 초과(150건)면 "가장 오래된 것부터" 버려지고
//              최신 100건이 순서대로 남는지 + 드랍 카운터가 정확한지
//   [테스트 3] 드랍 로그 rate limit (첫 1건 + 100건마다 요약)
//   [테스트 4] 종료 시 잔여 드랍 요약 (stop() / 소멸자 경로)
//   [테스트 5] 단말 미접속 상태에서도 죽지 않고 대기하다가, 접속 시 밀린 결과를 전송하는지
//   [테스트 6] 단말이 끊고 나가면 재대기(re-listen)로 돌아가 다음 단말을 받는지
//   [테스트 7] 전송 정책: 상태 변화 시에만 즉시 전송, 무변화 submit은 전송하지 않음
//   [테스트 8] 전송 정책: 무변화 구간의 주기 재전송(하트비트) + 변화 시 타이머 리셋
//   [테스트 9] 정책 -> 소켓 end-to-end (하트비트가 실제로 단말까지 도착하는지)
//
// [구조 변경 2026-08-03] ResultPublisher가 client(connect)에서 server(listen/accept)로
//   바뀌었기 때문에, 이 테스트의 수신부도 "서버 역할"에서 "단말 역할(client)"로 뒤집혔다.
//   -> 예전 LineReceiver(bind/listen)는 LineClient(connect)로 교체됐다.
//
// 테스트 1~4는 publisher.start() 전에 publish()를 몰아친 뒤 start()하는 방식이라
// 워커 스레드 타이밍에 의존하지 않고 결정적으로 동작한다.
// 테스트 8~9는 시간 기반이라 여유 있는 허용 범위로 검사한다.
//
// 빌드: g++ -std=c++17 test_result_publisher.cpp danger_judgment_engine.cpp \
//           -o test_result_publisher -pthread
//       (ResultDispatcher가 toJson()을 쓰므로 엔진 구현이 함께 링크돼야 한다)
// 실행: ./test_result_publisher   (종료코드 0=성공, 1=실패)

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "network/result_publisher.hpp"
#include "network/result_dispatcher.hpp"

namespace {

constexpr uint16_t kTestPort = 19001;  // 실제 운영 포트(9000)와 겹치지 않게 별도 포트 사용

// 테스트용 최소 TCP 단말.
// ResultPublisher가 서버가 되었으므로 이쪽이 접속하는 쪽이다.
class LineClient {
public:
    explicit LineClient(uint16_t port) : port_(port) {}
    ~LineClient() { close(); }

    // 퍼블리셔의 listen 소켓이 열릴 때까지 재시도하며 접속한다
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

    // expected_lines 만큼 모이거나 timeout_ms가 지날 때까지 수신한다.
    std::vector<std::string> receive(std::size_t expected_lines, int timeout_ms) {
        std::vector<std::string> lines;
        if (fd_ < 0) return lines;

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char chunk[4096];
        while (lines.size() < expected_lines && std::chrono::steady_clock::now() < deadline) {
            timeval tv{0, 50 * 1000};  // 50ms 슬라이스 (deadline 초과를 최소화)
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

std::string makeMessage(int i) {
    return "{\"seq\":" + std::to_string(i) + "}";
}

// 퍼블리셔가 특정 링크 상태가 될 때까지 기다린다 (재대기 동작 확인용).
bool waitForState(const risk_transport::ResultPublisher& p, risk_transport::LinkState want,
                  int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (p.state() == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return p.state() == want;
}

// 정책 테스트용 판정 결과. 상태 비교 대상(final_risk/exception)과 무관한 필드는 고정한다.
JudgmentResult makeResult(RiskLevel risk, ExceptionState exc = ExceptionState::NONE,
                          double distance_m = 2.0) {
    JudgmentResult r{};
    r.camera_risk = risk;
    r.tof_risk    = risk;
    r.final_risk  = risk;
    r.exception   = exc;
    r.distance_m  = distance_m;
    return r;
}

// 전송된 JSON을 모아두는 테스트용 sink (소켓 없이 정책만 검증할 때 사용).
class RecordingSink {
public:
    void operator()(const std::string& json) {
        std::lock_guard<std::mutex> lk(mtx_);
        lines_.push_back(json);
    }
    std::size_t size() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return lines_.size();
    }
    std::vector<std::string> snapshot() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return lines_;
    }
private:
    mutable std::mutex mtx_;
    std::vector<std::string> lines_;
};

// 큐 용량 이내: 드랍 없이 순서대로 전부 도착해야 한다.
void testNoDropWithinCapacity() {
    std::cout << "[테스트 1] 20건 연속 publish - 드랍 없이 순서대로 전송\n";

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);

    // start() 전에 몰아넣어서 워커 타이밍과 무관하게 큐에 20건이 쌓이도록 한다.
    const int kCount = 20;
    for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));

    publisher.start();

    LineClient client(kTestPort);
    if (!client.connectWithin(3000)) { check(false, "단말이 퍼블리셔에 접속"); return; }

    auto lines = client.receive(kCount, 5000);
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

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);

    const int kCount = 150;
    const int kCapacity = 100;
    for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));

    check(publisher.droppedCount() == static_cast<std::size_t>(kCount - kCapacity),
          "드랍 카운터 " + std::to_string(kCount - kCapacity) +
          " (실제: " + std::to_string(publisher.droppedCount()) + ")");

    publisher.start();

    LineClient client(kTestPort);
    if (!client.connectWithin(3000)) { check(false, "단말이 퍼블리셔에 접속"); return; }

    auto lines = client.receive(kCapacity, 5000);
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

// 종료 시 잔여 드랍 요약: rate limit 때문에 아직 안 찍힌 분량이 stop()/소멸자에서
// 1줄로 정리되는지, 그리고 stop() 후 소멸자가 또 불려도 중복 출력되지 않는지 확인.
void testResidualDropSummaryOnStop() {
    std::cout << "\n[테스트 4] 종료 시 잔여 드랍 요약 (stop() / 소멸자 경로)\n";

    // 큐 용량 100 + 드랍 50건 -> 첫 1건만 로그, 잔여 49건은 로그에 안 남은 상태
    const int kCount = 150;

    // (1) 명시적 stop() 경로 + stop() 뒤 소멸자 중복 호출
    std::ostringstream captured;
    std::streambuf* saved = std::cerr.rdbuf(captured.rdbuf());
    std::size_t lines_after_stop = 0;
    {
        risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);
        for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));
        publisher.stop();   // start() 없이 publish만 한 경우에도 잔여 요약이 나와야 함
        for (char c : captured.str()) if (c == '\n') ++lines_after_stop;
    }   // 여기서 소멸자 -> stop() 재호출: 잔여분이 없으므로 추가 출력이 없어야 함
    std::cerr.rdbuf(saved);

    std::size_t lines_after_dtor = 0;
    for (char c : captured.str()) if (c == '\n') ++lines_after_dtor;

    check(lines_after_stop == 2,
          "stop() 시점에 2줄 (첫 드랍 + 잔여 요약) (실제: " +
          std::to_string(lines_after_stop) + "줄)");
    check(captured.str().find("dropped 49 more (total: 50)") != std::string::npos,
          "잔여 49건이 요약 1줄로 출력됨");
    check(lines_after_dtor == lines_after_stop,
          "소멸자에서 stop()이 또 불려도 중복 출력 없음 (실제: " +
          std::to_string(lines_after_dtor) + "줄)");

    // (2) 명시적 stop() 없이 소멸자만으로 잔여 요약이 나오는지
    std::ostringstream captured2;
    saved = std::cerr.rdbuf(captured2.rdbuf());
    {
        risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);
        for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));
    }   // stop() 호출 없이 소멸자만
    std::cerr.rdbuf(saved);

    check(captured2.str().find("dropped 49 more (total: 50)") != std::string::npos,
          "소멸자 경로에서도 잔여 요약 출력됨");

    std::cout << "  --- 실제 stderr 출력 (소멸자 경로) ---\n";
    std::istringstream lines(captured2.str());
    std::string line;
    while (std::getline(lines, line)) std::cout << "  | " << line << "\n";
}

// 단말이 아직 안 붙은 구간: 서버는 LISTENING 상태로 살아 있어야 하고,
// 그 사이 publish된 결과는 큐에 남아 있다가 접속 시점에 전송돼야 한다.
void testSurvivesWithoutTerminal() {
    std::cout << "\n[테스트 5] 단말 미접속 - 죽지 않고 대기, 접속 후 밀린 결과 전송\n";

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);
    publisher.start();

    check(waitForState(publisher, risk_transport::LinkState::LISTENING, 2000),
          "start() 후 LISTENING 상태로 대기");

    // 단말 없이 publish - 여기서 죽거나 멈추면 안 된다.
    const int kCount = 5;
    for (int i = 1; i <= kCount; ++i) publisher.publish(makeMessage(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    check(publisher.state() == risk_transport::LinkState::LISTENING,
          "단말 없이 publish해도 계속 LISTENING (전송 실패 카운터: " +
          std::to_string(publisher.sendFailureCount()) + ")");

    LineClient client(kTestPort);
    if (!client.connectWithin(3000)) { check(false, "단말이 뒤늦게 접속"); publisher.stop(); return; }

    auto lines = client.receive(kCount, 3000);
    check(lines.size() == static_cast<std::size_t>(kCount),
          "접속 후 밀린 " + std::to_string(kCount) + "건 수신 (실제: " +
          std::to_string(lines.size()) + ")");

    publisher.stop();
}

// 단말이 끊고 나가도 서버는 죽지 않고 재대기로 돌아가 다음 단말을 받아야 한다.
// (단말 IP가 바뀌며 재접속하는 실제 운용 시나리오)
void testRelistenAfterDisconnect() {
    std::cout << "\n[테스트 6] 단말 재접속 - 연결 종료 후 re-listen\n";

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);
    publisher.start();

    // ── 첫 번째 단말 ─────────────────────────────
    {
        LineClient first(kTestPort);
        if (!first.connectWithin(3000)) { check(false, "첫 단말 접속"); publisher.stop(); return; }
        publisher.publish(makeMessage(1));
        auto lines = first.receive(1, 3000);
        check(lines.size() == 1 && lines[0] == makeMessage(1), "첫 단말이 1건 수신");
    }   // 소멸자에서 close() -> 단말이 끊고 나감

    // 보낼 게 없는 구간에서도 연결 종료를 감지해 재대기로 돌아가야 한다.
    check(waitForState(publisher, risk_transport::LinkState::LISTENING, 3000),
          "연결 종료 감지 후 LISTENING으로 복귀 (실제 상태: " +
          std::to_string(static_cast<int>(publisher.state())) + ")");

    // ── 두 번째 단말 (재접속) ────────────────────
    LineClient second(kTestPort);
    check(second.connectWithin(3000), "두 번째 단말이 같은 포트로 접속");

    publisher.publish(makeMessage(2));
    auto lines = second.receive(1, 3000);
    check(lines.size() == 1 && lines[0] == makeMessage(2), "재접속한 단말이 이후 결과를 수신");
    check(publisher.acceptedCount() == 2,
          "누적 accept 2회 (실제: " + std::to_string(publisher.acceptedCount()) + ")");

    publisher.stop();
}

// 전송 정책 - 변화 감지 부분. 하트비트 스레드를 띄우지 않아 결정적으로 동작한다.
void testDispatcherSendsOnlyOnChange() {
    std::cout << "\n[테스트 7] 전송 정책 - 상태 변화 시에만 즉시 전송\n";

    RecordingSink sink;
    risk_transport::ResultDispatcher dispatcher(
        [&sink](const std::string& json) { sink(json); }, std::chrono::milliseconds(200));
    // start() 하지 않음 -> 하트비트 없이 submit() 경로만 검증

    dispatcher.submit(makeResult(RiskLevel::SAFE));                      // 첫 결과 -> 전송
    dispatcher.submit(makeResult(RiskLevel::SAFE, ExceptionState::NONE, 2.5));  // 거리만 변함 -> 전송 안 함
    dispatcher.submit(makeResult(RiskLevel::CAUTION));                   // 위험도 변화 -> 전송
    dispatcher.submit(makeResult(RiskLevel::CAUTION));                   // 동일 -> 전송 안 함
    dispatcher.submit(makeResult(RiskLevel::CAUTION, ExceptionState::SENSOR_FAULT));  // 예외 변화 -> 전송

    check(sink.size() == 3,
          "3회만 전송 (첫 결과 + 위험도 변화 + 예외 변화) (실제: " +
          std::to_string(sink.size()) + ")");
    check(dispatcher.changeSendCount() == 3,
          "변화 전송 카운터 3 (실제: " + std::to_string(dispatcher.changeSendCount()) + ")");
    check(dispatcher.heartbeatSendCount() == 0,
          "하트비트 미기동이므로 0 (실제: " + std::to_string(dispatcher.heartbeatSendCount()) + ")");

    auto lines = sink.snapshot();
    check(lines.size() == 3 && lines[0].find("\"risk_level\":0") != std::string::npos
                            && lines[1].find("\"risk_level\":1") != std::string::npos,
          "전송된 JSON의 risk_level이 판정값과 일치");
    // 거리만 바뀐 submit도 last_에 반영돼야 다음 하트비트가 최신 값을 싣는다.
    check(lines.size() == 3 && lines[2].find("SENSOR_FAULT") != std::string::npos,
          "예외 상태가 JSON에 반영됨");
}

// 전송 정책 - 하트비트 + 타이머 리셋. 시간 기반이라 여유 있는 범위로 검사한다.
void testDispatcherHeartbeatAndReset() {
    std::cout << "\n[테스트 8] 전송 정책 - 무변화 시 주기 재전송 + 변화 시 타이머 리셋\n";

    // (1) 무변화 구간에서 주기적으로 재전송되는지 (period 100ms, 350ms 관찰 -> 2~4회)
    {
        RecordingSink sink;
        risk_transport::ResultDispatcher dispatcher(
            [&sink](const std::string& json) { sink(json); }, std::chrono::milliseconds(100));
        dispatcher.start();
        dispatcher.submit(makeResult(RiskLevel::SAFE));
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        dispatcher.stop();

        std::size_t hb = dispatcher.heartbeatSendCount();
        check(hb >= 2 && hb <= 4,
              "100ms 주기 x 350ms -> 하트비트 2~4회 (실제: " + std::to_string(hb) + "회)");
        check(dispatcher.changeSendCount() == 1,
              "즉시 전송은 첫 결과 1회뿐 (실제: " +
              std::to_string(dispatcher.changeSendCount()) + "회)");
        check(sink.size() == 1 + hb, "sink 총 전송 건수 = 즉시 1 + 하트비트 " + std::to_string(hb));
    }

    // (2) 변화가 생기면 즉시 나가고, 하트비트 타이머가 그 시점으로 리셋되는지.
    //     period 200ms 기준으로 t=150 부근에 상태를 바꾸고, 그로부터 120ms 뒤에 관찰한다.
    //     리셋이 동작하면 다음 하트비트는 변화 시점+200ms이므로 그 사이에 아무것도 안 나가야 한다.
    //     (sleep이 밀려도 오탐이 나지 않도록 절대 횟수가 아니라 "변화 직전 대비 증가 없음"으로 본다.)
    {
        RecordingSink sink;
        risk_transport::ResultDispatcher dispatcher(
            [&sink](const std::string& json) { sink(json); }, std::chrono::milliseconds(200));
        dispatcher.start();

        dispatcher.submit(makeResult(RiskLevel::SAFE));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        const std::size_t hb_before    = dispatcher.heartbeatSendCount();
        const std::size_t sends_before = sink.size();

        auto t0 = std::chrono::steady_clock::now();
        dispatcher.submit(makeResult(RiskLevel::DANGER));
        auto submit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();

        check(submit_ms < 20,
              "상태 변화 submit()이 판정 루프를 막지 않음 (" + std::to_string(submit_ms) + "ms)");
        check(sink.size() == sends_before + 1,
              "변화 시 즉시 1건 전송 (실제 증가: " +
              std::to_string(sink.size() - sends_before) + "건)");

        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        std::size_t hb_after = dispatcher.heartbeatSendCount();
        dispatcher.stop();

        check(hb_after == hb_before,
              "변화 시점 기준으로 타이머 리셋 - 이후 120ms 동안 하트비트 없음 (증가: " +
              std::to_string(hb_after - hb_before) + "회)");
    }
}

// 정책 -> 소켓 end-to-end: 하트비트가 실제로 단말까지 도착하는지.
void testHybridEndToEnd() {
    std::cout << "\n[테스트 9] end-to-end - 변화 즉시 전송 + 하트비트가 단말까지 도착\n";

    risk_transport::ResultPublisher publisher("127.0.0.1", kTestPort);
    publisher.start();

    LineClient client(kTestPort);
    if (!client.connectWithin(3000)) { check(false, "단말 접속"); publisher.stop(); return; }

    risk_transport::ResultDispatcher dispatcher(
        [&publisher](const std::string& json) { publisher.publish(json); },
        std::chrono::milliseconds(100));
    dispatcher.start();

    dispatcher.submit(makeResult(RiskLevel::DANGER, ExceptionState::NONE, 1.2));
    auto lines = client.receive(4, 450);   // 즉시 1건 + 100ms 하트비트 3건 정도

    dispatcher.stop();
    publisher.stop();

    check(lines.size() >= 3,
          "무변화 구간에서도 3건 이상 도착 (실제: " + std::to_string(lines.size()) + "건)");

    bool all_danger = !lines.empty();
    for (const auto& l : lines) {
        if (l.find("\"risk_level\":2") == std::string::npos) all_danger = false;
    }
    check(all_danger, "재전송된 줄도 마지막 판정 결과(risk_level=2)를 그대로 담고 있음");
    check(lines.size() >= 2 && lines.front() != lines.back(),
          "재전송마다 utc_time이 갱신됨 (단말이 링크 생존을 판단할 수 있음)");

    if (!lines.empty()) {
        std::cout << "  --- 수신 예시 ---\n";
        std::cout << "  | " << lines.front() << "\n";
        if (lines.size() > 1) std::cout << "  | " << lines.back() << "\n";
    }
}

} // namespace

int main() {
    std::cout << "=== ResultPublisher(서버) + ResultDispatcher(전송 정책) 테스트 ===\n\n";

    testNoDropWithinCapacity();
    testDropsOldestOnOverflow();
    testDropLogRateLimit();
    testResidualDropSummaryOnStop();
    testSurvivesWithoutTerminal();
    testRelistenAfterDisconnect();
    testDispatcherSendsOnlyOnChange();
    testDispatcherHeartbeatAndReset();
    testHybridEndToEnd();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
