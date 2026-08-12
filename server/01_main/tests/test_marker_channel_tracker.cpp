// test_marker_channel_tracker.cpp
// MarkerChannelTracker(지게차 마커 -> 액티브 카메라 채널 판정) 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   [테스트 1] confirm_frames 미달이면 전환 신호가 없다 (연속성이 끊기면 처음부터 다시)
//   [테스트 2] confirm_frames를 채우면 전환되고, 그 뒤로는 같은 채널에 재신호가 없다
//   [테스트 3] lost_grace_ms 이내의 순간 미검출로는 전환되지 않는다
//   [테스트 4] lost_grace_ms 초과 + 다른 카메라 확정이면 그때 전환된다
//              (초과했어도 후보가 미확정이면 전환되지 않는 것도 같이 본다)
//
// 시간은 MarkerChannelTracker::update()의 now 인자로 주입한다. 실제로 500ms를
// 기다리면 테스트가 느려지고 CI 부하에 따라 결과가 흔들려서, 단조 시계 값을 직접
// 만들어 넘긴다(그래서 이 테스트에는 sleep이 하나도 없다).
//
// 빌드/실행: ctest -R marker_channel_tracker_test --output-on-failure
//            (또는 ./build/tests/test_marker_channel_tracker, 종료코드 0=성공)

#include <chrono>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>

#include "logic/tracking/marker_channel_tracker.hpp"

namespace {

using Clock = MarkerChannelTracker::Clock;
using std::chrono::milliseconds;

constexpr int kForkliftMarkerId = 0;   // 설정 스키마의 forklift.marker_id 기본값
constexpr int kConfirmFrames    = 3;   // handover.confirm_frames
constexpr auto kLostGrace       = milliseconds(500);  // handover.lost_grace_ms

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

// 채널 channel에서 marker_ids가 보인 ArUco 프레임 한 장.
// 좌표는 이 클래스가 쓰지 않으므로 채우지 않는다.
ArucoFrame frameOn(int channel, std::initializer_list<int> marker_ids) {
    ArucoFrame f;
    f.channel = channel;
    for (int id : marker_ids) {
        ArucoMarker m;
        m.id = id;
        f.markers.push_back(m);
    }
    return f;
}

std::string toText(const std::optional<int>& v) {
    return v ? std::to_string(*v) : std::string("없음");
}

// 테스트 시작 시각 기준으로 ms만큼 지난 시점.
Clock::time_point at(Clock::time_point base, int ms) {
    return base + milliseconds(ms);
}

// confirm_frames를 못 채우면 액티브가 잡히지 않아야 한다.
void testBelowConfirmFramesDoesNotSwitch() {
    std::cout << "[테스트 1] confirm_frames 미달 -> 전환 없음\n";

    MarkerChannelTracker tracker(kForkliftMarkerId, kConfirmFrames, kLostGrace);
    const auto t0 = Clock::now();

    check(!tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 0)).has_value(),
          "1프레임째: 신호 없음");
    check(!tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 30)).has_value(),
          "2프레임째: 신호 없음 (confirm_frames=3 미달)");
    check(!tracker.activeChannel().has_value(), "아직 액티브 카메라 없음");

    // 미검출 프레임이 한 장 끼면 연속성이 끊겨 streak이 0으로 돌아가야 한다.
    check(!tracker.onArucoFrame(frameOn(1, {}), at(t0, 60)).has_value(), "미검출 프레임: 신호 없음");
    check(tracker.streakOf(1) == 0, "미검출 프레임에서 연속 카운트 리셋 (실제: " +
                                        std::to_string(tracker.streakOf(1)) + ")");

    // 리셋됐으니 여기서 두 장을 더 봐도 여전히 2/3라 확정되면 안 된다.
    check(!tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 90)).has_value(),
          "리셋 후 1프레임째: 신호 없음");
    check(!tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 120)).has_value(),
          "리셋 후 2프레임째: 신호 없음");
    check(!tracker.activeChannel().has_value(), "연속성이 끊긴 뒤에도 액티브 없음");

    // 다른 마커만 보이는 프레임은 이 지게차와 무관하므로 카운트되면 안 된다.
    MarkerChannelTracker other(kForkliftMarkerId, kConfirmFrames, kLostGrace);
    for (int i = 0; i < kConfirmFrames + 2; ++i) {
        other.onArucoFrame(frameOn(1, {7, 9}), at(t0, i * 30));
    }
    check(!other.activeChannel().has_value(), "추적 대상이 아닌 마커 ID는 무시됨");
}

// confirm_frames를 채우면 그 순간 전환되고, 그 뒤로는 신호가 반복되지 않아야 한다.
void testConfirmFramesSwitches() {
    std::cout << "\n[테스트 2] confirm_frames 충족 -> 전환\n";

    MarkerChannelTracker tracker(kForkliftMarkerId, kConfirmFrames, kLostGrace);
    const auto t0 = Clock::now();

    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 0));
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 30));
    auto third = tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 60));

    check(third.has_value() && *third == 1,
          "3프레임째에 channel 1로 전환 신호 (실제: " + toText(third) + ")");
    check(tracker.activeChannel().has_value() && *tracker.activeChannel() == 1,
          "액티브 카메라 = 1");

    // 액티브가 계속 보이는 동안에는 아무 신호도 없어야 한다
    // (호출부가 매 프레임 sendCameraAssignment()를 때리지 않게 하는 게 이 클래스의 목적).
    check(!tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 90)).has_value(),
          "같은 채널이 계속 보이는 동안 재신호 없음");
    check(!tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 120)).has_value(),
          "같은 채널 재신호 없음 (2)");

    tracker.reset();
    check(!tracker.activeChannel().has_value(), "reset() 후 액티브 해제");
    check(tracker.streakOf(1) == 0, "reset() 후 연속 카운트도 0");
}

