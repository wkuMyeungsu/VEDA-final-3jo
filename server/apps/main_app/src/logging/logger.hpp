#pragma once

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

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

    void setLogFile(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_stream_.is_open()) {
            file_stream_.close();
        }
        if (!filepath.empty()) {
            file_stream_.open(filepath, std::ios::app);
        }
    }

    void log(LogLevel level, const std::string& tag, const std::string& message) {
        const auto now = std::chrono::system_clock::now();
        const auto now_time_t = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) % 1000;

        std::tm tm_buf{};
        localtime_r(&now_time_t, &tm_buf);

        std::ostringstream ss;
        ss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
           << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
           << "[" << levelToString(level) << "] "
           << "[" << tag << "] "
           << message << "\n";

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
        }
    }

private:
    Logger() = default;
    ~Logger() {
        if (file_stream_.is_open()) {
            file_stream_.close();
        }
    }

    static const char* levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO ";
            case LogLevel::Warn:  return "WARN ";
            case LogLevel::Error: return "ERROR";
            default:              return "LOG  ";
        }
    }

    std::mutex mutex_;
    std::ofstream file_stream_;
};

}  // namespace forklift::logging

#define LOG_DEBUG(tag, msg) ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Debug, tag, msg)
#define LOG_INFO(tag, msg)  ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Info,  tag, msg)
#define LOG_WARN(tag, msg)  ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Warn,  tag, msg)
#define LOG_ERROR(tag, msg) ::forklift::logging::Logger::instance().log(::forklift::logging::LogLevel::Error, tag, msg)
