// test_fov_handover.cpp
// 등록된 단말 마커가 보이면 그 화면으로 즉시 배정하고, 여러 카메라에 동시에
// 있으면 더 크게 보이는 쪽을 고른다. 매 확인마다 현재 배정 stream을 돌려준다.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "logic/pipeline/safety_frame_pipeline.hpp"

#ifndef CHANNEL3_HOMOGRAPHY_PATH
#error "채널 3 호모그래피 테스트 파일 경로가 필요합니다."
#endif

namespace {

constexpr int kChannel = 3;
constexpr int kOtherChannel = 2;
constexpr int kForkliftMarkerId = 1;
constexpr const char* kCameraId = "CAM_01";
constexpr const char* kStreamId = "CAM_01_CH_03";
constexpr const char* kOtherStreamId = "CAM_01_CH_02";
constexpr const char* kTerminalId = "TEST_FORKLIFT_01";

int failures = 0;

void check(bool condition, const std::string& description) {
    std::cout << (condition ? "  [통과] " : "  [실패] ") << description << '\n';
    if (!condition) ++failures;
}

std::array<double, 9> inverse3x3(const std::array<double, 9>& m) {
    const double determinant =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    return {
        (m[4] * m[8] - m[5] * m[7]) / determinant,
        (m[2] * m[7] - m[1] * m[8]) / determinant,
        (m[1] * m[5] - m[2] * m[4]) / determinant,
        (m[5] * m[6] - m[3] * m[8]) / determinant,
        (m[0] * m[8] - m[2] * m[6]) / determinant,
        (m[2] * m[3] - m[0] * m[5]) / determinant,
        (m[3] * m[7] - m[4] * m[6]) / determinant,
        (m[1] * m[6] - m[0] * m[7]) / determinant,
        (m[0] * m[4] - m[1] * m[3]) / determinant};
}

// 채널3 통합 테스트와 동일한 실제 H. 테스트 입력 생성에만 역행렬로 쓴다.
const std::array<double, 9> kPixelToWorld{
    0.3660082962662228, 0.028254535934075793, -651.4810663073883,
    -0.0077259655544824205, -0.40175409356616903, 488.25114065028424,
    -0.00008805736388571837, -0.00012439536054048328, 1.0};

forklift::common::PixelPoint worldToPixel(const WorldPoint& world) {
    static const auto inverse = inverse3x3(kPixelToWorld);
    const double denominator = inverse[6] * world.x + inverse[7] * world.y + inverse[8];
    return {static_cast<float>((inverse[0] * world.x + inverse[1] * world.y + inverse[2]) / denominator),
            static_cast<float>((inverse[3] * world.x + inverse[4] * world.y + inverse[5]) / denominator)};
}

forklift::config::SafetyServerConfig testConfig() {
    forklift::config::SafetyServerConfig config;
    config.source_path = "fov_handover_config.json";
    config.danger_judgment = {300.0, 150.0, 40.0, 10.0, 100.0, 50.0, 2.0};
    config.forklifts.push_back({kTerminalId, kForkliftMarkerId, 0.0});
    config.homography.stream_files[kStreamId] = CHANNEL3_HOMOGRAPHY_PATH;
    config.homography.stream_image_sizes[kStreamId] = {2592, 1520};
    config.homography.stream_files[kOtherStreamId] = CHANNEL3_HOMOGRAPHY_PATH;
    config.homography.stream_image_sizes[kOtherStreamId] = {2592, 1520};
    config.streams.push_back({kStreamId, kCameraId, "PNM-C16083RVQ", 4, kChannel,
                              "rtsp://test", CHANNEL3_HOMOGRAPHY_PATH, 2592, 1520});
    config.streams.push_back({kOtherStreamId, kCameraId, "PNM-C16083RVQ", 4, kOtherChannel,
                              "rtsp://test-2", CHANNEL3_HOMOGRAPHY_PATH, 2592, 1520});
    config.handover.confirm_frames = 3;
    config.handover.lost_grace_ms = 500;
    config.tracking = {0.3, 1000.0, 200, 500};
    return config;
}

class FixedSensorReader : public ISensorReader {
public:
    SensorInput read() override {
        return SensorInput{true, true, 350.0, 0.1, false};
    }
};

ArucoFrame markerFrame(const std::string& stream_id, int channel,
                       const WorldPoint& world, float half_size = 5.0f) {
    ArucoFrame frame;
    frame.utcTime = "2026-08-25T00:00:00.000Z";
    frame.channel = channel;
    frame.stream_id = stream_id;
    frame.camera_id = kCameraId;
    const auto center = worldToPixel(world);
    ArucoMarker marker;
    marker.id = kForkliftMarkerId;
    marker.corners = {{{center.x - half_size, center.y - half_size},
                       {center.x + half_size, center.y - half_size},
                       {center.x + half_size, center.y + half_size},
                       {center.x - half_size, center.y + half_size}}};
    frame.markers.push_back(marker);
    return frame;
}

}  // namespace

