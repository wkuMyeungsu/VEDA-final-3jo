// 메타데이터 카메라 시각과 서버 수신 시각의 차이 계산 테스트
#include "common/metadata_timing.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const std::string& description) {
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << description << '\n';
    if (!condition) ++failures;
}

void checkClose(double actual, double expected, const std::string& description) {
    check(std::fabs(actual - expected) < 1e-6, description);
}

}  // namespace

int main() {
    using Clock = std::chrono::system_clock;

    Clock::time_point received;
    check(forklift::common::parseUtc(
              "2026-08-10T01:04:51.602Z", received),
          "서버 수신 시각 파싱");

    const auto timing = forklift::common::makeMetadataTiming(
        "2026-08-10T01:04:51.604Z", received);
    check(timing.server_received_utc == "2026-08-10T01:04:51.602Z",
          "서버 수신 시각 포맷 보존");
    checkClose(timing.delta_ms, -2.0,
               "밀리초 소수부를 포함한 음수 지연시간 계산");

    Clock::time_point later;
    check(forklift::common::parseUtc(
              "2026-08-10T01:04:50.074Z", later),
          "두 번째 서버 수신 시각 파싱");
    const auto positiveTiming = forklift::common::makeMetadataTiming(
        "2026-08-10T01:04:50.019Z", later);
    checkClose(positiveTiming.delta_ms, 55.0,
               "밀리초 소수부를 포함한 양수 지연시간 계산");

    Clock::time_point ignored;
    check(!forklift::common::parseUtc(
              "2026-08-10T01:04:51.604oopsZ", ignored),
          "소수부 뒤의 잘못된 문자를 거부");

    std::cout << "\n=== "
              << (failures == 0 ? "ALL PASSED" : "FAILED")
              << " (실패 " << failures << "건) ===\n";
    return failures == 0 ? 0 : 1;
}
