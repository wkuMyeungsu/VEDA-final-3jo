#include <chrono>
#include <initializer_list>
#include <iostream>
#include <string>

#include "logic/tracking/marker_channel_tracker.hpp"

namespace {

using Clock = MarkerStreamTracker::Clock;
using std::chrono::milliseconds;
constexpr int kMarker = 1;
constexpr int kConfirm = 3;
constexpr auto kGrace = milliseconds(500);
const std::string kStream1 = "CAM_01_CH_01";
const std::string kStream2 = "CAM_02_CH_03";
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "실패: " << message << '\n';
        ++failures;
    }
}

ArucoFrame frameOn(const std::string& stream_id, std::initializer_list<int> marker_ids) {
    ArucoFrame frame;
    frame.stream_id = stream_id;
    frame.camera_id = stream_id.substr(0, 6);
    for (const int id : marker_ids) {
        ArucoMarker marker;
        marker.id = id;
        frame.markers.push_back(marker);
    }
    return frame;
}

Clock::time_point at(Clock::time_point base, int ms) {
    return base + milliseconds(ms);
}

void confirm(MarkerStreamTracker& tracker, const std::string& stream, Clock::time_point base,
             int start_ms = 0) {
    for (int i = 0; i < kConfirm; ++i) {
        tracker.onArucoFrame(frameOn(stream, {kMarker}), at(base, start_ms + i * 30));
    }
}

void testStreamIdentityAndConfirmation() {
    MarkerStreamTracker tracker(kMarker, kConfirm, kGrace);
    const auto t0 = Clock::now();

    check(!tracker.onArucoFrame(frameOn(kStream1, {kMarker}), at(t0, 0)),
          "stream별 첫 프레임은 미확정");
    check(!tracker.onArucoFrame(frameOn(kStream1, {kMarker}), at(t0, 30)),
          "stream별 두 번째 프레임은 미확정");
    check(!tracker.activeStream(), "확정 전 active stream 없음");
    check(!tracker.onArucoFrame(frameOn(kStream1, {}), at(t0, 60)),
          "미검출이면 stream streak 리셋");
    check(tracker.streakOf(kStream1) == 0, "stream별 streak 리셋");

    confirm(tracker, kStream1, t0, 90);
    const auto active = tracker.activeStream();
    check(active && *active == kStream1, "stream_id로 첫 담당 stream 확정");
    check(!tracker.onArucoFrame(frameOn(kStream1, {kMarker}), at(t0, 200)),
          "같은 stream은 재전환하지 않음");

    MarkerStreamTracker other(kMarker, kConfirm, kGrace);
    confirm(other, kStream1, t0);
    check(other.activeStream() && *other.activeStream() == kStream1,
          "다른 stream의 동일 channel 번호와 독립적으로 추적");
}

void testHandoverUsesStreamId() {
    MarkerStreamTracker tracker(kMarker, kConfirm, kGrace);
    const auto t0 = Clock::now();
    confirm(tracker, kStream1, t0);

    for (int i = 0; i < kConfirm; ++i) {
        check(!tracker.onArucoFrame(frameOn(kStream2, {kMarker}), at(t0, 100 + i * 30)),
              "유예 중 후보 stream은 전환하지 않음");
    }
    check(tracker.activeStream() && *tracker.activeStream() == kStream1,
          "유예 중 active stream 유지");

    const auto switched = tracker.onArucoFrame(frameOn(kStream2, {kMarker}), at(t0, 700));
    check(switched && *switched == kStream2, "유예 후 새 stream_id로 전환");
    check(tracker.streakOf(kStream2) >= kConfirm, "새 stream streak 유지");

    tracker.reset();
    check(!tracker.activeStream() && tracker.streakOf(kStream2) == 0,
          "reset 후 stream 상태 초기화");
}

}  // namespace

int main() {
    testStreamIdentityAndConfirmation();
    testHandoverUsesStreamId();
    return failures == 0 ? 0 : 1;
}
