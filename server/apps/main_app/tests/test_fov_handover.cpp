// test_fov_handover.cpp
// FOV 기반 핸드오버 검증: 지게차 마커의 world 좌표가 활성 스트림 화면 안에 있는 한
// 전환이 일어나지 않고, 화면 밖으로 나간 채 유예 시간을 넘겼을 때만 전환된다.
//
// 채널3 통합 테스트와 같은 fixture H(두 스트림 공유)를 사용한다.

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
                       const WorldPoint& world) {
    ArucoFrame frame;
    frame.utcTime = "2026-08-25T00:00:00.000Z";
    frame.channel = channel;
    frame.stream_id = stream_id;
    frame.camera_id = kCameraId;
    const auto center = worldToPixel(world);
    ArucoMarker marker;
    marker.id = kForkliftMarkerId;
    marker.corners = {{{center.x - 5.0f, center.y - 5.0f},
                       {center.x + 5.0f, center.y - 5.0f},
                       {center.x + 5.0f, center.y + 5.0f},
                       {center.x - 5.0f, center.y + 5.0f}}};
    frame.markers.push_back(marker);
    return frame;
}

}  // namespace

int main() {
    std::cout << "=== FOV 기반 핸드오버 ===\n";
    FixedSensorReader sensors;
    const auto config = testConfig();
    const auto device_it = std::find_if(
        config.forklifts.begin(), config.forklifts.end(),
        [](const auto& device) { return device.terminal_id == kTerminalId; });
    if (device_it == config.forklifts.end()) return 1;
    const auto& device = *device_it;

    forklift::logic::SafetyFramePipeline pipeline(config, device, sensors);

    // 활성화: CH_03에서 3프레임 연속 관측
    for (int frame = 0; frame < 3; ++frame) {
        const auto changed = pipeline.processArucoStreamFrame(
            markerFrame(kStreamId, kChannel, {20.0, 120.0}));
        check(frame == 2 ? (changed && *changed == kStreamId) : !changed,
              "CH_03 3프레임 연속 관측 후 활성화");
    }

    // 시나리오 1: 같은 마커가 CH_02에서도 동시에 보인다 (같은 world 위치).
    // 유예 시간을 실제로 넘겨도 활성 CH_03의 FOV 안이므로 전환되지 않아야 한다.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    const auto unchanged = pipeline.processArucoStreamFrame(
        markerFrame(kOtherStreamId, kOtherChannel, {20.0, 120.0}));
    check(!unchanged && pipeline.activeStreamId() && *pipeline.activeStreamId() == kStreamId,
          "동시 관측(FOV 내)은 유예 경과 후에도 전환하지 않음");

    // 시나리오 2: 마커가 CH_03 화면 밖 world 위치로 이동했다.
    // CH_03 투영이 이미지 밖이 되는 위치를 찾는다.
    WorldPoint outside{20.0, 120.0};
    while (true) {
        outside.y -= 500.0;
        const auto px = worldToPixel(outside);
        if (px.y < -50.0f || px.x < -50.0f || px.x > 2600.0f || px.y > 1600.0f) break;
        if (outside.y < -100000.0) break;
    }
    // FOV 밖 위치를 CH_02가 보기 시작 -> 첫 프레임은 유예 타이머만 켜고,
    // 실제로 유예(500ms)가 지난 뒤 들어오는 프레임에서 전환된다.
    check(!pipeline.processArucoStreamFrame(
              markerFrame(kOtherStreamId, kOtherChannel, outside)),
          "FOV 이탈 직후에는 유예 중 전환 없음");
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    const auto switched = pipeline.processArucoStreamFrame(
        markerFrame(kOtherStreamId, kOtherChannel, outside));
    check(switched && *switched == kOtherStreamId &&
              pipeline.activeStreamId() && *pipeline.activeStreamId() == kOtherStreamId,
          "FOV 이탈 + 유예 경과 후 대상 스트림으로 전환");

    std::cout << (failures == 0 ? "test_fov_handover: 전체 통과\n" : "test_fov_handover: FAILED\n");
    return failures == 0 ? 0 : 1;
}
