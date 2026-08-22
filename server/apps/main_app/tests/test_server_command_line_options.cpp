#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/server_command_line_options.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    std::cout << (condition ? "  [OK]   " : "  [FAIL] ") << message << '\n';
    if (!condition) ++failures;
}

forklift::runtime::ServerCommandLineParseResult parse(
    std::initializer_list<const char*> arguments) {
    std::vector<std::string> storage;
    storage.reserve(arguments.size());
    for (const auto* argument : arguments) storage.emplace_back(argument);

    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& argument : storage) argv.push_back(argument.data());
    return forklift::runtime::parseServerCommandLine(
        static_cast<int>(argv.size()), argv.data(), "/default/config");
}

void testDefaults() {
    std::cout << "[기본 옵션]\n";
    const auto result = parse({"forklift_safety_server"});
    check(result.ok(), "기본 인자 파싱 성공");
    check(result.options.config_dir == "/default/config", "기본 설정 경로 보존");
    check(result.options.sensor_mode == forklift::runtime::SensorMode::Network,
          "센서 모드 기본값은 Network");
    check(!result.options.enable_debug_csv, "디버그 CSV 기본값은 비활성");
}

void testOverrides() {
    std::cout << "\n[명시적 옵션]\n";
    const auto result = parse({"forklift_safety_server", "--debug", "--no-sensor",
                               "--config-dir", "/custom/safety",
                               "--common-config-dir", "/custom/common"});
    check(result.ok(), "명시적 옵션 파싱 성공");
    check(result.options.enable_debug_csv, "--debug가 디버그 CSV를 활성화");
    check(result.options.sensor_mode == forklift::runtime::SensorMode::Disabled,
          "--no-sensor가 센서 모드를 Disabled로 전환");
    check(result.options.config_dir == "/custom/safety" &&
              result.options.common_config_dir == "/custom/common",
          "설정 경로 옵션을 각각 보존");
}

void testErrorsAndHelp() {
    std::cout << "\n[오류·도움말 옵션]\n";
    const auto help = parse({"forklift_safety_server", "--help"});
    check(help.ok() && help.options.show_help, "--help가 도움말 상태를 반환");

    const auto missing = parse({"forklift_safety_server", "--config-dir"});
    check(!missing.ok() && missing.error.find("경로") != std::string::npos,
          "경로 누락 옵션을 오류로 반환");

    const auto unknown = parse({"forklift_safety_server", "--unknown"});
    check(!unknown.ok() && unknown.error.find("알 수 없는 옵션") != std::string::npos,
          "알 수 없는 옵션을 오류로 반환");
}

}  // namespace

int main() {
    testDefaults();
    testOverrides();
    testErrorsAndHelp();
    return failures == 0 ? 0 : 1;
}
