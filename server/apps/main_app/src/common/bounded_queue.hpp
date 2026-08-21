#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace forklift::common {

// 실시간 입력이 소비 속도를 앞지를 때 메모리와 판정 지연이 무한히 늘지 않도록
// 가장 오래된 항목을 버리고 최신 항목을 유지하는 bounded FIFO다.
template <typename T>
class BoundedQueue {
public:
    struct PushResult {
        bool dropped_oldest{};
        std::size_t dropped_total{};
    };

    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    PushResult push(T value) {
        PushResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= capacity_) {
                queue_.pop_front();
                result.dropped_oldest = true;
                result.dropped_total = ++dropped_total_;
            }
            queue_.push_back(std::move(value));
        }
        condition_.notify_one();
        return result;
    }

    // running=false가 되어도 이미 들어온 항목은 모두 소비한 뒤 false를 반환한다.
    bool waitPop(T& value, const std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this, &running] {
            return !running.load() || !queue_.empty();
        });
        if (queue_.empty()) return false;
        value = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void notifyAll() { condition_.notify_all(); }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    std::size_t droppedTotal() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_total_;
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<T> queue_;
    std::size_t dropped_total_{};
};

}  // namespace forklift::common
