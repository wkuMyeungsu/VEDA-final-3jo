#include "detection_slot_limiter.h"

DetectionSlotLimiter::DetectionSlotLimiter(int permits) : permits_(permits) {}

void DetectionSlotLimiter::Acquire() {
    std::unique_lock<std::mutex> lk(mtx_);                      // 1. 잠금
    const auto id = std::this_thread::get_id();                 // 2. 내 스레드 ID 기록
    waiting_.push(id);                                          // 3. 대기 큐 맨 뒤에 등록
    cv_.wait(lk, [this, id] {                                   // 4. 조건 만족할 때까지 잠듦.
        return in_use_ < permits_ && waiting_.front() == id;    //      - 빈슬롯 있고, 큐 맨 앞이면 통과
    });                                         
    waiting_.pop();                                             // 5. 큐에서 맨 앞 스레드를 제거
    in_use_++;                                                  // 6. 슬롯 하나 사용 중으로 표시
}

void DetectionSlotLimiter::Release() {
    std::lock_guard<std::mutex> lk(mtx_);
    in_use_--;
    cv_.notify_all();
}