#pragma once

#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#elif defined(__unix__)
#include <unistd.h>
#endif

namespace forklift::platform {

inline bool gmtimeUtc(const std::time_t* source, std::tm* target) {
#if defined(_WIN32)
    return ::gmtime_s(target, source) == 0;
#elif defined(__unix__) || defined(__APPLE__)
    return ::gmtime_r(source, target) != nullptr;
#else
    const auto* value = std::gmtime(source);
    if (!value) return false;
    *target = *value;
    return true;
#endif
}

inline bool localtimeLocal(const std::time_t* source, std::tm* target) {
#if defined(_WIN32)
    return ::localtime_s(target, source) == 0;
#elif defined(__unix__) || defined(__APPLE__)
    return ::localtime_r(source, target) != nullptr;
#else
    const auto* value = std::localtime(source);
    if (!value) return false;
    *target = *value;
    return true;
#endif
}

inline std::string processId() {
#if defined(_WIN32)
    return std::to_string(static_cast<unsigned long long>(::GetCurrentProcessId()));
#elif defined(__unix__) || defined(__APPLE__)
    return std::to_string(static_cast<long long>(::getpid()));
#else
    return "0";
#endif
}

inline std::filesystem::path executablePath() {
#if defined(_WIN32)
    std::wstring buffer(256, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(),
                                                   static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return std::filesystem::path(buffer);
        }
        if (buffer.size() >= 32768) return {};
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) return {};
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    return std::filesystem::path(buffer.data());
#elif defined(__linux__)
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) return {};
    buffer[length] = '\0';
    return std::filesystem::path(buffer);
#else
    return {};
#endif
}

inline std::filesystem::path executableDirectory() {
    const auto path = executablePath();
    if (!path.empty() && !path.parent_path().empty()) return path.parent_path();
    return std::filesystem::current_path();
}

inline void replaceFile(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        std::error_code& error) {
#if defined(_WIN32)
    if (::MoveFileExW(source.c_str(), destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error.clear();
    } else {
        error = std::error_code(static_cast<int>(::GetLastError()), std::system_category());
    }
#else
    std::filesystem::rename(source, destination, error);
#endif
}

}  // namespace forklift::platform
