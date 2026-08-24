// marker_channel_tracker.hpp
// 지게차 마커가 보이는 stream 추적 - 공개 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 목적: "지금 지게차가 어느 stream에 보이는가"만 판단한다. 카메라 N대의
//       stream M개에서 도착하는 ArUco 프레임을 먹고, 액티브 stream이 실제로
//       바뀌는 순간에만 새 stream_id를 돌려준다.
//       바뀌지 않으면 아무 신호도 내지 않는다(nullopt) - 호출부가 매 프레임
//       sendCameraAssignment()를 때리지 않도록 하는 게 이 클래스의 존재 이유다.
//
// 범위 밖:
//   - 실제 전송은 중앙 MQTT assignment 발행기가 맡는다.
//   - 사람 추적/거리 판정은 nearest_person_selector / DangerJudgmentEngine 몫이다.
//     여기서는 마커가 "보이냐 안 보이냐"만 본다(좌표는 안 쓴다).
//
// ── 판정 규칙 ────────────────────────────────────────────────
//   1) stream별로 마커 연속 검출 프레임 수(streak)를 센다.
//   2) streak >= confirm_frames 인 stream을 "확정 후보"로 본다.
//   3) 액티브 stream이 아직 없으면, 확정되는 즉시 그 stream을 액티브로 잡는다.
//   4) 액티브가 있으면, 액티브에서 마커를 마지막으로 본 지 lost_grace를 넘겼고
//      다른 stream이 확정 후보일 때만 전환한다.
//
// ── 왜 이런 비대칭 구조인가 (히스테리시스) ──────────────────
//   DangerJudgmentEngine::classifyByDistance()의 EMERGENCY 래치와 같은 원칙이다.
//   그쪽은 "위험도가 올라가는 방향은 즉시, 내려오는 방향만 신중하게"를 거리 축에서
//   적용했고, 여기서는 시간 축에서 "액티브를 새로 잡는 건 즉시(3번), 잡고 있던 걸
//   놓는 건 신중하게(4번)" 적용한다. 마커가 한두 프레임 가려졌다고 운전석 화면이
//   왔다갔다하면 운전자가 볼 수 없는 화면이 된다.
//
//   marker_id, confirm_frames, lost_grace_ms는 운영 설정 파일에서 읽는다.
//
// ── 동시성 ────────────────────────────────────────────────────
//   ArUco 프레임은 카메라별 비동기 스레드에서 들어올 수 있으므로, 내부 상태를
//   CameraAssignmentServer.terminals_와 같은 방식(mutable std::mutex)으로 보호한다.
//   DangerJudgmentEngine 쪽 히스테리시스가 "단일 스레드에서만 호출"을 전제로 두고
//   락을 안 쓰는 것과 다른 점이다(그쪽은 판정 루프가 하나뿐이라 그래도 됐다).
//
// 구현은 marker_channel_tracker.cpp에 있다. 표준 헤더 + ArucoFrame 타입 정의만 쓰므로
// POSIX 의존성이 없다.

#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <optional>

#include "input/aruco_metadata_parser.hpp"  // ArucoFrame (타입 정의만 사용, 링크 의존성 아님)

class MarkerStreamTracker {
public:
    // 시각 비교는 전부 단조 시계로 한다. 프레임에 실린 카메라 UtcTime은 카메라별로
    // 오차가 있고(main.cpp의 delta_ms 주석 참고) NTP 보정으로 뒤로 튈 수도 있어서,
    // "유예 시간이 지났는가" 같은 경과시간 판단에 쓰면 안 된다.
    using Clock = std::chrono::steady_clock;

    // marker_id     : 이 인스턴스가 담당하는 지게차의 marker ID
    // confirm_frames: 후보 확정에 필요한 연속 검출 프레임 수 (1 이상)
    // lost_grace    : 액티브 카메라가 마커를 놓쳐도 유지해주는 시간
    MarkerStreamTracker(int marker_id, int confirm_frames, std::chrono::milliseconds lost_grace);

    // ArUco 프레임 한 장 반영. 액티브 stream이 실제로 바뀌었을 때만 새 stream_id를 돌려준다.
    // stream_id가 비어 있으면 무시하고 nullopt.
    std::optional<std::string> onArucoFrame(const ArucoFrame& frame);

    // 위와 같되 "지금 시각"을 주입한다. 테스트가 lost_grace 경과를 실제로 기다리지
    // 않고 검증할 수 있게 분리했다(실시간 sleep이 들어간 테스트는 느리고 불안정하다).
    std::optional<std::string> onArucoFrame(const ArucoFrame& frame, Clock::time_point now);

    // 프레임 타입에 의존하지 않는 하위 API. 위 두 함수가 이걸로 귀결된다.
    // stream_id   : 전역 stream 식별자
    // marker_seen : 이번 프레임에 추적 대상 마커가 보였는지
    std::optional<std::string> update(const std::string& stream_id, bool marker_seen,
                                      Clock::time_point now);

    // 현재 액티브 stream. 아직 아무 stream도 확정되지 않았으면 nullopt.
    std::optional<std::string> activeStream() const;

    // stream별 연속 검출 수 (디버깅·테스트용). 모르는 stream이면 0.
    int streakOf(const std::string& stream_id) const;

    // 모든 상태 초기화 (액티브 해제 + streak 리셋).
    // DangerJudgmentEngine::resetHysteresis()와 같은 성격의 탈출구다.
    void reset();

    int markerId() const { return marker_id_; }
    int confirmFrames() const { return confirm_frames_; }
    std::chrono::milliseconds lostGrace() const { return lost_grace_; }

private:
    struct StreamState {
        int streak = 0;                 // 연속 검출 프레임 수 (미검출 프레임에서 0으로 리셋)
        Clock::time_point last_seen{};  // 마커가 마지막으로 보인 시각 (한 번도 없으면 epoch)
        bool ever_seen = false;
    };

    bool isConfirmed(const StreamState& s) const { return s.streak >= confirm_frames_; }

    const int marker_id_;
    const int confirm_frames_;
    const std::chrono::milliseconds lost_grace_;

    // 전환 직후의 짧은 실명은 정상 경계 상황(두 화면이 같은 마커를 간헐 동시 검출)에서
    // 되걸림을 유발한다 -> 전환 후 이 시간 동안은 추가 전환을 금지한다(최소 유지 시간).
    // ponytail: lost_grace x10(현재 설정에서 5초). 정상 핸드오버 지연이 운영에서
    // 문제되면
    // handover.switch_cooldown_ms 설정 항목으로 승격할 것.
    std::chrono::milliseconds switch_cooldown_;
    Clock::time_point last_switch_{};

    mutable std::mutex mtx_;
    std::map<std::string, StreamState> streams_;
    std::optional<std::string> active_;
};
