// nearest_person_selector_main.cpp
// 최근접 사람 선택 독립 실행 진입점 - 더미 데이터 테스트 시나리오 4종
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 선택 로직은 nearest_person_selector.h / .cpp에 있고, 이 파일은 실행파일
// (nearest_person_demo)의 main()만 담당한다. 라이브러리로 분리했으므로 이 main()은
// judgment_pipeline이나 테스트 실행파일의 main()과 충돌하지 않는다.
//
// 빌드:
//   CMake: cmake -S . -B build && cmake --build build --target nearest_person_demo
//   또는 직접: g++ -std=c++17 -I../judgment nearest_person_selector_main.cpp \
//              nearest_person_selector.cpp -o nearest_person_demo

#include <iostream>
#include <vector>

#include "nearest_person_selector.h"

// ============================================================
// 테스트 시나리오 (더미 데이터)
// ============================================================
// [교체 지점] tracks 목록은 실제로는 cross_camera_reid.cpp의 update() 반환값이 들어옴.

int main() {
    std::cout << "=== 최근접 사람 선택 - 테스트 ===\n\n";

    WorldPoint forklift{5.0, 5.0};

    // 시나리오 1: 여러 명 중 가장 가까운 사람 선택
    {
        std::vector<Track> tracks = {
            {1, 1, {8.0, 8.0}, 0},   // 거리 약 4.24m
            {2, 1, {5.5, 5.5}, 0},   // 거리 약 0.71m <- 최근접이어야 함
            {3, 2, {1.0, 1.0}, 0},   // 거리 약 5.66m
        };
        auto result = selectNearestPerson(forklift, tracks);
        printResult("[시나리오 1] 3명 중 최근접 선택", forklift, result);
    }

    // 시나리오 2: 가장 가까운 트랙이 이번 프레임 미검출 -> 제외되고 차순위가 선택돼야 함
    {
        std::vector<Track> tracks = {
            {4, 1, {5.1, 5.1}, 2},   // 매우 가깝지만 2프레임째 미검출 -> 제외 대상
            {5, 1, {7.0, 7.0}, 0},   // 정상 검출 중 -> 이게 선택돼야 함
        };
        auto result = selectNearestPerson(forklift, tracks);
        printResult("[시나리오 2] 미검출 트랙 제외 확인", forklift, result);
    }

    // 시나리오 3: 트랙이 아예 없음 (화면에 사람 없음)
    {
        std::vector<Track> tracks = {};
        auto result = selectNearestPerson(forklift, tracks);
        printResult("[시나리오 3] 사람 없음", forklift, result);
    }

    // 시나리오 4: 전부 미검출 상태 (일시적 가림 등) -> found=false여야 함
    {
        std::vector<Track> tracks = {
            {6, 1, {5.2, 5.2}, 1},
            {7, 1, {6.0, 6.0}, 3},
        };
        auto result = selectNearestPerson(forklift, tracks);
        printResult("[시나리오 4] 전부 미검출 상태", forklift, result);
    }

    std::cout << "=== 테스트 종료 ===\n";
    return 0;
}
