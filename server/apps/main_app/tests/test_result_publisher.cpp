#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "logging/logger.hpp"
#include "network/result_publisher.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    std::cout << (condition ? "  [통과] " : "  [실패] ") << message << "\n";
    if (!condition) ++failures;
}

std::string captureOverflowLogs(std::size_t count,
                                std::size_t& dropped,
                                std::string& stopLogs) {
    std::ostringstream publishLogs;
    std::ostringstream finalLogs;
    std::streambuf* saved_out = std::cout.rdbuf(publishLogs.rdbuf());
    std::streambuf* saved_err = std::cerr.rdbuf(publishLogs.rdbuf());

    risk_transport::ResultPublisher publisher(
        "test-terminal", "127.0.0.1", 1883, {},
        risk_transport::ResultPublisherRole::RiskResult);
    for (std::size_t i = 0; i < count; ++i) {
        publisher.publish("{\"seq\":" + std::to_string(i) + "}");
    }
    dropped = publisher.droppedCount();

    std::cout.rdbuf(finalLogs.rdbuf());
    std::cerr.rdbuf(finalLogs.rdbuf());
    publisher.stop();
    stopLogs = finalLogs.str();
    std::cout.rdbuf(saved_out);
    std::cerr.rdbuf(saved_err);
    return publishLogs.str();
}

void testNoOverflowWithinCapacity() {
    std::cout << "[테스트 1] 큐 용량 이내 publish는 드랍하지 않음\n";

    std::size_t dropped = 0;
    std::string stopLogs;
    const std::string publishLogs = captureOverflowLogs(100, dropped, stopLogs);

    check(dropped == 0,
          "100건 publish 후 드랍 카운터 0 (실제: " + std::to_string(dropped) + ")");
    check(publishLogs.empty(), "용량 이내에서는 overflow 로그가 없음");
    check(stopLogs.empty(), "종료 시 잔여 드랍 요약 로그가 없음");
}

void testOverflowDropsOldestAndRateLimitsLogs() {
    std::cout << "\n[테스트 2] 큐 초과 시 오래된 결과 드랍·로그 rate limit\n";

    std::size_t dropped = 0;
    std::string stopLogs;
    const std::string publishLogs = captureOverflowLogs(350, dropped, stopLogs);

    check(dropped == 250,
          "350건 publish 후 250건 드랍 (실제: " + std::to_string(dropped) + ")");

    std::size_t publishLogLines = 0;
    for (char c : publishLogs) {
        if (c == '\n') ++publishLogLines;
    }
    check(publishLogLines == 3,
          "publish 중 로그는 첫 드랍 및 100건 단위 요약 3줄만 출력 (실제: " +
              std::to_string(publishLogLines) + ")");
    check(publishLogs.find("전송 대기열 초과") != std::string::npos,
          "첫 overflow가 전송 대기열 초과 로그로 기록됨");
    check(publishLogs.find("누적: 101") != std::string::npos &&
              publishLogs.find("누적: 201") != std::string::npos,
          "이후 드랍은 누적 101·201건 시점에 요약됨");
    check(stopLogs.find("전송 건너뜀 요약 (추가: 49건, 누적: 250)") != std::string::npos,
          "종료 시 rate limit 잔여 49건이 한 줄로 요약됨");
}

void testTopicsAreSelectedByRole() {
    std::cout << "\n[테스트 3] 위험 결과와 서버 상태 토픽은 역할로 분리됨\n";

    risk_transport::ResultPublisher risk(
        "TERM_01", "127.0.0.1", 1883, {}, risk_transport::ResultPublisherRole::RiskResult);
    risk_transport::ResultPublisher status(
        "SERVER", "127.0.0.1", 1883, {}, risk_transport::ResultPublisherRole::ServerStatus);

    check(risk.topic() == "forklift/risk/TERM_01",
          "단말 publisher는 자신의 risk 토픽을 사용함");
    check(status.topic() == "forklift/status/server",
          "서버 상태 publisher는 단말 목록의 첫 항목과 무관하게 status 토픽을 사용함");
}

void testLoggerFormatAndDebugGate() {
    std::cout << "\n[공통 Logger 포맷 및 DEBUG 기본 정책]\n";
    auto& logger = forklift::logging::Logger::instance();
    logger.setDebugEnabled(false);

    std::ostringstream normalLogs;
    std::streambuf* saved_out = std::cout.rdbuf(normalLogs.rdbuf());
    std::streambuf* saved_err = std::cerr.rdbuf(normalLogs.rdbuf());
    LOG_DEBUG("TEST", "표시되면 안 됨");
    LOG_INFO("TEST", "운영 로그");
    std::cout.rdbuf(saved_out);
    std::cerr.rdbuf(saved_err);

    check(normalLogs.str().find("표시되면 안 됨") == std::string::npos,
          "일반 운영 기본 모드에서 DEBUG 로그는 출력하지 않음");
    check(normalLogs.str().find("] [INFO] [TEST] 운영 로그") != std::string::npos,
          "로그가 [시간] [LEVEL] [TAG] 포맷으로 출력됨");

    logger.setDebugEnabled(true);
    std::ostringstream debugLogs;
    saved_out = std::cout.rdbuf(debugLogs.rdbuf());
    saved_err = std::cerr.rdbuf(debugLogs.rdbuf());
    LOG_DEBUG("TEST", "진단 로그");
    std::cout.rdbuf(saved_out);
    std::cerr.rdbuf(saved_err);
    logger.setDebugEnabled(false);

    check(debugLogs.str().find("] [DEBUG] [TEST] 진단 로그") != std::string::npos,
          "진단 모드에서 DEBUG 로그를 활성화할 수 있음");
}

void testLoggerFlushesStartupBuffer() {
    std::cout << "\n[공통 Logger 기동 전 버퍼 정책]\n";
    const auto path = std::filesystem::temp_directory_path() / "forklift_logger_startup_test.log";
    std::error_code ec;
    std::filesystem::remove(path, ec);

    auto& logger = forklift::logging::Logger::instance();
    logger.setLogFile("");
    logger.setDebugEnabled(false);
    LOG_INFO("TEST", "파일 연결 전 기동 로그");

    check(logger.setLogFile(path.string()), "로그 파일 연결 성공");
    std::ifstream file(path);
    const std::string contents((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    check(contents.find("파일 연결 전 기동 로그") != std::string::npos,
          "파일 경로 확정 전 로그가 server.log로 flush됨");

    logger.setLogFile("");
    std::filesystem::remove(path, ec);
}

}  // namespace

int main() {
    std::cout << "=== ResultPublisher 큐/드랍 정책 테스트 ===\n";
    testNoOverflowWithinCapacity();
    testOverflowDropsOldestAndRateLimitsLogs();
    testTopicsAreSelectedByRole();
    testLoggerFormatAndDebugGate();
    testLoggerFlushesStartupBuffer();
    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
