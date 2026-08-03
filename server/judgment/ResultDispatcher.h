#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "danger_judgment_engine.h"

// ResultDispatcher - 판정 결과 송신 정책 (헤더 온리, 소켓 의존성 없음)
//
// [정책 확정 2026-08-03] 하이브리드 전송
//   - 판정 상태가 이전과 다르면: 즉시 전송하고 하트비트 타이머를 리셋
//   - 200ms 동안 상태 변화가 없으면: 마지막 판정 결과를 재전송 (하트비트 겸용)
//   "무조건 200ms 주기 전송"을 쓰지 않는 이유: 위험 상태 전이가 최대 200ms 밀려
//   판정 지연 100ms 목표와 정면으로 충돌한다. 변화는 즉시 내보내고, 링크 생존 확인과
//   값 refresh만 주기 전송이 맡는다.
//
// 재전송은 백그라운드 스레드가 담당하므로 판정 루프는 submit()에서 블로킹되지 않는다
// (submit()은 짧은 락 구간 + 문자열 직렬화만 하고 I/O는 sink 쪽 큐로 넘긴다).
//
// 전송 자체(소켓)는 여기서 다루지 않는다. sink로 ResultPublisher::publish를 넘기면 되고,
// 테스트에서는 람다를 넣어 소켓 없이 정책만 검증할 수 있다.
//
// [추가 2026-08-03] 이벤트 로그 훅(EventSink)
//   여기가 "상태 변화"를 아는 유일한 지점이라 SQLite 이벤트 로그도 이 판단을 재사용한다.
//   하트비트 재전송 경로(run())에서는 절대 부르지 않는다 - 무변화 재전송까지 로그로 남기면
//   같은 상태가 수천 건씩 중복 저장돼 로그가 쓸모없어진다.
//   로거 구현(sqlite3 의존)은 여기 들어오지 않는다. 콜백 시그니처만 두어
//   이 헤더는 계속 표준 헤더만으로 컴파일된다.

namespace risk_transport {

class ResultDispatcher {
public:
    using Sink  = std::function<void(const std::string&)>;
    using Clock = std::chrono::steady_clock;

    // 상태 변화가 실제로 일어났을 때만 호출되는 콜백.
    // prev_risk_level: 직전 상태의 risk_level. 최초 이벤트에는 kNoPreviousRisk(-1)가 들어온다
    //                  (EventLogger::kNoPreviousRisk와 같은 값이며 DB에는 NULL로 저장된다).
    //                  0(SAFE)이 유효한 위험도라서 "직전 없음"을 0으로 표현할 수 없다.
    using EventSink = std::function<void(const JudgmentResult&, int prev_risk_level)>;

    // "직전 상태 없음"(= 최초 이벤트)을 뜻하는 prev_risk_level 값.
    static constexpr int kNoPreviousRisk = -1;

    explicit ResultDispatcher(Sink sink,
                              std::chrono::milliseconds heartbeat_period = std::chrono::milliseconds(200))
        : sink_(std::move(sink)), period_(heartbeat_period) {}

    ~ResultDispatcher() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread(&ResultDispatcher::run, this);
    }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    // 판정 루프에서 프레임마다 호출한다.
    // 상태가 바뀐 경우에만 즉시 전송하고, 그렇지 않으면 마지막 결과만 갱신해 둔다
    // (다음 하트비트가 최신 값을 싣고 나간다).
    void submit(const JudgmentResult& r) {
        bool changed = false;
        int  prev_risk = kNoPreviousRisk;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            changed  = !has_last_ || !sameState(last_, r);
            // last_를 덮어쓰기 전에 직전 위험도를 떠 둔다 (이벤트 로그의 previous_risk_level).
            // has_last_가 false면 직전 상태가 없으므로 sentinel을 그대로 둔다 -> DB에 NULL.
            if (changed && has_last_) prev_risk = static_cast<int>(last_.final_risk);
            last_    = r;
            has_last_ = true;
            if (changed) next_heartbeat_ = Clock::now() + period_;   // 변화 시 타이머 리셋
        }
        if (!changed) return;

