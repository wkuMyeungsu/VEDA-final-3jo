// nearest_person_selector.cpp
// 지게차 최근접 사람 선택 구현 - 더미 데이터 기반 프로토타입
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 자료구조 선언과 함수 선언은 nearest_person_selector.h,
// 독립 실행용 main()과 더미 시나리오는 nearest_person_selector_main.cpp에 있다.
// 선택 로직 자체는 분리 전과 동일하다.
//
// 빌드:
//   CMake: cmake -S . -B build && cmake --build build
//   또는 직접: g++ -std=c++17 -I../judgment -c nearest_person_selector.cpp
//             (-I../judgment은 헤더가 WorldPoint를 danger_judgment_engine.h에서
//              가져오기 때문에 필요하다. 링크할 심볼은 없다.)

#include "logic/tracking/nearest_person_selector.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// ============================================================
// 1. 선택 로직
// ============================================================

double euclideanDistance(const WorldPoint& a, const WorldPoint& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

NearestPersonResult selectNearestPerson(const WorldPoint& forklift,
                                        const std::vector<Track>& tracks) {
    NearestPersonResult result;
    for (const auto& t : tracks) {
        if (t.missed_frames > 0) continue; // 이번 프레임에 실제로 안 보인 트랙은 제외

        double d = euclideanDistance(forklift, t.last_world);
        if (d < result.distance_mm) {
            result.found      = true;
            result.track_id   = t.track_id;
            result.stream_id  = t.stream_id;
            result.camera_id  = t.camera_id;
            result.channel    = t.channel;
            result.position   = t.last_world;
            result.distance_mm = d;
        }
    }
    return result;
}

// ============================================================
// 2. 출력 헬퍼
// ============================================================

void printResult(const std::string& scenario, const WorldPoint& forklift,
                 const NearestPersonResult& r) {
    std::cout << scenario << "\n";
    std::cout << "  지게차 위치: (" << forklift.x << ", " << forklift.y << ")\n";
    if (r.found) {
        std::cout << "  -> 최근접 사람: track_id=" << r.track_id
                  << " | camera_id=" << r.camera_id
                  << " | 위치=(" << r.position.x << ", " << r.position.y << ")"
                  << " | 거리=" << std::fixed << std::setprecision(2) << r.distance_mm << "mm\n";
    } else {
        std::cout << "  -> 근처에 유효한 사람 트랙 없음\n";
    }
    std::cout << "\n";
}
