// marker_channel_tracker.cpp
// 지게차 마커 채널 추적 구현
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 판정 규칙과 히스테리시스 근거는 marker_channel_tracker.hpp 상단에 정리돼 있다.

#include "logic/tracking/marker_channel_tracker.hpp"

#include <algorithm>

MarkerChannelTracker::MarkerChannelTracker(int marker_id, int confirm_frames,
                                           std::chrono::milliseconds lost_grace)
    // confirm_frames가 0 이하면 "첫 프레임에 무조건 전환"이 되어 오검출 한 방에 화면이
    // 튄다. 설정 로더(safety_server_config.cpp)가 이미 1 이상을 강제하지만, 이 클래스를
    // 설정 없이 직접 만드는 호출부(테스트 등)도 있어서 여기서도 바닥을 깐다.
    : marker_id_(marker_id),
      confirm_frames_(std::max(1, confirm_frames)),
      lost_grace_(std::max(std::chrono::milliseconds(0), lost_grace)) {}

std::optional<int> MarkerChannelTracker::onArucoFrame(const ArucoFrame& frame) {
    return onArucoFrame(frame, Clock::now());
}

std::optional<int> MarkerChannelTracker::onArucoFrame(const ArucoFrame& frame,
                                                      Clock::time_point now) {
    // 파서 규약상 channel은 1 이상이다(aruco_metadata_parser.cpp에서 검증됨).
    // 그래도 ArucoFrame의 기본값이 -1이라, 파서를 안 거친 프레임이 들어오면
    // 채널 맵에 -1짜리 유령 항목이 생긴다. 여기서 막는다.
    if (frame.channel < 1) return std::nullopt;

    // 마커 리스트에서 추적 대상 ID만 본다. 좌표는 쓰지 않는다 - 이 클래스는
    // "어느 카메라에 보이는가"만 판단하고, 위치 판정은 판정 계층 몫이다.
    const bool marker_seen =
        std::any_of(frame.markers.begin(), frame.markers.end(),
                    [this](const ArucoMarker& m) { return m.id == marker_id_; });

    return update(frame.channel, marker_seen, now);
}

std::optional<int> MarkerChannelTracker::update(int channel, bool marker_seen,
                                                Clock::time_point now) {
    std::lock_guard<std::mutex> lk(mtx_);

    ChannelState& self = channels_[channel];
    if (marker_seen) {
        ++self.streak;
        self.last_seen = now;
        self.ever_seen = true;
    } else {
        // 연속성이 끊기면 확정 자격도 같이 사라진다. last_seen은 남겨둔다 -
        // 액티브 카메라의 유예 시간을 재는 기준이 바로 이 값이다.
        self.streak = 0;
    }

    // ── 1) 아직 액티브가 없음: 확정되는 즉시 채택 ────────────────
    // 시작 직후에는 유예를 걸 대상 자체가 없으므로 지연시킬 이유가 없다.
    if (!active_) {
        if (!isConfirmed(self)) return std::nullopt;
        active_ = channel;
        return active_;
    }

    // ── 2) 이번 프레임이 액티브 카메라 것: 전환 판단 대상이 아니다 ──
    // (위에서 streak/last_seen은 이미 갱신했으므로 유예 시계는 여기서 리셋된 셈이다.)
    if (*active_ == channel) return std::nullopt;

    // ── 3) 후보가 아직 확정되지 않았으면 전환 없음 ──────────────
    if (!isConfirmed(self)) return std::nullopt;

    // ── 4) 액티브가 유예 시간을 넘겨 마커를 놓쳤을 때만 전환 ────
    auto active_it = channels_.find(*active_);
    if (active_it == channels_.end()) {
        // 액티브는 반드시 이 함수를 통해 등록되므로 도달할 수 없는 경로다.
        // 그래도 여기까지 왔다면 액티브 쪽 근거가 사라진 것이니 후보를 채택한다.
        active_ = channel;
        return active_;
    }

    const ChannelState& act = active_it->second;
    // "마지막으로 마커가 보인 시각"만 본다. 액티브 카메라가 미검출 프레임을 계속
    // 보내는 경우든, 스트림이 통째로 끊겨 프레임이 아예 안 오는 경우든 지게차가
    // 그 화면에 없다는 결론은 같아서 구분하지 않는다.
    if (now - act.last_seen <= lost_grace_) return std::nullopt;

    active_ = channel;
    return active_;
}

std::optional<int> MarkerChannelTracker::activeChannel() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return active_;
}

int MarkerChannelTracker::streakOf(int channel) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = channels_.find(channel);
    return it == channels_.end() ? 0 : it->second.streak;
}

void MarkerChannelTracker::reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    channels_.clear();
    active_.reset();
}
