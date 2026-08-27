#pragma once

#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include "common/platform.hpp"

namespace forklift::common {

struct MetadataTiming {
    std::string server_received_utc;
    double delta_ms = NAN;
};

inline std::string formatUtc(const std::chrono::system_clock::time_point& time) {
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(time);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time - seconds).count();
    const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
    if (!forklift::platform::gmtimeUtc(&raw, &utc)) return {};
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << millis << 'Z';
    return output.str();
}

inline bool parseUtc(const std::string& value,
                     std::chrono::system_clock::time_point& output) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':') return false;
    std::tm utc{};
    try {
        utc.tm_year = std::stoi(value.substr(0, 4)) - 1900;
        utc.tm_mon = std::stoi(value.substr(5, 2)) - 1;
        utc.tm_mday = std::stoi(value.substr(8, 2));
        utc.tm_hour = std::stoi(value.substr(11, 2));
        utc.tm_min = std::stoi(value.substr(14, 2));
        utc.tm_sec = std::stoi(value.substr(17, 2));
    } catch (...) {
        return false;
    }
    std::size_t fractionEnd = value.find_first_of("Z+ -", 19);
    if (fractionEnd == std::string::npos) fractionEnd = value.size();
    double fraction = 0.0;
    if (fractionEnd > 19) {
        const std::string fractionText = value.substr(19, fractionEnd - 19);
        // 소수점이 포함된 문자열 자체를 파싱해야 한다. 앞에 "0."을 더하면
        // "0..604"가 되어 stod가 앞의 0만 읽고 소수부를 조용히 버린다.
        if (fractionText.front() != '.') return false;
        try {
            std::size_t consumed = 0;
            fraction = std::stod(fractionText, &consumed);
            if (consumed != fractionText.size()) return false;
        }
        catch (...) { return false; }
    }
#ifdef _WIN32
    const std::time_t raw = _mkgmtime(&utc);
#else
    const std::time_t raw = timegm(&utc);
#endif
    if (raw == static_cast<std::time_t>(-1)) return false;
    output = std::chrono::system_clock::from_time_t(raw) +
             std::chrono::duration_cast<std::chrono::system_clock::duration>(
                 std::chrono::duration<double>(fraction));
    return true;
}

inline MetadataTiming makeMetadataTiming(
    const std::string& cameraUtc,
    const std::chrono::system_clock::time_point& received) {
    MetadataTiming timing;
    timing.server_received_utc = formatUtc(received);
    std::chrono::system_clock::time_point camera;
    if (parseUtc(cameraUtc, camera)) {
        timing.delta_ms = std::chrono::duration<double, std::milli>(received - camera).count();
    }
    return timing;
}

}  // namespace forklift::common