        ++change_sends_;
        // 전송이 먼저다. 이벤트 로그가 느려지더라도(디스크 등) 단말로 나가는 시각이
        // 밀리지 않게 순서를 고정한다. event_sink_ 자체도 큐잉만 하고 즉시 반환한다.
        sink_(toJson(r));
        if (event_sink_) event_sink_(r, prev_risk);
        cv_.notify_one();   // 하트비트 스레드가 리셋된 시각 기준으로 다시 자도록 깨운다
    }

    // 상태 변화 이벤트 훅을 등록한다 (예: EventLogger::log).
    // start()/submit() 전에 한 번만 설정하는 것을 전제로 한다 - 동작 중 교체는 지원하지 않는다
    // (ResultPublisher::onStateChange와 같은 규약).
    void onStateChangeEvent(EventSink cb) { event_sink_ = std::move(cb); }

    // 마지막으로 전송한 판정 결과가 있는지 (없으면 하트비트도 나가지 않는다)
    bool hasResult() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return has_last_;
    }

    // 상태 변화로 즉시 나간 누적 건수
    std::size_t changeSendCount() const { return change_sends_.load(); }

    // 무변화 구간에서 주기 재전송으로 나간 누적 건수
    std::size_t heartbeatSendCount() const { return heartbeat_sends_.load(); }

    std::chrono::milliseconds heartbeatPeriod() const { return period_; }

    // "판정 상태가 같은가" 판단 기준.
    //
    // 비교 대상: risk_level / exception_state / camera_id / zone
    //   = 단말이 경보 동작을 결정할 때 쓰는 필드들.
    // distance_m을 일부러 제외한 이유: 사람이 조금만 움직여도 매 프레임 값이 흔들려서
    // 포함하면 사실상 "프레임마다 전송"이 되어 하이브리드 방식의 의미가 없어진다.
    // 거리가 임계값을 넘어가면 risk_level이 바뀌므로 위험 전이는 그대로 즉시 전송되고,
    // 표시용 거리 값은 200ms 하트비트에 최신 값이 실려 나간다.
    static bool sameState(const JudgmentResult& a, const JudgmentResult& b) {
        return a.final_risk == b.final_risk
            && a.exception  == b.exception
            && a.camera_id  == b.camera_id
            && a.zone       == b.zone;
    }

private:
    void run() {
        std::unique_lock<std::mutex> lk(mtx_);
        while (running_) {
            if (!has_last_) {
                // 아직 판정 결과가 한 건도 없으면 재전송할 것도 없다.
                cv_.wait_for(lk, period_);
                continue;
            }
            const auto now = Clock::now();
            if (now < next_heartbeat_) {
                cv_.wait_until(lk, next_heartbeat_);
                continue;
            }

            // utc_time은 toJson()이 호출 시점으로 새로 찍는다. 판정 내용(위험도/예외/거리)은
            // 마지막 결과 그대로이고 시각만 갱신되므로, 단말이 "언제 기준 상태인지"를
            // 하트비트만으로도 알 수 있다.
            const JudgmentResult snapshot = last_;
            next_heartbeat_ = now + period_;

            lk.unlock();
            ++heartbeat_sends_;
            sink_(toJson(snapshot));
            lk.lock();
        }
    }

    Sink sink_;
    EventSink event_sink_;   // 상태 변화 시에만 호출. 미등록이면 로깅 없이 동작한다.
    std::chrono::milliseconds period_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    JudgmentResult last_{};
    bool has_last_ = false;
    Clock::time_point next_heartbeat_{};

    std::atomic<bool> running_{false};
    std::thread worker_;

    std::atomic<std::size_t> change_sends_{0};
    std::atomic<std::size_t> heartbeat_sends_{0};
};

} // namespace risk_transport
