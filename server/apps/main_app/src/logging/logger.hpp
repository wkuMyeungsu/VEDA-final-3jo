#pragma once

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "common/platform.hpp"

namespace forklift::logging {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

class Logger {
public:
    static Logger& instance() {
        static Logger instance_;
        return instance_;
    }

    // 프로세스 기동을 구분하는 식별자다. MQTT payload와 기동 배너에 쓰고, 매 줄
    // 로그 꼬리표로는 붙이지 않는다. 한 프로세스 안에서는 절대 변하지 않는다.
    const std::string& runId() const { return run_id_; }

    // DEBUG 로그는 일반 운영 환경에서 기본적으로 끈다. 호출부가
    // 디버그 모드나 명시적 진단 옵션을 켰을 때만 파일/콘솔 I/O를 수행한다.
    void setDebugEnabled(bool enabled) { debug_enabled_.store(enabled); }

    void log(LogLevel level, const std::string& tag, const std::string& message) {
        if (level == LogLevel::Debug && !debug_enabled_.load()) return;

        const auto now = std::chrono::system_clock::now();
        const auto now_time_t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) % 1000;

        std::tm tm_buf{};
        forklift::platform::gmtimeUtc(&now_time_t, &tm_buf);

        std::ostringstream ss;
        ss << "[" << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z] "
           << "[" << levelToString(level) << "] "
           << "[" << tag << "] "
           << message << "\n";

        writeOutput(ss.str(), level == LogLevel::Error);
    }

    // 기동 배너가 다른 로그보다 먼저 보이게, 배너 전까지의 출력을 잠시 붙잡아 둔다.
    void holdUntilReady() {
        std::lock_guard<std::mutex> lock(mutex_);
        holding_ = true;
    }

    // 기동 배너를 먼저 쓰고, 붙잡아 둔 로그를 그 뒤에 이어서 내보낸다.
    void announceReady(const std::string& text) {
        std::string payload = text;
        if (payload.empty() || payload.back() != '\n') payload.push_back('\n');
        std::lock_guard<std::mutex> lock(mutex_);
        emitLocked(payload, false);
        holding_ = false;
        for (const auto& entry : held_) emitLocked(entry.first, entry.second);
        held_.clear();
    }

    // 기동 실패처럼 배너 없이 버퍼만 비울 때 사용한다.
    void releaseHold() {
        std::lock_guard<std::mutex> lock(mutex_);
        holding_ = false;
        for (const auto& entry : held_) emitLocked(entry.first, entry.second);
        held_.clear();
    }

    // 기동 배너처럼 [시간] [LEVEL] [TAG] 접두어가 없는 줄을 남긴다.
    // 여러 줄을 한 번에 써서 다른 스레드 로그와 섞이지 않게 한다.
    void writeUnprefixed(const std::string& text) {
        std::string payload = text;
        if (payload.empty() || payload.back() != '\n') payload.push_back('\n');
        writeOutput(payload, false);
    }

private:
    Logger() : run_id_(makeRunId()) {}

    void writeOutput(const std::string& text, bool error_stream) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (holding_) {
            held_.emplace_back(text, error_stream);
            return;
        }
        emitLocked(text, error_stream);
    }

    void emitLocked(const std::string& text, bool error_stream) {
        if (error_stream) {
            std::cerr << text;
            std::cerr.flush();
        } else {
            std::cout << text;
            std::cout.flush();
        }
    }

    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
            default:              return "LOG";
        }
    }

    static std::string makeRunId() {
        const auto now = std::chrono::system_clock::now();
        const auto now_time_t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) % 1000;
        std::tm tm_buf{};
        forklift::platform::gmtimeUtc(&now_time_t, &tm_buf);

        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y%m%dT%H%M%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count()
           << "Z-p" << forklift::platform::processId();
        return ss.str();
    }

    std::mutex mutex_;
    std::atomic<bool> debug_enabled_{false};
    bool holding_{false};
    std::vector<std::pair<std::string, bool>> held_;
    const std::string run_id_;
};

}  // namespace forklift::logging

#define LOG_DEBUG(tag, msg) ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Debug, tag, msg)
#define LOG_INFO(tag, msg)  ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Info,  tag, msg)
#define LOG_WARN(tag, msg)  ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Warn,  tag, msg)
#define LOG_ERROR(tag, msg) ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Error, tag, msg)
