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

    // 최소 유지 시간(switch_cooldown) 도입으로 전환은 활성화 수 초 뒤에만 가능해졌다.
    // 현장의 실제 핸드오버는 분 단위로 일어나므로 시간축도 그에 맞춘다.
    // A는 6초까지 마커를 계속 본 뒤 사라진다.
    for (int ms = 100; ms <= 5900; ms += 100) {
        tracker.onArucoFrame(frameOn(kStream1, {kMarker}), at(t0, ms));
    }
    for (int i = 0; i < kConfirm; ++i) {
        check(!tracker.onArucoFrame(frameOn(kStream2, {kMarker}), at(t0, 6000 + i * 30)),
              "유예 중 후보 stream은 전환하지 않음");
    }
    check(tracker.activeStream() && *tracker.activeStream() == kStream1,
          "유예 중 active stream 유지");

    const auto switched = tracker.onArucoFrame(frameOn(kStream2, {kMarker}), at(t0, 7000));
    check(switched && *switched == kStream2, "유예 후 새 stream_id로 전환");
    check(tracker.streakOf(kStream2) >= kConfirm, "새 stream streak 유지");

    tracker.reset();
    check(!tracker.activeStream() && tracker.streakOf(kStream2) == 0,
          "reset 후 stream 상태 초기화");
}

}  // namespace


// 두 화면이 같은 마커를 경계에서 간헐적으로 동시에 볼 때(현장 최빈 상황),
// 액티브가 잠깐 놓치면 전환 -> 되돌아온 쪽도 잠깐 놓치면 역전 ... 이 반복되어
// 관제 화면이 계속 번갈는다는 버그 보고(#핸드오버 플래핑)의 재현 테스트.
// 양쪽 모두 1초 주기로 마커를 놓친다 - 어느 쪽도 "정말 사라진" 게 아니다.
void testBoundaryFlickerDoesNotOscillate() {
    MarkerStreamTracker tracker(kMarker, kConfirm, kGrace);
    const auto t0 = Clock::now();

    // A 활성화
    confirm(tracker, kStream1, t0);

    // 경계 지점 재현: 두 화면 모두 주기적으로 마커를 놓친다(약 600ms 이상씩).
    // 각 화면이 놓친 동안 상대가 3프레임 연속 검출로 확정되므로,
    // 유예 시간(500ms)만으로는 전환이 계속 양방향으로 반복된다.
    int switches = 0;
    std::string active = kStream1;
    auto step = [&](int ms, const std::string& stream, bool seen) {
        const auto changed = tracker.update(stream, seen, at(t0, ms));
        if (changed && *changed != active) {
            ++switches;
            active = *changed;
        }
    };

    // 타임라인 (ms): A=CH_01, B=CH_03
    //  A 활성화 후, B가 확정되고 A가 500ms 넘게 놓치면 -> B로 전환(정상)
    //  직후 B도 500ms 넘게 놓치고 A가 살아나면 -> A로 역전(플래핑)
    //  이 주기를 30초간 반복한다.
    int ms = 100;
    bool a_is_active = true;
    while (ms <= 30000) {
        // 현재 액티브가 마커를 놓치는 구간: 700ms 동안 상대 프레임만 유입
        const std::string& other = a_is_active ? kStream2 : kStream1;
        for (int i = 0; i < 3; ++i) { step(ms, other, true); ms += 40; }
        step(ms, a_is_active ? kStream1 : kStream2, false); ms += 500;   // 액티브 실명 > grace
        step(ms, other, true);   // 상대 확정 상태에서 -> 전환 발생 지점
        const auto changed = tracker.update(other == kStream1 ? kStream2 : kStream1,
                                            false, at(t0, ms));
        if (changed && *changed != active) { ++switches; active = *changed; }
        ms += 40;
        // 액티브가 바뀌었다
        a_is_active = !a_is_active;
    }

    check(switches >= 1, "정말 오래 놓친 경우 첫 핸드오버는 동작해야 한다");
    check(switches <= 7,
          "경계 간헐 검출 상황에서 관제 전환은 최소 유지 시간 내에 제한돼야 한다 "
          "(30초간 실제: " + std::to_string(switches) + "회)");
}

int main() {
    testStreamIdentityAndConfirmation();
    testHandoverUsesStreamId();
    testBoundaryFlickerDoesNotOscillate();
    return failures == 0 ? 0 : 1;
}
