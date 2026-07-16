// cross_camera_reid.cpp
// 카메라 간 객체 ID 유지 (크로스카메라 Re-ID) - 더미 데이터 기반 프로토타입
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 목적: 카메라 핸드오버(A -> B) 시 동일 인물에게 동일 tracking ID를 유지시킨다.
//   - 기본: 같은 카메라 내 프레임 간 연속성은 IoU(bbox 겹침 비율)로 매칭
//   - 카메라 전환(핸드오버) 시점: world 좌표(호모그래피 변환 결과) 근접도로 매칭
//   - [보강] 같은 카메라라도 검출 주기가 낮거나 이동이 빨라 IoU가 깨질 경우를 대비해,
//           world 좌표 근접도를 항상 fallback으로 함께 확인한다 (둘 중 더 확실한 신호 채택).
//
// [교체 지점] Detection 구조체의 bbox/world 필드는 현재 더미 값.
//   실제로는 ONVIF 메타데이터 파서(BoundingBox) + 호모그래피 변환(cv::perspectiveTransform)
//   결과로 교체된다. 이 파일의 매칭 로직 자체는 그대로 재사용 가능.
//
// 빌드: g++ -std=c++17 cross_camera_reid.cpp -o cross_camera_reid
// (순수 표준 C++17, 외부 의존성 없음 -> 윈도우/라즈베리파이 동일하게 빌드됨)

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>

// ============================================================
// 1. 데이터 구조 정의
// ============================================================

// 바닥 평면 위 실제 좌표 (미터 단위)
// [교체 지점] 호모그래피 변환(cv::perspectiveTransform) 결과로 교체 예정
struct WorldPoint {
    double x = 0.0;
    double y = 0.0;
};

// 카메라 자체 픽셀 좌표계 기준 bbox (같은 카메라 내에서만 비교 의미 있음)
// [교체 지점] ONVIF BoundingBox 파싱 결과로 교체 예정 (센서 원본 해상도 2592x1520 기준)
struct PixelBBox {
    double x1 = 0, y1 = 0;  // 좌상단
    double x2 = 0, y2 = 0;  // 우하단
};

// 한 프레임에서 들어온 검출 결과 (아직 track_id 없음)
struct Detection {
    int        camera_id;
    PixelBBox  bbox;    // 자기 카메라 픽셀 좌표
    WorldPoint world;   // world 좌표 (카메라가 달라도 비교 가능)
    double     timestamp_s;
};

// 유지되고 있는 추적 대상
struct Track {
    int        track_id;
    int        camera_id;      // 현재 이 트랙을 보고 있는 카메라
    PixelBBox  last_bbox;
    WorldPoint last_world;
    double     last_seen_s = 0.0;
    int        missed_frames = 0;   // 연속으로 매칭 안 된 프레임 수
};

// ============================================================
// 2. 매칭 점수 계산 헬퍼
// ============================================================

double iou(const PixelBBox& a, const PixelBBox& b) {
    double ix1 = std::max(a.x1, b.x1);
    double iy1 = std::max(a.y1, b.y1);
    double ix2 = std::min(a.x2, b.x2);
    double iy2 = std::min(a.y2, b.y2);
    double iw = std::max(0.0, ix2 - ix1);
    double ih = std::max(0.0, iy2 - iy1);
    double inter = iw * ih;

    double areaA = std::max(0.0, a.x2 - a.x1) * std::max(0.0, a.y2 - a.y1);
    double areaB = std::max(0.0, b.x2 - b.x1) * std::max(0.0, b.y2 - b.y1);
    double uni = areaA + areaB - inter;

    if (uni <= 0.0) return 0.0;
    return inter / uni;
}

