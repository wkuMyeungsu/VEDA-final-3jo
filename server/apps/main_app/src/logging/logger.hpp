#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unistd.h>

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

    // 프로세스 기동을 구분하는 식별자다. 여러 서버 실행본과 systemd journal을
    // 시간만으로 합치면 재시작·NTP 보정 구간을 구분하기 어려우므로 PID와 UTC 기동
    // 시각을 함께 사용한다. 한 프로세스 안에서는 절대 변하지 않는다.
    const std::string& runId() const { return run_id_; }

    // 설정 파일을 읽는 동안 발생한 로그도 최종 server.log에 남겨야 한다.
    // 로그 파일 경로는 설정 로드가 끝난 뒤에야 확정되므로, 그 전까지는
    // 소량만 메모리에 보관했다가 파일이 열리면 한 번에 flush한다.
    bool setLogFile(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_stream_.is_open()) {
            file_stream_.close();
        }
        if (!filepath.empty()) {
            file_stream_.open(filepath, std::ios::app);
        } else {
            return false;
        }

        if (!file_stream_.is_open()) return false;

        while (!pending_lines_.empty()) {
            file_stream_ << pending_lines_.front();
            pending_lines_.pop_front();
        }
        file_stream_.flush();
        return static_cast<bool>(file_stream_);
    }

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
        gmtime_r(&now_time_t, &tm_buf);

        std::ostringstream ss;
        ss << "[" << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z] "
           << "[" << levelToString(level) << "] "
           << "[" << tag << "] "
           << message << " [run_id=" << run_id_ << "]\n";

        const std::string log_line = ss.str();

        std::lock_guard<std::mutex> lock(mutex_);
        if (level == LogLevel::Error) {
            std::cerr << log_line;
            std::cerr.flush();
        } else {
            std::cout << log_line;
            std::cout.flush();
        }

        if (file_stream_.is_open()) {
            file_stream_ << log_line;
            file_stream_.flush();
        } else {
            // 기동 실패처럼 파일 경로 자체를 끝내 확정하지 못하는 경우에도
            // 메모리가 무한히 늘지 않도록 최근 로그만 보관한다.
            if (pending_lines_.size() >= kPendingLineLimit) {
                pending_lines_.pop_front();
            }
            pending_lines_.push_back(log_line);
        }
    }

private:
    Logger() : run_id_(makeRunId()) {}
    ~Logger() {
        if (file_stream_.is_open()) {
            file_stream_.close();
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
        gmtime_r(&now_time_t, &tm_buf);

        std::ostringstream ss;
        ss << std::put_time(&tm_buf, "%Y%m%dT%H%M%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count()
           << "Z-p" << static_cast<long long>(::getpid());
        return ss.str();
    }

    static constexpr std::size_t kPendingLineLimit = 256;
    std::mutex mutex_;
    std::ofstream file_stream_;
    std::deque<std::string> pending_lines_;
    std::atomic<bool> debug_enabled_{false};
    const std::string run_id_;
};

}  // namespace forklift::logging

#define LOG_DEBUG(tag, msg) ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Debug, tag, msg)
#define LOG_INFO(tag, msg)  ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Info,  tag, msg)
#define LOG_WARN(tag, msg)  ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Warn,  tag, msg)
#define LOG_ERROR(tag, msg) ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Error, tag, msg)