// 액티브가 잠깐 마커를 놓쳤다고(유예 시간 이내) 전환되면 안 된다.
void testMomentaryLossWithinGraceKeepsActive() {
    std::cout << "\n[테스트 3] lost_grace_ms 이내 순간 미검출 -> 전환 없음\n";

    MarkerChannelTracker tracker(kForkliftMarkerId, kConfirmFrames, kLostGrace);
    const auto t0 = Clock::now();

    // channel 1을 액티브로 확정 (마지막 검출 시각 = t0+60)
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 0));
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 30));
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 60));

    // channel 2가 확정될 만큼(3프레임) 보이지만, 아직 유예 시간(500ms) 안이다.
    bool switched = false;
    switched |= tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 100)).has_value();
    switched |= tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 130)).has_value();
    switched |= tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 160)).has_value();

    check(tracker.streakOf(2) >= kConfirmFrames, "channel 2는 확정 조건을 채움");
    check(!switched, "유예 시간 이내에는 다른 카메라가 확정돼도 전환 안 됨");
    check(tracker.activeChannel().has_value() && *tracker.activeChannel() == 1,
          "액티브는 여전히 1 (실제: " + toText(tracker.activeChannel()) + ")");

    // 액티브에서 미검출 프레임이 한 장 들어와도(순간 폐색) 그 자체로는 전환 사유가 아니다.
    check(!tracker.onArucoFrame(frameOn(1, {}), at(t0, 200)).has_value(),
          "액티브의 순간 미검출 프레임 -> 신호 없음");
    check(*tracker.activeChannel() == 1, "순간 미검출 뒤에도 액티브는 1");

    // 액티브가 마커를 다시 보면 유예 시계가 리셋되므로, 그 시점 기준으로 다시 재야 한다.
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 300));
    auto late = tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 700));
    check(!late.has_value(),
          "액티브가 다시 검출되면 유예 시계 리셋 (t0+700은 마지막 검출 t0+300에서 400ms)");
    check(*tracker.activeChannel() == 1, "리셋 덕분에 액티브는 계속 1");
}

// 유예 시간을 넘겼고 다른 카메라가 확정 상태면 그때 전환돼야 한다.
void testSwitchAfterGraceExceeded() {
    std::cout << "\n[테스트 4] lost_grace_ms 초과 + 다른 카메라 확정 -> 전환\n";

    MarkerChannelTracker tracker(kForkliftMarkerId, kConfirmFrames, kLostGrace);
    const auto t0 = Clock::now();

    // channel 1 확정 (마지막 검출 = t0+60), 이후 지게차가 시야를 벗어남
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 0));
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 30));
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 60));
    tracker.onArucoFrame(frameOn(1, {}), at(t0, 100));

    // t0+600 시점이면 유예(500ms)는 이미 지났지만, channel 2는 아직 1프레임뿐이라
    // 확정이 아니다 -> 두 조건이 모두 필요하다는 걸 확인한다.
    auto notConfirmedYet = tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 600));
    check(!notConfirmedYet.has_value(), "유예는 지났지만 후보 미확정 -> 전환 없음 (1/3)");
    check(!tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 620)).has_value(),
          "후보 미확정 -> 전환 없음 (2/3)");

    auto switched = tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 640));
    check(switched.has_value() && *switched == 2,
          "유예 초과 + 후보 확정(3/3) -> channel 2로 전환 (실제: " + toText(switched) + ")");
    check(tracker.activeChannel().has_value() && *tracker.activeChannel() == 2,
          "액티브 카메라 = 2");

    // 전환 후에는 다시 조용해야 한다.
    check(!tracker.onArucoFrame(frameOn(2, {kForkliftMarkerId}), at(t0, 700)).has_value(),
          "전환 직후 같은 채널 재신호 없음");

    // 되돌아오는 방향도 같은 규칙이 적용되는지 (1 -> 2 -> 1)
    tracker.onArucoFrame(frameOn(2, {}), at(t0, 800));
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 1400));
    tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 1430));
    auto back = tracker.onArucoFrame(frameOn(1, {kForkliftMarkerId}), at(t0, 1460));
    check(back.has_value() && *back == 1,
          "반대 방향(2 -> 1) 전환도 같은 규칙으로 동작 (실제: " + toText(back) + ")");
}

}  // namespace

int main() {
    std::cout << "=== MarkerChannelTracker(지게차 마커 채널 추적) 테스트 ===\n\n";

    testBelowConfirmFramesDoesNotSwitch();
    testConfirmFramesSwitches();
    testMomentaryLossWithinGraceKeepsActive();
    testSwitchAfterGraceExceeded();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
