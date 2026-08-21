// cross_camera_reid.cpp
// 카메라 간 객체 ID 유지 (크로스카메라 Re-ID) - 구현
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 목적: 카메라 핸드오버(A -> B) 시 동일 인물에게 동일 tracking ID를 유지시킨다.
//   - 기본: 같은 카메라 내 프레임 간 연속성은 IoU(bbox 겹침 비율)로 매칭
//   - 카메라 전환(핸드오버) 시점: world 좌표(호모그래피 변환 결과) 근접도로 매칭
//   - [보강] 같은 카메라라도 검출 주기가 낮거나 이동이 빨라 IoU가 깨질 경우를 대비해,
//           world 좌표 근접도를 항상 fallback으로 함께 확인한다 (둘 중 더 확실한 신호 채택).
//
// Detection.world는 main.cpp에서 stream별 호모그래피로 변환한 mm 좌표다.
// 변환에 실패한 검출은 이 트래커에 넣지 않아 임의 좌표가 매칭을 오염시키지 않게 한다.
//
// 데이터 구조/공개 인터페이스 선언은 cross_camera_reid.h 참고.

#include "logic/tracking/cross_camera_reid.h"
#include "logging/logger.hpp"

#include <algorithm>
#include <cmath>

// ============================================================
// 1. 매칭 점수 계산 헬퍼
// ============================================================

double iou(const BoundingBox& a, const BoundingBox& b) {
    // BoundingBox 필드(left/top/right/bottom)는 float(ONVIF 파서 원본 타입)이므로
    // 아래 계산 전체를 double로 통일해서 진행한다.
    double aLeft = a.left, aTop = a.top, aRight = a.right, aBottom = a.bottom;
    double bLeft = b.left, bTop = b.top, bRight = b.right, bBottom = b.bottom;

    double ix1 = std::max(aLeft, bLeft);
    double iy1 = std::max(aTop, bTop);
    double ix2 = std::min(aRight, bRight);
    double iy2 = std::min(aBottom, bBottom);
    double iw = std::max(0.0, ix2 - ix1);
    double ih = std::max(0.0, iy2 - iy1);
    double inter = iw * ih;

    double areaA = std::max(0.0, aRight - aLeft) * std::max(0.0, aBottom - aTop);
    double areaB = std::max(0.0, bRight - bLeft) * std::max(0.0, bBottom - bTop);
    double uni = areaA + areaB - inter;

    if (uni <= 0.0) return 0.0;
    return inter / uni;
}

// ============================================================
// 2. 크로스카메라 트래커
// ============================================================

CrossCameraTracker::CrossCameraTracker(double iou_threshold,
                                       double world_distance_threshold_mm,
                                       double track_timeout_sec)
    : iou_threshold_(iou_threshold),
      world_distance_threshold_mm_(world_distance_threshold_mm),
      track_timeout_sec_(track_timeout_sec) {}

std::vector<Track> CrossCameraTracker::update(const std::vector<Detection>& detections, double now_s) {
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

            if (t.stream_id == d.stream_id) {
                // 동일 스트림: IoU 우선 (프레임 간 연속성, bbox 비교 가능)
                double v = iou(t.last_bbox, d.bbox);
                if (v >= iou_threshold_) iou_score = v;
            }

            // world 거리는 카메라 동일 여부와 무관하게 항상 계산 가능
            // (호모그래피로 이미 공통 좌표계로 변환된 값이므로)
            // -> 동일 카메라인데 검출 주기가 느리거나 이동이 빨라 IoU가 깨진 경우의 fallback,
            //    그리고 카메라 전환(핸드오버) 시의 유일한 매칭 수단
            double dist = euclideanDistance(t.last_world, d.world);
            if (dist <= world_distance_threshold_mm_) {
                world_score = 1.0 - (dist / world_distance_threshold_mm_);
            }

            if (iou_score < 0.0 && world_score < 0.0) continue; // 둘 다 임계값 밖 -> 매칭 후보 아님

            bool is_handover = (t.stream_id != d.stream_id);
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
            LOG_DEBUG("CCTV", "사람 트랙 핸드오버 (track_id=" + std::to_string(t.track_id) +
                               ", " + t.stream_id + " -> " + d.stream_id +
                               ", world 거리=" + std::to_string(euclideanDistance(t.last_world, d.world)) +
                               "mm)");
        }

        t.stream_id   = d.stream_id;
        t.camera_id   = d.camera_id;
        t.channel     = d.channel;
        t.last_bbox   = d.bbox;
        t.last_world  = d.world;
        t.last_seen_s = now_s;
        t.missed_frames = 0;
    }

    // ── 3) 매칭 안 된 기존 트랙: missed_frames 증가(로그용), 시간 초과 시 소멸 ──
    for (size_t ti = 0; ti < tracks_.size(); ++ti) {
        if (!track_matched[ti]) {
            tracks_[ti].missed_frames++;
        }
    }
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
            [&](const Track& t) {
                bool expired = (now_s - t.last_seen_s) > track_timeout_sec_;
                if (expired) {
                    LOG_DEBUG("CCTV", "사람 트랙 만료 (track_id=" + std::to_string(t.track_id) +
                                       ", " + std::to_string(now_s - t.last_seen_s) +
                                       "초 연속 미검출)");
                }
                return expired;
            }),
        tracks_.end());

    // ── 4) 매칭 안 된 검출: 새 트랙(새 ID) 생성 ────────────────
    for (size_t di = 0; di < detections.size(); ++di) {
        if (!det_matched[di]) {
            Track nt;
            nt.track_id      = next_id_++;
            nt.stream_id     = detections[di].stream_id;
            nt.camera_id     = detections[di].camera_id;
            nt.channel       = detections[di].channel;
            nt.last_bbox     = detections[di].bbox;
            nt.last_world    = detections[di].world;
            nt.last_seen_s   = now_s;
            LOG_DEBUG("CCTV", "사람 트랙 생성 (track_id=" + std::to_string(nt.track_id) +
                               ", stream: " + nt.stream_id + ")");
            tracks_.push_back(nt);
        }
    }

    return tracks_;
}
