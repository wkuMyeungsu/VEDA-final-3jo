// nearest_person_selector.cpp
// 지게차에서 가장 가까운 사람 선택 - 더미 데이터 기반 프로토타입
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 목적: cross_camera_reid.cpp가 돌려주는 여러 명의 Track 중,
//       특정 지게차에서 가장 가까운 사람 1명만 골라 danger_judgment_engine.cpp의
//       CameraInput.person 필드로 넘길 값을 만든다.
//       (박명수 파트가 "사람 중복 제거는 안 한다"고 결정했으므로, 여러 명 중 1명을
//        추리는 책임은 이쪽에 있다 - 워크가이드에 명시된 결정사항.)
//
// [교체 지점] Track 구조체는 cross_camera_reid.cpp의 Track과 필드 구성을 맞춰야 함.
//   실제 배포 시 두 파일은 공통 헤더(예: server/common/types.hpp)로 WorldPoint를
//   공유하는 게 이상적 (지금은 각 파일이 독립적으로 테스트 가능하도록 중복 정의함).
//
// 빌드: g++ -std=c++17 nearest_person_selector.cpp -o nearest_person_selector

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include <iomanip>

// ============================================================
// 1. 데이터 구조 정의
// ============================================================

struct WorldPoint {
    double x = 0.0;
    double y = 0.0;
};

// cross_camera_reid.cpp의 Track과 동일한 필드 구성 (실제로는 그쪽 출력이 그대로 들어옴)
struct Track {
    int        track_id;
    int        camera_id;
    WorldPoint last_world;
    int        missed_frames = 0;
};

// 최근접 사람 선택 결과
struct NearestPersonResult {
    bool       found = false;
    int        track_id = -1;
    WorldPoint position;
    double     distance_m = std::numeric_limits<double>::max();
};

// ============================================================
// 2. 선택 로직
// ============================================================

double euclideanDistance(const WorldPoint& a, const WorldPoint& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// 지게차 world 좌표 기준, tracks 중 가장 가까운 사람 1명을 찾는다.
// missed_frames > 0인 트랙(이번 프레임에 실제로 검출 안 된 트랙)은 후보에서 제외한다.
NearestPersonResult selectNearestPerson(const WorldPoint& forklift,
                                         const std::vector<Track>& tracks) {
    NearestPersonResult result;
    for (const auto& t : tracks) {
        if (t.missed_frames > 0) continue; // 이번 프레임에 실제로 안 보인 트랙은 제외

        double d = euclideanDistance(forklift, t.last_world);
        if (d < result.distance_m) {
            result.found      = true;
            result.track_id   = t.track_id;
            result.position   = t.last_world;
            result.distance_m = d;
        }
    }
    return result;
}

// ============================================================
// 3. 출력 헬퍼
// ============================================================

void printResult(const std::string& scenario, const WorldPoint& forklift,
                  const NearestPersonResult& r) {
    std::cout << scenario << "\n";
    std::cout << "  지게차 위치: (" << forklift.x << ", " << forklift.y << ")\n";
    if (r.found) {
        std::cout << "  -> 최근접 사람: track_id=" << r.track_id
                  << " | 위치=(" << r.position.x << ", " << r.position.y << ")"
                  << " | 거리=" << std::fixed << std::setprecision(2) << r.distance_m << "m\n";
    } else {
        std::cout << "  -> 근처에 유효한 사람 트랙 없음\n";
    }
    std::cout << "\n";
}

// ============================================================
// 4. 테스트 시나리오 (더미 데이터)
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