int main() {
    std::cout << "=== 마커 가시성 핸드오버 ===\n";
    FixedSensorReader sensors;
    const auto config = testConfig();
    const auto device_it = std::find_if(
        config.forklifts.begin(), config.forklifts.end(),
        [](const auto& device) { return device.terminal_id == kTerminalId; });
    if (device_it == config.forklifts.end()) return 1;
    const auto& device = *device_it;

    forklift::logic::SafetyFramePipeline pipeline(config, device, sensors);
    const WorldPoint forklift{20.0, 120.0};

    const auto first = pipeline.processArucoStreamFrame(
        markerFrame(kStreamId, kChannel, forklift, 8.0f));
    check(first && *first == kStreamId && pipeline.activeStreamId() &&
              *pipeline.activeStreamId() == kStreamId,
          "등록 마커가 보이면 즉시 그 화면으로 배정");
    const auto again = pipeline.processArucoStreamFrame(
        markerFrame(kStreamId, kChannel, forklift, 8.0f));
    check(again && *again == kStreamId, "매 확인마다 현재 배정 화면을 다시 알려줌");

    const auto sameView = pipeline.processArucoStreamFrame(
        markerFrame(kOtherStreamId, kOtherChannel, forklift, 8.0f));
    check(sameView && *sameView == kStreamId && pipeline.activeStreamId() &&
              *pipeline.activeStreamId() == kStreamId,
          "비슷한 크기로 동시에 보이면 현재 배정 화면을 유지");

    const auto switched = pipeline.processArucoStreamFrame(
        markerFrame(kOtherStreamId, kOtherChannel, forklift, 20.0f));
    check(switched && *switched == kOtherStreamId &&
              pipeline.activeStreamId() && *pipeline.activeStreamId() == kOtherStreamId,
          "더 크게 보이는 화면이 있으면 즉시 그쪽으로 배정");

    forklift::logic::SafetyFramePipeline jitter(config, device, sensors);
    check(jitter.processArucoStreamFrame(markerFrame(kStreamId, kChannel, forklift, 10.0f)) &&
              jitter.activeStreamId() && *jitter.activeStreamId() == kStreamId,
          "지터 테스트 시작 화면 CH_03");
    const auto jittered = jitter.processArucoStreamFrame(
        markerFrame(kOtherStreamId, kOtherChannel, forklift, 11.0f));
    check(jittered && *jittered == kStreamId,
          "1.3배 미만 크기 차이면 화면을 바꾸지 않음");

    forklift::logic::SafetyFramePipeline smallerOther(config, device, sensors);
    check(smallerOther.processArucoStreamFrame(
              markerFrame(kStreamId, kChannel, forklift, 12.0f)) &&
              smallerOther.activeStreamId() && *smallerOther.activeStreamId() == kStreamId,
          "시작 화면은 마커가 더 크게 보이는 CH_03");
    const WorldPoint otherSpot{900.0, 120.0};
    const auto keepLarger = smallerOther.processArucoStreamFrame(
        markerFrame(kOtherStreamId, kOtherChannel, otherSpot, 4.0f));
    check(keepLarger && *keepLarger == kStreamId &&
              smallerOther.activeStreamId() && *smallerOther.activeStreamId() == kStreamId,
          "다른 화면의 더 작은 동일 ID보다 큰 쪽을 유지");

    auto staleConfig = config;
    staleConfig.tracking.track_freshness_ms = 1;
    staleConfig.handover.lost_grace_ms = 2;
    forklift::logic::SafetyFramePipeline staleView(staleConfig, device, sensors);
    check(staleView.processArucoStreamFrame(
              markerFrame(kStreamId, kChannel, forklift)).has_value(),
          "중단 스트림 만료 테스트 시작 화면 배정");
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    ArucoFrame emptyOther = markerFrame(kOtherStreamId, kOtherChannel, forklift);
    emptyOther.markers.clear();
    staleView.processArucoStreamFrame(emptyOther);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    staleView.processArucoStreamFrame(emptyOther);
    check(!staleView.localizationStatus().localized,
          "카메라가 끊겨 새 프레임이 없어도 과거 마커 관측은 만료");

    std::cout << (failures == 0 ? "test_fov_handover: 전체 통과\n" : "test_fov_handover: FAILED\n");
    return failures == 0 ? 0 : 1;
}
