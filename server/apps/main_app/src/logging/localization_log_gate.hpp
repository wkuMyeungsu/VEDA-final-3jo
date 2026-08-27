#pragma once

#include <chrono>
#include <string>

namespace forklift::logging {

// 지게차 마커가 몇 초 간격으로 보였다 안 보였다 할 때 LOCALIZED ↔ 미검출 로그가
// 연속으로 쌓이지 않게 한다. 첫 상태는 즉시 남기고, 이후에는 같은 상태가
// kCooldown 동안 유지된 뒤에만 한 줄 더 찍는다.
struct LocalizationLogGate {
    static constexpr std::chrono::seconds kCooldown{3};

    bool shouldLog(const std::string& status, std::chrono::steady_clock::time_point now) {
        if (last_logged_.empty()) {
            last_logged_ = status;
            pending_ = status;
            pending_since_ = now;
            return true;
        }
        if (status == last_logged_) {
            pending_ = status;
            pending_since_ = now;
            return false;
        }
        if (status != pending_) {
            pending_ = status;
            pending_since_ = now;
            return false;
        }
        if (now - pending_since_ < kCooldown) return false;
        last_logged_ = status;
        return true;
    }

private:
    std::string last_logged_;
    std::string pending_;
    std::chrono::steady_clock::time_point pending_since_{};
};

}  // namespace forklift::logging
