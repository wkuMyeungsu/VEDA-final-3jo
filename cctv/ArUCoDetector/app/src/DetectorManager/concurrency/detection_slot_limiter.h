#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

// 여러 채널이 동시에 "무거운 연산" (왜곡보정 + ArUco 검출)에 들어가는 걸 permits 개로 제한한다.
// 일반 세마포어와 다르게 대기 순서를 큐로 명시적으로 보장한다(FIFO).

class DetectionSlotLimiter {

    public:
        explicit DetectionSlotLimiter(int permits);
        
        DetectionSlotLimiter(const DetectionSlotLimiter&) = delete;
        DetectionSlotLimiter& operator=(const DetectionSlotLimiter&) = delete;

        void Acquire(); // 슬롯 하나를 얻을 때까지 블록, 큐에 들어간 순서대로만 통과.
        void Release(); // 슬롯 반납. 대기 큐의 맨 앞 스레드를 깨운다.

    private:
        const int permits_;                     // 동시에 허용할 최대 스레드 수 (생성 시 고정)
        std::mutex mtx_;                        // 아래 공유 변수들을 보호하는 잠금장치
        std::condition_variable cv_;            // 스레드를 재우고 깨우는 신호 도구
        std::queue<std::thread::id> waiting_;   // 대기 순서를 기록하는 FIFO 큐
        int in_use_ = 0;                        // 현재 슬롯을 쓰고 있는 스레드 수
};