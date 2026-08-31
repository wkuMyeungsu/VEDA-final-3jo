#pragma once

#include <chrono>

#include "logic/judgment/danger_judgment_engine.h"

// 서버 내부 판정과 Qt 단말/관제 UI 계약을 분리한다.
//
// Qt(운전자 단말 RiskHud/EdgeWarningFrame, 관제 RiskBanner/AlertList)는
// exception_state != NONE 이면 risk_level 을 무시하고 경보 UI를 켠다.
// DEAD_RECKONING 은 "자율 항법" / UNKNOWN 으로 표시되므로, 마커가 잠깐 빠질 때마다
// 운전자·관제 화면이 깜빡인다. 내부 엔진은 예외를 그대로 두되, MQTT로 나가는 값만
// 여기서 공지용으로 다듬는다. JSON 필드명/타입은 바꾸지 않는다.
class AnnouncementGate {
public:
    using Clock = std::chrono::steady_clock;

    explicit AnnouncementGate(std::chrono::milliseconds rise_confirm = std::chrono::milliseconds(400),
                              std::chrono::milliseconds fall_confirm = std::chrono::milliseconds(1500))
        : rise_confirm_ms_(rise_confirm), fall_confirm_ms_(fall_confirm) {}

    void reset() {
        has_published_ = false;
        published_risk_ = RiskLevel::SAFE;
        published_exception_ = ExceptionState::NONE;
        pending_risk_ = RiskLevel::SAFE;
        pending_since_ = Clock::time_point{};
    }

    JudgmentResult apply(JudgmentResult incoming, Clock::time_point now = Clock::now()) {
        sanitizeForClients(incoming);

        if (!has_published_) {
            publish(incoming);
            return incoming;
        }

        const int incoming_level = static_cast<int>(incoming.final_risk);
        const int published_level = static_cast<int>(published_risk_);
        if (incoming_level == published_level) {
            pending_risk_ = incoming.final_risk;
            pending_since_ = now;
            incoming.final_risk = published_risk_;
            incoming.exception = holdException(incoming.exception);
            return incoming;
        }

        const bool rising = incoming_level > published_level;
        const auto confirm = rising ? riseDelay(incoming.final_risk) : fall_confirm_ms_;
        if (pending_risk_ != incoming.final_risk) {
            pending_risk_ = incoming.final_risk;
            pending_since_ = now;
        }
        if (now - pending_since_ < confirm) {
            incoming.final_risk = published_risk_;
            incoming.exception = holdException(incoming.exception);
            return incoming;
        }

        publish(incoming);
        return incoming;
    }

    RiskLevel publishedRisk() const { return published_risk_; }

private:
    static void sanitizeForClients(JudgmentResult& incoming) {
        if (incoming.exception != ExceptionState::DEAD_RECKONING) return;
        incoming.exception = ExceptionState::NONE;
        // 거리를 한 번도 못 잰 폐색은 엔진이 최소 CAUTION을 건다. Qt는 그걸
        // "자율 항법" 경보로 보여 주므로, 공지에는 SAFE로 둔다. 측정값이 있으면
        // 엔진이 이미 직전 단계를 유지한다.
        if (incoming.distance_mm < 0.0 && incoming.final_risk == RiskLevel::CAUTION)
            incoming.final_risk = RiskLevel::SAFE;
    }

    std::chrono::milliseconds riseDelay(RiskLevel incoming) const {
        if (incoming == RiskLevel::EMERGENCY) return std::chrono::milliseconds(0);
        return rise_confirm_ms_;
    }

    ExceptionState holdException(ExceptionState incoming) const {
        // 공지 단계가 SAFE로 붙들려 있는 동안에는 예외만 바꿔서 HUD를 켜지 않는다.
        if (published_risk_ == RiskLevel::SAFE) return published_exception_;
        return incoming;
    }

    void publish(const JudgmentResult& incoming) {
        has_published_ = true;
        published_risk_ = incoming.final_risk;
        published_exception_ = incoming.exception;
        pending_risk_ = incoming.final_risk;
    }

    std::chrono::milliseconds rise_confirm_ms_;
    std::chrono::milliseconds fall_confirm_ms_;
    bool has_published_ = false;
    RiskLevel published_risk_ = RiskLevel::SAFE;
    ExceptionState published_exception_ = ExceptionState::NONE;
    RiskLevel pending_risk_ = RiskLevel::SAFE;
    Clock::time_point pending_since_{};
};
