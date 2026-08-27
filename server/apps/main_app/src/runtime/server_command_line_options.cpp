#include "runtime/server_command_line_options.hpp"

#include <sstream>
#include <utility>

namespace forklift::runtime {
namespace {

constexpr const char* kConfigDirOption = "--config-dir";
constexpr const char* kCommonConfigDirOption = "--common-config-dir";
constexpr const char* kLogCsvOption = "--log-csv";
constexpr const char* kNoSensorOption = "--no-sensor";
constexpr const char* kHelpOption = "--help";
constexpr const char* kShortHelpOption = "-h";

bool takeOptionValue(int& index, int argc, char* argv[], std::string& value,
                     const char* option, std::string& error) {
    if (index + 1 >= argc) {
        error = std::string(option) + " 옵션에는 경로가 필요합니다.";
        return false;
    }
    value = argv[++index];
    return true;
}

}  // namespace

ServerCommandLineParseResult parseServerCommandLine(
    int argc, char* argv[], std::string default_config_dir) {
    ServerCommandLineParseResult result;
    result.options.config_dir = std::move(default_config_dir);

    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index] == nullptr ? std::string() : argv[index];
        if (option == kConfigDirOption) {
            if (!takeOptionValue(index, argc, argv, result.options.config_dir,
                                 kConfigDirOption, result.error)) return result;
        } else if (option == kCommonConfigDirOption) {
            if (!takeOptionValue(index, argc, argv, result.options.common_config_dir,
                                 kCommonConfigDirOption, result.error)) return result;
        } else if (option == kLogCsvOption) {
            std::string kind;
            if (!takeOptionValue(index, argc, argv, kind, kLogCsvOption, result.error))
                return result;
            if (kind == "object") {
                result.options.enable_object_csv = true;
            } else if (kind == "aruco") {
                result.options.enable_aruco_csv = true;
            } else if (kind == "latency") {
                result.options.enable_latency_csv = true;
            } else if (kind == "all") {
                result.options.enable_object_csv = true;
                result.options.enable_aruco_csv = true;
                result.options.enable_latency_csv = true;
            } else {
                result.error = std::string(kLogCsvOption) +
                               " 값은 object, aruco, latency, all 중 하나여야 합니다.";
                return result;
            }
        } else if (option == kNoSensorOption) {
            result.options.sensor_mode = SensorMode::Disabled;
        } else if (option == kHelpOption || option == kShortHelpOption) {
            result.options.show_help = true;
            return result;
        } else {
            result.error = "알 수 없는 옵션: " + option;
            return result;
        }
    }
    return result;
}

std::string serverCommandLineUsage(const char* program_name) {
    std::ostringstream usage;
    usage << "사용법: " << (program_name == nullptr ? "forklift_safety_server" : program_name)
          << " [옵션]\n\n"
          << "옵션:\n"
          << "  " << kConfigDirOption << " PATH          안전 설정 디렉터리 경로 (기본값: 자동 감지)\n"
          << "  " << kCommonConfigDirOption << " PATH   공통 설정 디렉터리 경로 (기본값: 자동 감지)\n"
          << "  " << kLogCsvOption << " KIND          CSV 로그 활성화 (object|aruco|latency|all, 반복 가능)\n"
          << "  " << kNoSensorOption << "               센서 입력을 위험 판정에서 제외 (테스트 전용)\n"
          << "  " << kHelpOption << ", " << kShortHelpOption << "                 도움말 출력\n";
    return usage.str();
}

const char* sensorModeName(SensorMode mode) {
    return mode == SensorMode::Disabled ? "disabled" : "network";
}

}  // namespace forklift::runtime
