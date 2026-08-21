#include <atomic>
#include <iostream>

#include "common/bounded_queue.hpp"

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "실패: " << message << '\n';
        ++failures;
    }
}
}  // namespace

int main() {
    forklift::common::BoundedQueue<int> queue(2);
    check(!queue.push(1).dropped_oldest, "첫 항목은 드롭하지 않음");
    check(!queue.push(2).dropped_oldest, "용량 이내 항목은 드롭하지 않음");

    const auto overflow = queue.push(3);
    check(overflow.dropped_oldest && overflow.dropped_total == 1,
          "용량 초과 시 가장 오래된 항목 한 건을 드롭함");
    check(queue.size() == 2 && queue.droppedTotal() == 1,
          "큐 크기와 누적 드롭 수를 유지함");

    std::atomic<bool> running{true};
    int value = 0;
    check(queue.waitPop(value, running) && value == 2,
          "드롭 이후 가장 오래된 생존 항목부터 반환함");
    check(queue.waitPop(value, running) && value == 3,
          "최신 항목을 보존함");

    running = false;
    check(!queue.waitPop(value, running),
          "종료 후 빈 큐에서는 대기를 끝냄");
    return failures == 0 ? 0 : 1;
}
