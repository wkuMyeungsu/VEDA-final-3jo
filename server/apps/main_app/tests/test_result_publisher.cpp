#include <chrono>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "logging/localization_log_gate.hpp"
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
    check(normalLogs.str().find("[run_id=") == std::string::npos,
          "일반 운영 로그 매 줄에 run_id를 붙이지 않음");
    check(!logger.runId().empty(), "프로세스 run_id는 기동 식별용으로 생성됨");

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

void testAnnounceReadyComesBeforeHeldLogs() {
    std::cout << "\n[기동 배너 우선 출력]\n";
    auto& logger = forklift::logging::Logger::instance();
    logger.holdUntilReady();

    std::ostringstream captured;
    std::streambuf* saved_out = std::cout.rdbuf(captured.rdbuf());
    std::streambuf* saved_err = std::cerr.rdbuf(captured.rdbuf());
    LOG_WARN("CONFIG", "채널 제외");
    logger.announceReady("서버 기동 완료");
    LOG_INFO("CCTV", "연결 성공");
    std::cout.rdbuf(saved_out);
    std::cerr.rdbuf(saved_err);

    const std::string& text = captured.str();
    const auto ready = text.find("서버 기동 완료");
    const auto config = text.find("채널 제외");
    const auto cctv = text.find("연결 성공");
    check(ready != std::string::npos && config != std::string::npos &&
              cctv != std::string::npos,
          "기동 배너와 전후 로그가 모두 출력됨");
    check(ready < config, "기동 완료가 붙잡아 둔 CONFIG 로그보다 앞섬");
    check(config < cctv, "붙잡아 둔 CONFIG 로그가 이후 CCTV 로그보다 앞섬");
}

void testLocalizationLogGateSuppressesFlicker() {
    std::cout << "\n[마커 로그 쿨다운]\n";
    forklift::logging::LocalizationLogGate gate;
    const auto t0 = std::chrono::steady_clock::now();

    check(gate.shouldLog("LOCALIZED", t0), "첫 위치 상태는 즉시 남김");
    check(!gate.shouldLog("LOCALIZED", t0 + std::chrono::seconds(1)),
          "같은 상태가 유지되면 다시 남기지 않음");
    check(!gate.shouldLog("MARKER_NOT_DETECTED", t0 + std::chrono::seconds(1)),
          "바뀐 직후에는 미검출을 남기지 않음");
    check(!gate.shouldLog("LOCALIZED", t0 + std::chrono::seconds(2)),
          "쿨다운 안 왕복은 한 줄도 남기지 않음");
    check(!gate.shouldLog("MARKER_NOT_DETECTED", t0 + std::chrono::seconds(2)),
          "다시 미검출로 바뀌어도 쿨다운 전에는 남기지 않음");
    check(gate.shouldLog("MARKER_NOT_DETECTED", t0 + std::chrono::seconds(5)),
          "3초 이상 유지된 미검출만 남김");
}

}  // namespace

int main() {
    std::cout << "=== ResultPublisher 큐/드랍 정책 테스트 ===\n";
    testNoOverflowWithinCapacity();
    testOverflowDropsOldestAndRateLimitsLogs();
    testTopicsAreSelectedByRole();
    testLoggerFormatAndDebugGate();
    testAnnounceReadyComesBeforeHeldLogs();
    testLocalizationLogGateSuppressesFlicker();
    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
