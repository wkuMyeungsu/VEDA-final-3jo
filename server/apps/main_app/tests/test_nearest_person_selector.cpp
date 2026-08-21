#include <iostream>
#include <vector>
#include <string>

#include "logic/tracking/nearest_person_selector.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "실패: " << message << '\n';
        ++failures;
    }
}

Track makeTrack(int id, const std::string& stream, double wx, double wy, double last_seen_s, int missed_frames) {
    Track t;
    t.track_id = id;
    t.stream_id = stream;
    t.camera_id = stream.size() >= 6 ? stream.substr(0, 6) : stream;
    t.last_world = {wx, wy};
    t.last_seen_s = last_seen_s;
    t.missed_frames = missed_frames;
    return t;
}

// Case 1: 트랙 2개, 둘 다 신선(last_seen_s = now_s), 거리 280mm / 2429mm -> 280mm 선택
void testBothFreshSelectNearest() {
    WorldPoint forklift{0.0, 0.0};
    double now_s = 100.0;
    double freshness_sec = 0.2; // 200ms

    std::vector<Track> tracks = {
        makeTrack(1, "CAM_01_CH_03", 280.0, 0.0, now_s, 0),      // 280mm
        makeTrack(2, "CAM_01_CH_02", 2429.0, 0.0, now_s, 0)     // 2429mm
    };

    auto result = selectNearestPerson(forklift, tracks, now_s, freshness_sec);
    check(result.found, "Case 1: 사람 검출 성공");
    check(result.track_id == 1, "Case 1: 더 가까운 트랙(280mm, CH_03) 선택");
    check(result.distance_mm == 280.0, "Case 1: 거리 280mm");
}

// Case 2: 가까운 트랙이 오래됨(now_s - last_seen_s > freshness), 먼 트랙은 신선 -> 먼 트랙 선택
void testNearStaleFarFresh() {
    WorldPoint forklift{0.0, 0.0};
    double now_s = 100.0;
    double freshness_sec = 0.2; // 200ms

    std::vector<Track> tracks = {
        makeTrack(1, "CAM_01_CH_03", 280.0, 0.0, now_s - 0.3, 2),  // 300ms 전 (stale)
        makeTrack(2, "CAM_01_CH_02", 2429.0, 0.0, now_s, 0)       // 현재 (fresh)
    };

    auto result = selectNearestPerson(forklift, tracks, now_s, freshness_sec);
    check(result.found, "Case 2: 사람 검출 성공");
    check(result.track_id == 2, "Case 2: 신선한 먼 트랙(2429mm, CH_02) 선택");
    check(result.distance_mm == 2429.0, "Case 2: 거리 2429mm");
}

// Case 3: 모든 트랙이 오래됨 -> found == false
void testAllStaleNoneFound() {
    WorldPoint forklift{0.0, 0.0};
    double now_s = 100.0;
    double freshness_sec = 0.2; // 200ms

    std::vector<Track> tracks = {
        makeTrack(1, "CAM_01_CH_03", 280.0, 0.0, now_s - 0.25, 3), // 250ms 전 (stale)
        makeTrack(2, "CAM_01_CH_02", 2429.0, 0.0, now_s - 0.4, 5) // 400ms 전 (stale)
    };

    auto result = selectNearestPerson(forklift, tracks, now_s, freshness_sec);
    check(!result.found, "Case 3: 모든 트랙이 freshness 초과 시 found == false");
}

// Case 4: missed_frames가 1 이상이지만 last_seen_s는 신선 -> 후보로 인정됨 (핵심 회귀 방지)
void testMissedFramesPositiveButFreshAccepted() {
    WorldPoint forklift{0.0, 0.0};
    double now_s = 100.0;
    double freshness_sec = 0.2; // 200ms

    // 다른 채널의 프레임이 방금 들어와 missed_frames=1이 되었지만, 실제로는 50ms 전에 관측된 트랙
    std::vector<Track> tracks = {
        makeTrack(1, "CAM_01_CH_03", 280.0, 0.0, now_s - 0.05, 1)  // 50ms 전, missed_frames=1
    };

    auto result = selectNearestPerson(forklift, tracks, now_s, freshness_sec);
    check(result.found, "Case 4: missed_frames > 0 이어도 freshness 이내면 후보로 인정");
    check(result.track_id == 1, "Case 4: 트랙 1 정상 선택");
    check(result.distance_mm == 280.0, "Case 4: 거리 280mm");
}

} // namespace

int main() {
    testBothFreshSelectNearest();
    testNearStaleFarFresh();
    testAllStaleNoneFound();
    testMissedFramesPositiveButFreshAccepted();

    if (failures > 0) {
        std::cerr << "총 " << failures << "개 테스트 실패\n";
        return 1;
    }
    std::cout << "test_nearest_person_selector: 모든 테스트 통과 (4/4)\n";
    return 0;
}
