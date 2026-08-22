#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "logic/judgment/judgment_pipeline.h"
#include "network/result_dispatcher.hpp"

namespace {

int failures = 0;
constexpr const char* kTerminal = "TERM_TEST";
constexpr const char* kStream1 = "CAM_01_CH_01";
constexpr const char* kStream2 = "CAM_02_CH_03";
constexpr const char* kCamera1 = "CAM_01";
constexpr const char* kCamera2 = "CAM_02";
constexpr int kChannel1 = 1;
constexpr int kChannel2 = 3;
const WorldPoint kForklift{5000.0, 5000.0};
const forklift::config::DangerJudgmentConfig kJudgmentConfig{
    3000.0, 1500.0, 400.0, 100.0, 1000.0, 500.0, 2.0};

void check(bool condition, const std::string& message) {
    std::cout << (condition ? "  [OK]   " : "  [FAIL] ") << message << '\n';
    if (!condition) ++failures;
}

NearestPersonResult found(const char* stream_id, const char* camera_id, int channel,
                          const WorldPoint& position) {
    NearestPersonResult result;
    result.found = true;
    result.track_id = 7;
    result.stream_id = stream_id;
    result.camera_id = camera_id;
    result.channel = channel;
    result.position = position;
    result.distance_mm = 999.0;  // 엔진이 좌표로 재계산해야 한다.
    return result;
}

NearestPersonResult notFound() { return {}; }

JudgmentPipeline makePipeline(StubSensorReader& sensors, double radius = 0.0,
                             bool ignore_sensor_input = false) {
    JudgmentPipeline pipeline(kTerminal, sensors, kJudgmentConfig, radius,
                              std::chrono::milliseconds(500), ignore_sensor_input);
    pipeline.setActiveStream(kStream1, kCamera1, kChannel1);
    return pipeline;
}

void testStreamIdentityMapping() {
    std::cout << "[스트림 식별자 매핑]\n";
    StubSensorReader sensors(5000.0);
    auto pipeline = makePipeline(sensors);

    const auto camera = pipeline.toCameraInput(
        kForklift, true, found(kStream1, kCamera1, kChannel1, {5500.0, 5000.0}));
    check(camera.camera_id == kCamera1, "활성 stream의 실제 camera_id를 전달");
    check(camera.person_detected && camera.person.x == 5500.0,
          "stream별 최근접 사람 좌표를 전달");
    check(!pipeline.isCameraIdMismatch(
              found(kStream1, kCamera1, kChannel1, {5500.0, 5000.0})),
          "동일 stream은 mismatch가 아님");
    check(pipeline.isCameraIdMismatch(
              found(kStream2, kCamera2, kChannel2, {5500.0, 5000.0})),
          "다른 stream은 mismatch로 식별");
    check(!pipeline.toCameraInput(kForklift, true, notFound()).person_detected,
          "사람 미검출은 person_detected=false");
}

void testRiskAndJson() {
    std::cout << "\n[위험 판정 및 JSON]\n";
    StubSensorReader sensors(5000.0);
    auto pipeline = makePipeline(sensors);

    const auto danger = pipeline.processFrame(
        kForklift, true, found(kStream1, kCamera1, kChannel1, {5500.0, 5500.0}));
    check(danger.result.final_risk == RiskLevel::DANGER, "707mm는 DANGER");
    check(std::abs(danger.result.distance_mm - std::sqrt(500000.0)) < 1e-6,
          "거리 입력값이 아니라 world 좌표로 재계산");
    check(danger.result.camera_id == kCamera1 && danger.result.terminal_id == kTerminal,
          "카메라·단말 식별자를 결과에 보존");
    check(toJson(danger.result).find("\"camera_id\":\"CAM_01\"") != std::string::npos,
          "JSON에 실제 camera_id를 직렬화");
    check(toJson(danger.result).find("\"terminal_id\":\"TERM_TEST\"") != std::string::npos,
          "JSON에 terminal_id를 직렬화");

    const auto absent = pipeline.processFrame(kForklift, true, notFound());
    check(absent.result.final_risk == RiskLevel::SAFE &&
              absent.result.distance_mm == -1.0,
          "사람 미검출은 거리 미확정 SAFE");
}

void testDeviceRadiusAndHandover() {
    std::cout << "\n[단말별 충돌 반경 및 stream 핸드오버]\n";
    StubSensorReader sensors(5000.0);
    auto radius_pipeline = makePipeline(sensors, 200.0);
    const auto adjusted = radius_pipeline.processFrame(
        kForklift, true, found(kStream1, kCamera1, kChannel1, {6600.0, 5000.0}));
    check(adjusted.result.distance_mm == 1600.0 &&
              adjusted.result.final_risk == RiskLevel::DANGER,
          "TERM별 collision_radius는 판정에만 적용하고 원시 거리는 보존");

    auto pipeline = makePipeline(sensors);
    const auto emergency = pipeline.processFrame(
        kForklift, true, found(kStream1, kCamera1, kChannel1, {5300.0, 5000.0}));
    check(emergency.result.final_risk == RiskLevel::EMERGENCY,
          "첫 stream에서 EMERGENCY 래치 진입");
    pipeline.setActiveStream(kStream2, kCamera2, kChannel2);
    const auto after_handover = pipeline.processFrame(
        kForklift, true, found(kStream2, kCamera2, kChannel2, {10000.0, 10000.0}));
    check(pipeline.activeStreamId() == kStream2 && pipeline.activeCameraId() == kCamera2 &&
              pipeline.activeChannel() == kChannel2,
          "핸드오버 시 stream/camera/channel을 함께 갱신");
    check(after_handover.result.final_risk == RiskLevel::SAFE,
          "핸드오버 시 이전 stream의 EMERGENCY 히스테리시스를 리셋");
}

void testSensorExcludedMode() {
    std::cout << "\n[센서 제외 테스트 모드]\n";
    // ToF라면 DANGER가 될 값이지만, 센서 제외 모드에서는 카메라 거리만 반영한다.
    StubSensorReader sensors(100.0);
    auto pipeline = makePipeline(sensors, 0.0, true);
    const auto result = pipeline.processFrame(
        kForklift, true, found(kStream1, kCamera1, kChannel1, {8500.0, 5000.0}));
    check(result.result.camera_risk == RiskLevel::SAFE,
          "센서 제외 모드에서 카메라 원거리 판정은 SAFE");
    check(result.result.tof_risk == RiskLevel::SAFE && result.result.exception == ExceptionState::NONE,
          "센서 제외 모드에서 ToF 위험도와 센서 예외를 판정하지 않음");
    check(result.result.final_risk == RiskLevel::SAFE,
          "센서 제외 모드에서 센서값 때문에 위험도가 상승하지 않음");
}

void testAlertHookOnlyRunsOnStateTransition() {
    std::cout << "\n[ALERT 로그 훅 상태 전이 정책]\n";
    std::vector<std::pair<RiskLevel, RiskLevel>> transitions;
    risk_transport::ResultDispatcher dispatcher(
        [](const std::string&) {}, std::chrono::hours(1));
    dispatcher.onAlert([&](const JudgmentResult& previous, const JudgmentResult& current) {
        transitions.emplace_back(previous.final_risk, current.final_risk);
    });

    JudgmentResult safe = risk_transport::ResultDispatcher::idleResult();
    safe.terminal_id = kTerminal;
    JudgmentResult danger = safe;
    danger.final_risk = RiskLevel::DANGER;
    danger.distance_mm = 700.0;

    dispatcher.primeIdle(safe);
    dispatcher.submit(safe);    // idle -> 첫 실제 SAFE: 운영 ALERT는 생략
    dispatcher.submit(danger);  // SAFE -> DANGER: 1건
    dispatcher.submit(danger);  // 같은 상태 heartbeat 후보: 추가 없음
    dispatcher.submit(safe);    // DANGER -> SAFE: 1건

    check(transitions.size() == 2, "ALERT 훅은 위험 상태 전이에서만 호출");
    check(transitions.size() >= 2 &&
              transitions[0] == std::make_pair(RiskLevel::SAFE, RiskLevel::DANGER) &&
              transitions[1] == std::make_pair(RiskLevel::DANGER, RiskLevel::SAFE),
          "ALERT 훅이 SAFE↔DANGER 전이를 순서대로 전달");
}

void testDispatcherAddsCorrelationContext() {
    std::cout << "\n[분산 관측 상관 키]\n";
    std::vector<std::string> payloads;
    risk_transport::ResultDispatcher dispatcher(
        [&](const std::string& payload) { payloads.push_back(payload); },
        std::chrono::hours(1));

    JudgmentResult safe = risk_transport::ResultDispatcher::idleResult();
    safe.terminal_id = kTerminal;
    dispatcher.submit(safe);
    dispatcher.submit(safe);  // 동일 상태는 즉시 재발행하지 않음

    JudgmentResult danger = safe;
    danger.final_risk = RiskLevel::DANGER;
    dispatcher.submit(danger);

    check(payloads.size() == 2, "상태 변화 2건만 즉시 발행");
    if (payloads.size() == 2) {
        const auto& first = payloads[0];
        const auto& second = payloads[1];
        check(first.find("\"server_run_id\":\"") != std::string::npos,
              "risk payload에 server_run_id가 포함됨");
        check(first.find("\"decision_id\":\"") != std::string::npos,
              "risk payload에 decision_id가 포함됨");
        check(first.find("\"publish_seq\":1") != std::string::npos &&
                  first.find("\"send_reason\":\"change\"") != std::string::npos,
              "첫 발행에 publish_seq=1과 change 사유가 포함됨");
        const auto first_marker = first.find("\"decision_id\":\"");
        const auto second_marker = second.find("\"decision_id\":\"");
        const auto first_decision_begin = first_marker == std::string::npos ? first_marker : first_marker + 15;
        const auto first_decision_end = first.find('"', first_decision_begin);
        const auto second_decision_begin = second_marker == std::string::npos ? second_marker : second_marker + 15;
        const auto second_decision_end = second.find('"', second_decision_begin);
        check(first_marker != std::string::npos && second_marker != std::string::npos &&
                  first_decision_end > first_decision_begin && second_decision_end > second_decision_begin &&
                  first.substr(first_decision_begin, first_decision_end - first_decision_begin) !=
                      second.substr(second_decision_begin, second_decision_end - second_decision_begin),
              "위험 상태 전이마다 decision_id가 새로 발급됨");
        check(second.find("\"publish_seq\":2") != std::string::npos,
              "두 번째 발행의 publish_seq가 증가함");
    }
}

}  // namespace

int main() {
    testStreamIdentityMapping();
    testRiskAndJson();
    testDeviceRadiusAndHandover();
    testSensorExcludedMode();
    testAlertHookOnlyRunsOnStateTransition();
    testDispatcherAddsCorrelationContext();
    return failures == 0 ? 0 : 1;
}
