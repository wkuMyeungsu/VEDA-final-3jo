#pragma once

#include <string>

namespace forklift::runtime {

// 센서 입력은 운영 기본값(Network)으로 두고, 테스트 실행에서만 Disabled를 명시한다.
enum class SensorMode {
    Network,
    Disabled,
};

struct ServerCommandLineOptions {
    std::string config_dir;
    std::string common_config_dir;
    bool enable_debug_csv = false;
    SensorMode sensor_mode = SensorMode::Network;
    bool show_help = false;
};

struct ServerCommandLineParseResult {
    ServerCommandLineOptions options;
    std::string error;

    bool ok() const { return error.empty(); }
};

ServerCommandLineParseResult parseServerCommandLine(
    int argc, char* argv[], std::string default_config_dir);

std::string serverCommandLineUsage(const char* program_name);

const char* sensorModeName(SensorMode mode);

}  // namespace forklift::runtime