double euclideanDistance(const WorldPoint& a, const WorldPoint& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// ============================================================
// 3. 크로스카메라 트래커
// ============================================================

class CrossCameraTracker {
public:
    // 임계값들 - 실측/실험 통해 조정 예정 [확실하지 않음: 초기값, 실측 필요]
    double iou_threshold = 0.3;           // 동일 카메라 프레임 간 매칭
    double world_dist_threshold_m = 1.0;  // 카메라 전환(핸드오버) 매칭
    int    max_missed_frames = 5;         // 이 프레임 수 넘게 못 찾으면 트랙 소멸

    // 한 프레임의 검출 목록을 받아 트랙을 갱신하고, 현재 트랙 목록을 반환
    std::vector<Track> update(const std::vector<Detection>& detections, double now_s) {
        std::vector<bool> det_matched(detections.size(), false);
        std::vector<bool> track_matched(tracks_.size(), false);

        // ── 1) 모든 (트랙, 검출) 쌍의 매칭 후보와 점수 계산 ──────────
        struct Candidate { size_t track_idx; size_t det_idx; double score; bool is_handover; };
        std::vector<Candidate> candidates;

        for (size_t ti = 0; ti < tracks_.size(); ++ti) {
            for (size_t di = 0; di < detections.size(); ++di) {
                const Track& t = tracks_[ti];
                const Detection& d = detections[di];

                double iou_score = -1.0;   // 유효하지 않으면 -1
                double world_score = -1.0;

                if (t.camera_id == d.camera_id) {
                    // 동일 카메라: IoU 우선 (프레임 간 연속성, bbox 비교 가능)
                    double v = iou(t.last_bbox, d.bbox);
                    if (v >= iou_threshold) iou_score = v;
                }

                // world 거리는 카메라 동일 여부와 무관하게 항상 계산 가능
                // (호모그래피로 이미 공통 좌표계로 변환된 값이므로)
                // -> 동일 카메라인데 검출 주기가 느리거나 이동이 빨라 IoU가 깨진 경우의 fallback,
                //    그리고 카메라 전환(핸드오버) 시의 유일한 매칭 수단
                double dist = euclideanDistance(t.last_world, d.world);
                if (dist <= world_dist_threshold_m) {
                    world_score = 1.0 - (dist / world_dist_threshold_m);
                }

                if (iou_score < 0.0 && world_score < 0.0) continue; // 둘 다 임계값 밖 -> 매칭 후보 아님

                bool is_handover = (t.camera_id != d.camera_id);
                double score = std::max(iou_score, world_score); // 둘 중 더 확실한 신호 채택
                candidates.push_back({ti, di, score, is_handover});
            }
        }

        // ── 2) 점수 내림차순 그리디 매칭 (점수 높은 쌍부터 확정) ──────
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate& a, const Candidate& b) { return a.score > b.score; });

        for (const auto& c : candidates) {
            if (track_matched[c.track_idx] || det_matched[c.det_idx]) continue;

            track_matched[c.track_idx] = true;
            det_matched[c.det_idx] = true;

            Track& t = tracks_[c.track_idx];
            const Detection& d = detections[c.det_idx];

            if (c.is_handover) {
                std::cout << "  [핸드오버] track_id=" << t.track_id
                          << " : camera " << t.camera_id << " -> camera " << d.camera_id
                          << " (world 거리=" << std::fixed << std::setprecision(2)
                          << euclideanDistance(t.last_world, d.world) << "m) ID 승계\n";
            }

            t.camera_id   = d.camera_id;
            t.last_bbox   = d.bbox;
            t.last_world  = d.world;
            t.last_seen_s = now_s;
            t.missed_frames = 0;
        }

        // ── 3) 매칭 안 된 기존 트랙: missed_frames 증가, 초과 시 소멸 ──
        for (size_t ti = 0; ti < tracks_.size(); ++ti) {
            if (!track_matched[ti]) {
                tracks_[ti].missed_frames++;
            }
        }
        tracks_.erase(
            std::remove_if(tracks_.begin(), tracks_.end(),
                [&](const Track& t) {
                    bool expired = t.missed_frames > max_missed_frames;
                    if (expired) {
                        std::cout << "  [트랙 소멸] track_id=" << t.track_id
                                  << " (" << t.missed_frames << "프레임 연속 미검출)\n";
                    }
                    return expired;
                }),
            tracks_.end());

        // ── 4) 매칭 안 된 검출: 새 트랙(새 ID) 생성 ────────────────
        for (size_t di = 0; di < detections.size(); ++di) {
            if (!det_matched[di]) {
                Track nt;
                nt.track_id      = next_id_++;
                nt.camera_id     = detections[di].camera_id;
                nt.last_bbox     = detections[di].bbox;
                nt.last_world    = detections[di].world;
                nt.last_seen_s   = now_s;
                std::cout << "  [신규 트랙] track_id=" << nt.track_id
                          << " (camera " << nt.camera_id << ")\n";
                tracks_.push_back(nt);
            }
        }

        return tracks_;
    }

private:
    std::vector<Track> tracks_;
    int next_id_ = 1;
};

// ============================================================
// 4. 출력 헬퍼
// ============================================================

void printTracks(const std::vector<Track>& tracks) {
    if (tracks.empty()) {
        std::cout << "  (활성 트랙 없음)\n";
        return;
    }
    for (const auto& t : tracks) {
        std::cout << "  -> track_id=" << t.track_id
                  << " | camera=" << t.camera_id
                  << " | world=(" << std::fixed << std::setprecision(1)
                  << t.last_world.x << ", " << t.last_world.y << ")"
                  << " | missed=" << t.missed_frames << "\n";
    }
}

// ============================================================
// 5. 테스트 시나리오 (더미 데이터)
// ============================================================
// 시나리오: camera1(x: 0~10m 구역) -> camera2(x: 8~20m 구역), 핸드오버 겹침구역 8~10m
// [교체 지점] 아래 프레임들은 전부 더미. 실제로는 매 프레임 ONVIF 파서 출력이 여기로 들어옴.

int main() {
    CrossCameraTracker tracker;

    std::cout << "=== 크로스카메라 Re-ID 테스트 ===\n\n";

    // Frame 1: camera1에서 사람 A 최초 검출 -> 신규 트랙(ID 1) 예상
    std::cout << "[Frame 1] t=0.0s, camera1 사람A 최초 검출\n";
    {
        std::vector<Detection> dets = {
            {1, {400,300,500,450}, {5.0, 5.0}, 0.0}
        };
        printTracks(tracker.update(dets, 0.0));
    }

    // Frame 2: camera1에서 사람A 이동 (bbox 겹침 큼) -> IoU 매칭으로 ID1 유지 예상
    std::cout << "\n[Frame 2] t=0.1s, camera1 사람A 이동 (같은 카메라)\n";
    {
        std::vector<Detection> dets = {
            {1, {420,300,520,450}, {6.0, 5.0}, 0.1}
        };
        printTracks(tracker.update(dets, 0.1));
    }

    // Frame 3: camera1에서 사람A가 경계 쪽으로 더 이동 -> 여전히 IoU 매칭, ID1 유지
    // (bbox는 프레임당 30px 수준으로 이동 - 검출 주기 대비 현실적인 이동폭.
    //  world 좌표만 경계 근처로 앞당겨 다음 프레임의 핸드오버 테스트를 준비함)
    std::cout << "\n[Frame 3] t=0.2s, camera1 사람A 경계 접근\n";
    {
        std::vector<Detection> dets = {
            {1, {460,300,560,450}, {9.0, 5.0}, 0.2}
        };
        printTracks(tracker.update(dets, 0.2));
    }

    // Frame 4: 핸드오버! camera1은 더 이상 못 봄(구역 이탈), camera2가 근접한 world좌표에서 검출
    //          -> 같은 카메라가 아니므로 IoU 불가, world 거리(0.5m < 1.0m 임계값)로 ID1 승계 예상
    std::cout << "\n[Frame 4] t=0.3s, 핸드오버 발생 (camera1 -> camera2)\n";
    {
        std::vector<Detection> dets = {
            {2, {50,300,150,450}, {9.5, 5.0}, 0.3}   // camera2 자체 픽셀좌표는 camera1과 다름
        };
        printTracks(tracker.update(dets, 0.3));
    }

    // Frame 5: camera2에서 사람A 계속 이동 -> 이제 동일 카메라(2)이므로 IoU 매칭, ID1 유지
    std::cout << "\n[Frame 5] t=0.4s, camera2에서 사람A 계속 추적\n";
    {
        std::vector<Detection> dets = {
            {2, {70,300,170,450}, {10.5, 5.0}, 0.4}
        };
        printTracks(tracker.update(dets, 0.4));
    }

    // Frame 6: camera1에 전혀 다른 사람B 등장 -> 매칭 대상 없음 -> 신규 트랙(ID2) 예상
    std::cout << "\n[Frame 6] t=0.5s, camera1에 사람B 신규 등장\n";
    {
        std::vector<Detection> dets = {
            {1, {200,200,300,350}, {2.0, 8.0}, 0.5},  // 사람B
            {2, {90,300,190,450}, {11.5, 5.0}, 0.5}   // 사람A는 camera2에서 계속 이동 중
        };
        printTracks(tracker.update(dets, 0.5));
    }

    // Frame 7~9: 사람B가 오검출 등으로 3프레임 연속 사라짐 (아직 소멸 임계값 5 미만 -> 유지)
    for (int i = 7; i <= 9; ++i) {
        double t = 0.5 + (i - 6) * 0.1;
        std::cout << "\n[Frame " << i << "] t=" << std::fixed << std::setprecision(1) << t
                  << "s, 사람B 미검출 (오검출/가림 가정)\n";
        std::vector<Detection> dets = {
            {2, {90.0 + (i-6)*20, 300,190.0 + (i-6)*20,450}, {11.5 + (i-6)*1.0, 5.0}, t} // 사람A만 계속 검출
        };
        printTracks(tracker.update(dets, t));
    }

    // Frame 10~12: 사람B가 6프레임 넘게 사라진 상태 -> missed_frames > 5 -> 트랙 소멸되어야 함
    for (int i = 10; i <= 12; ++i) {
        double t = 0.5 + (i - 6) * 0.1;
        std::cout << "\n[Frame " << i << "] t=" << std::fixed << std::setprecision(1) << t
                  << "s, 사람B 계속 미검출\n";
        std::vector<Detection> dets = {
            {2, {90.0 + (i-6)*20, 300,190.0 + (i-6)*20,450}, {11.5 + (i-6)*1.0, 5.0}, t}
        };
        printTracks(tracker.update(dets, t));
    }

    // Frame 13: camera1에 다시 사람이 나타남 -> 소멸된 지 오래라 새 ID(ID3) 발급되어야 함
    std::cout << "\n[Frame 13] t=1.2s, camera1에 사람 재등장 (트랙 소멸 후)\n";
    {
        std::vector<Detection> dets = {
            {1, {210,205,310,355}, {2.1, 8.1}, 1.2},
            {2, {370,300,470,450}, {17.5, 5.0}, 1.2}
        };
        printTracks(tracker.update(dets, 1.2));
    }

    std::cout << "\n=== 테스트 종료 ===\n";
    return 0;
}
