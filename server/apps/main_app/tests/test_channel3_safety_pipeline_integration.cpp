// 고정 fixture/테스트 입력을 사용하는 stream CAM_01_CH_03 위험 판정 파이프라인 통합 테스트.
// 실제 장비 기반 E2E 지표로 집계하지 않는다.
// 운영 main과 같은 SafetyFramePipeline을 호출해 ArUco ID부터 JSON·SQLite까지 검증한다.

#include <sqlite3.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "logging/event_logger.hpp"
#include "logic/pipeline/safety_frame_pipeline.hpp"
#include "network/result_dispatcher.hpp"

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

// 실제 채널 3 산출물의 픽셀→월드 행렬이다. 테스트 입력을 만들 때만 역행렬로 사용하고,
// 서버 파이프라인은 JSON 파일을 독립적으로 읽어 월드 좌표를 다시 복원한다.
constexpr std::array<double, 9> kPixelToWorld{
    0.3660082962662228, 0.028254535934075793, -651.4810663073883,
    -0.0077259655544824205, -0.40175409356616903, 488.25114065028424,
    -0.00008805736388571837, -0.00012439536054048328, 1.0};

int failures = 0;

void check(bool condition, const std::string& description) {
    std::cout << (condition ? "  [통과] " : "  [실패] ") << description << '\n';
    if (!condition) ++failures;
}

bool near(double actual, double expected, double tolerance = 0.5) {
    return std::abs(actual - expected) <= tolerance;
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

forklift::common::PixelPoint worldToPixel(const WorldPoint& world) {
    static const auto inverse = inverse3x3(kPixelToWorld);
    const double denominator = inverse[6] * world.x + inverse[7] * world.y + inverse[8];
    return {(inverse[0] * world.x + inverse[1] * world.y + inverse[2]) / denominator,
            (inverse[3] * world.x + inverse[4] * world.y + inverse[5]) / denominator};
}

forklift::config::SafetyServerConfig testConfig() {
    forklift::config::SafetyServerConfig config;
    config.source_path = "channel3_pipeline_integration_config.json";
    // 334×242mm 축소 공간에서 단계 전환을 모두 볼 수 있도록 실제 기준을 1:10로 줄인다.
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
        // 카메라 거리 단계만 관찰하도록 ToF와 IMU는 정상·안전 상태로 고정한다.
        return SensorInput{true, true, 350.0, 0.1, false};
    }
};

ArucoMarker markerAt(int id, const WorldPoint& world) {
    const auto center = worldToPixel(world);
    ArucoMarker marker;
    marker.id = id;
    marker.corners = {{{center.x - 5.0, center.y - 5.0},
                       {center.x + 5.0, center.y - 5.0},
                       {center.x + 5.0, center.y + 5.0},
                       {center.x - 5.0, center.y + 5.0}}};
    return marker;
}

DetectedObject humanAt(const WorldPoint& world) {
    const auto ground = worldToPixel(world);
    DetectedObject human;
    human.classInfo.type = "Human";
    human.classInfo.likelihood = 0.99f;
    human.bbox.left = static_cast<float>(ground.x - 10.0);
    human.bbox.right = static_cast<float>(ground.x + 10.0);
    human.bbox.top = static_cast<float>(ground.y - 40.0);
    human.bbox.bottom = static_cast<float>(ground.y);
    return human;
}

std::string arucoXml(const ArucoFrame& frame) {
    std::ostringstream xml;
    xml.precision(15);
    xml << "<tt:MetadataStream><tt:Event><wsnt:NotificationMessage>"
           "<wsnt:Topic>tns1:OpenApp/ArUCo_Detection/MarkerDetected</wsnt:Topic>"
           "<wsnt:Message><tt:Message UtcTime=\"2026-08-13T00:00:00.000Z\">"
           "<tt:Source><tt:SimpleItem Name=\"Channel\" Value=\""
        << frame.channel << "\"/></tt:Source><tt:Data>"
        << "<tt:SimpleItem Name=\"MarkerCount\" Value=\"" << frame.markers.size()
        << "\"/><tt:SimpleItem Name=\"MarkerIds\" Value=\"";
    for (std::size_t index = 0; index < frame.markers.size(); ++index) {
        if (index) xml << ',';
        xml << frame.markers[index].id;
    }
    xml << "\"/>";
    for (std::size_t index = 0; index < frame.markers.size(); ++index) {
        xml << "<tt:SimpleItem Name=\"Marker" << index << "Corners\" Value=\"";
        for (std::size_t corner = 0; corner < frame.markers[index].corners.size(); ++corner) {
            if (corner) xml << ',';
            xml << frame.markers[index].corners[corner].x << ','
                << frame.markers[index].corners[corner].y;
        }
        xml << "\"/>";
    }
    xml << "</tt:Data></tt:Message></wsnt:Message>"
           "</wsnt:NotificationMessage></tt:Event></tt:MetadataStream>";
    return xml.str();
}

std::string objectXml(const std::vector<WorldPoint>& people) {
    std::ostringstream xml;
    xml.precision(15);
    xml << "<tt:MetadataStream><tt:VideoAnalytics>"
           "<tt:Frame UtcTime=\"2026-08-13T00:00:00.100Z\">";
    int objectId = 1;
    for (const auto& person : people) {
        const auto human = humanAt(person);
        xml << "<tt:Object ObjectId=\"" << objectId++ << "\"><tt:Appearance><tt:Shape>"
            << "<tt:BoundingBox left=\"" << human.bbox.left << "\" top=\""
            << human.bbox.top << "\" right=\"" << human.bbox.right << "\" bottom=\""
            << human.bbox.bottom << "\"/></tt:Shape><tt:Class>"
               "<tt:Type Likelihood=\"0.99\">Human</tt:Type>"
               "</tt:Class></tt:Appearance></tt:Object>";
    }
    xml << "</tt:Frame></tt:VideoAnalytics></tt:MetadataStream>";
    return xml.str();
}

std::string temporaryDbPath() {
    return (std::filesystem::temp_directory_path() / "channel3_safety_pipeline_integration.db").string();
}

void removeDb(const std::string& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path + "-wal", error);
    std::filesystem::remove(path + "-shm", error);
}

std::vector<int> readRiskLevels(const std::string& path) {
    std::vector<int> levels;
    sqlite3* database = nullptr;
    if (sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(database);
        return levels;
    }
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT risk_level FROM events ORDER BY id", -1,
                           &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW)
            levels.push_back(sqlite3_column_int(statement, 0));
        sqlite3_finalize(statement);
    }
    sqlite3_close(database);
    return levels;
}

}  // namespace

int main() {
    std::cout << "=== 채널 3 실제 호모그래피 축소 위험 시나리오 ===\n";

    FixedSensorReader sensors;
    const auto config = testConfig();
    const auto device_it = std::find_if(
        config.forklifts.begin(), config.forklifts.end(),
        [](const auto& device) { return device.terminal_id == kTerminalId; });
    check(device_it != config.forklifts.end(), "TERM별 설정 목록에서 대상 terminal_id를 선택");
    if (device_it == config.forklifts.end()) return 1;
    const auto& device = *device_it;

    // 설정된 ID와 다른 마커만 들어오면 거리 판정을 하지 않고, 운영 상태에
    // "마커 미검출" 진단 근거를 남겨야 한다.
    forklift::logic::SafetyFramePipeline markerMissingPipeline(config, device, sensors);
    ArucoFrame wrongMarkerFrame;
    wrongMarkerFrame.utcTime = "2026-08-13T00:00:00.000Z";
    wrongMarkerFrame.channel = kChannel;
    wrongMarkerFrame.stream_id = kStreamId;
    wrongMarkerFrame.camera_id = kCameraId;
    wrongMarkerFrame.markers = {markerAt(34, {20.0, 120.0})};
    for (int frame = 0; frame < 3; ++frame) {
        check(!markerMissingPipeline.processArucoStreamFrame(wrongMarkerFrame),
              "설정 ID와 다른 마커만 들어오면 활성 stream을 확정하지 않음");
    }
    auto missingObjects = parseOnvifMetadata(objectXml({{330.0, 120.0}}));
    missingObjects.stream_id = kStreamId;
    missingObjects.camera_id = kCameraId;
    missingObjects.channel = kChannel;
    const auto missingOutput = markerMissingPipeline.processObjectFrame(missingObjects, 0.5);
    const auto missingStatus = markerMissingPipeline.localizationStatus();
    check(!missingOutput.forklift_localized && missingOutput.judgment.result.distance_mm < 0.0,
          "지게차 마커 미검출 시 거리 측정을 수행하지 않음");
    check(missingStatus.status == "MARKER_NOT_DETECTED" &&
              missingStatus.configured_marker_id == kForkliftMarkerId &&
              missingStatus.last_observed_marker_ids.size() == 1 &&
              missingStatus.last_observed_marker_ids.front() == 34,
          "마커 미검출 원인과 최근 관측 ID를 진단 상태에 기록");

    // 다른 스트림의 빈/미검출 프레임이 사이에 들어와도, 대상을 본
    // 스트림의 연속 확인 횟수는 유지되어야 한다.
    forklift::logic::SafetyFramePipeline interleavedActivation(config, device, sensors);
    ArucoFrame targetFrame;
    targetFrame.utcTime = "2026-08-13T00:00:01.000Z";
    targetFrame.channel = kChannel;
    targetFrame.stream_id = kStreamId;
    targetFrame.camera_id = kCameraId;
    targetFrame.markers = {markerAt(kForkliftMarkerId, {20.0, 120.0})};
    ArucoFrame otherNoTarget = wrongMarkerFrame;
    otherNoTarget.channel = kOtherChannel;
    otherNoTarget.stream_id = kOtherStreamId;
    check(!interleavedActivation.processArucoStreamFrame(targetFrame),
          "교차 입력: 대상 스트림 1회 확인");
    check(!interleavedActivation.processArucoStreamFrame(otherNoTarget),
          "교차 입력: 다른 스트림 미검출은 후보를 초기화하지 않음");
    check(!interleavedActivation.processArucoStreamFrame(targetFrame),
          "교차 입력: 대상 스트림 2회 확인");
    check(!interleavedActivation.processArucoStreamFrame(otherNoTarget),
          "교차 입력: 다른 스트림이 다시 들어와도 연속성 유지");
    const auto interleavedStream = interleavedActivation.processArucoStreamFrame(targetFrame);
    check(interleavedStream && *interleavedStream == kStreamId,
          "교차 입력에서도 대상 스트림 3회 확인 후 활성화");

    forklift::logic::SafetyFramePipeline pipeline(config, device, sensors);
    check(pipeline.homographyStreamLoadErrors().empty(), "stream별 실제 H 계약과 해상도 검증");

    const WorldPoint forkliftWorld{20.0, 120.0};
    ArucoFrame aruco;
    aruco.channel = kChannel;
    aruco.stream_id = kStreamId;
    aruco.camera_id = kCameraId;
    // 잘못된 ID를 앞에 배치해도 TERM별 설정 ID 1의 중심을 선택해야 한다.
    aruco.markers = {markerAt(7, {300.0, 220.0}), markerAt(kForkliftMarkerId, forkliftWorld)};
    auto parsedAruco = parseArucoMetadata(arucoXml(aruco));
    check(parsedAruco && parsedAruco->markers.size() == 2,
          "채널 3 ArUco XML에서 ID와 네 꼭짓점 파싱");
    if (!parsedAruco) return 1;
    parsedAruco->stream_id = kStreamId;
    parsedAruco->camera_id = kCameraId;
    check(!pipeline.processArucoStreamFrame(*parsedAruco), "첫 번째 TERM marker 프레임에서는 stream 미확정");
    check(!pipeline.processArucoStreamFrame(*parsedAruco), "두 번째 TERM marker 프레임에서도 stream 미확정");
    const auto stream = pipeline.processArucoStreamFrame(*parsedAruco);
    check(stream && *stream == kStreamId && pipeline.activeCameraId() == kCameraId,
          "TERM marker를 3프레임 확인한 뒤 활성 stream 확정");
    const auto immediateLocalization = pipeline.localizationStatus();
    check(immediateLocalization.status == "LOCALIZED" &&
              immediateLocalization.localized &&
              immediateLocalization.has_position &&
              near(immediateLocalization.position.x, forkliftWorld.x) &&
              near(immediateLocalization.position.y, forkliftWorld.y) &&
              immediateLocalization.active_stream_id == kStreamId,
          "대상 ArUco 좌표를 받으면 모니터링용 월드 위치까지 즉시 갱신");

    MetadataFrame noObjectTick;
    noObjectTick.stream_id = kStreamId;
    noObjectTick.camera_id = kCameraId;
    noObjectTick.channel = kChannel;
    const auto periodicOutput = pipeline.processObjectFrame(noObjectTick, 0.25);
    check(periodicOutput.forklift_localized &&
              periodicOutput.judgment.result.final_risk == RiskLevel::SAFE &&
              periodicOutput.judgment.result.distance_mm < 0.0,
          "사람 객체 프레임이 없어도 주기 판정에서 센서와 최근 지게차 위치로 SAFE 산출");

    auto otherStreamObjects = parseOnvifMetadata(objectXml({{330.0, 120.0}}));
    otherStreamObjects.stream_id = kOtherStreamId;
    otherStreamObjects.camera_id = kCameraId;
    otherStreamObjects.channel = kOtherChannel;
    const auto otherStreamOutput = pipeline.processObjectFrame(otherStreamObjects, 0.5);
    check(otherStreamOutput.forklift_localized && otherStreamOutput.transformed_people == 1,
          "활성 stream이 아닌 다른 stream의 객체도 H 변환·판정 대상으로 수집");
    const auto otherStreamPeople = pipeline.peopleStatus(0.5);
    check(otherStreamPeople.tracks.size() == 1 &&
              otherStreamPeople.tracks.front().channel == kOtherChannel &&
              near(otherStreamPeople.tracks.front().position.x, 330.0) &&
              near(otherStreamPeople.tracks.front().position.y, 120.0) &&
              otherStreamPeople.tracks.front().observed_utc == "2026-08-13T00:00:00.100Z",
          "단말별 현재 사람 트랙에 ID·월드 좌표·채널·원본 시각을 보존");
    check(pipeline.localizationStatus().status == "LOCALIZED" &&
              pipeline.localizationStatus().last_target_marker_seen_utc == "2026-08-13T00:00:00.000Z",
          "지게차 위치 확보 시 진단 상태와 대상 마커 마지막 검출 시각을 갱신");

    const auto aggregatedOutput = pipeline.processAggregatedFrame(0.6);
    check(aggregatedOutput.forklift_localized && aggregatedOutput.nearest.found &&
              near(aggregatedOutput.nearest.position.x, 330.0) &&
              near(aggregatedOutput.nearest.position.y, 120.0),
          "주기 판정은 활성 카메라가 아닌 스트림까지 합친 전체 최신 관측으로 수행");

    // 채널별 최근 ID를 따로 남겨, 3채널의 다른 마커 목록이
    // 2채널/활성 채널의 대상 ID 진단을 덮어쓰지 않게 한다.
    pipeline.processArucoStreamFrame(otherNoTarget);
    const auto perStreamStatus = pipeline.localizationStatus();
    const auto targetDiagnostic = std::find_if(
        perStreamStatus.aruco_streams.begin(), perStreamStatus.aruco_streams.end(),
        [](const auto& value) { return value.stream_id == kStreamId; });
    const auto otherDiagnostic = std::find_if(
        perStreamStatus.aruco_streams.begin(), perStreamStatus.aruco_streams.end(),
        [](const auto& value) { return value.stream_id == kOtherStreamId; });
    check(targetDiagnostic != perStreamStatus.aruco_streams.end() &&
              targetDiagnostic->target_marker_visible &&
              std::find(targetDiagnostic->marker_ids.begin(), targetDiagnostic->marker_ids.end(),
                        kForkliftMarkerId) != targetDiagnostic->marker_ids.end() &&
              otherDiagnostic != perStreamStatus.aruco_streams.end() &&
          !otherDiagnostic->target_marker_visible &&
              otherDiagnostic->marker_ids.size() == 1 && otherDiagnostic->marker_ids.front() == 34,
          "채널별 최근 마커 ID와 설정 ID 가시성을 독립 기록");
    check(perStreamStatus.status == "LOCALIZED" && perStreamStatus.localized,
          "다른 스트림의 대상 마커 미검출이 유효한 스트림의 전역 위치를 덮어쓰지 않음");

    // 활성 채널의 한 프레임에서 대상 ID가 빠져도 lost_grace_ms 동안은
    // 직전 유효 위치를 쓰고, 유예가 끝나면 안전하게 미검출로 전환한다.
    auto graceConfig = config;
    graceConfig.handover.confirm_frames = 1;
    graceConfig.handover.lost_grace_ms = 30;
    forklift::logic::SafetyFramePipeline gracePipeline(graceConfig, device, sensors);
    check(gracePipeline.processArucoStreamFrame(targetFrame).has_value(),
          "유예 테스트 스트림 활성화");
    gracePipeline.processArucoStreamFrame(wrongMarkerFrame);
    auto graceObjects = parseOnvifMetadata(objectXml({{330.0, 120.0}}));
    graceObjects.stream_id = kStreamId;
    graceObjects.camera_id = kCameraId;
    graceObjects.channel = kChannel;
    const auto withinGrace = gracePipeline.processObjectFrame(graceObjects, 20.0);
    check(withinGrace.forklift_localized,
          "대상 ID 한 프레임 누락 직후에는 직전 위치를 유예 시간 동안 유지");
    std::this_thread::sleep_for(std::chrono::milliseconds(45));
    const auto afterGrace = gracePipeline.processObjectFrame(graceObjects, 20.1);
    check(!afterGrace.forklift_localized &&
              gracePipeline.localizationStatus().status == "MARKER_NOT_DETECTED",
          "유예 시간 만료 후에는 이전 위치를 폐기하고 미검출로 전환");

    const std::string databasePath = temporaryDbPath();
    removeDb(databasePath);
    risk_log::EventLogger logger(databasePath);
    check(logger.start(), "SQLite 이벤트 로거 시작");

    std::vector<std::string> published;
    risk_transport::ResultDispatcher dispatcher(
        [&](const std::string& json) { published.push_back(json); },
        std::chrono::hours(1));
    dispatcher.onStateChangeEvent(
        [&](const JudgmentResult& result, int previous) { logger.log(result, previous); });

    struct Step {
        const char* name;
        WorldPoint person;
        double distance_mm;
        RiskLevel expected;
        std::size_t expected_publish_count;
    };
    const std::vector<Step> steps{
        {"안전", {330.0, 120.0}, 310.0, RiskLevel::SAFE, 1},
        {"주의", {240.0, 120.0}, 220.0, RiskLevel::CAUTION, 2},
        {"위험", {120.0, 120.0}, 100.0, RiskLevel::DANGER, 3},
        {"비상", {50.0, 120.0}, 30.0, RiskLevel::EMERGENCY, 4},
        {"비상 유지", {65.0, 120.0}, 45.0, RiskLevel::EMERGENCY, 4},
        {"비상 해제", {80.0, 120.0}, 60.0, RiskLevel::DANGER, 5}};

    double timestamp = 1.0;
    int nearestTrackId = -1;
    for (const auto& step : steps) {
        // 두 번째 사람은 항상 멀리 두어 최근접 선택이 실제로 수행되는지 확인한다.
        auto objects = parseOnvifMetadata(
            objectXml({{320.0, 220.0}, step.person}));
        objects.stream_id = kStreamId;
        objects.camera_id = kCameraId;
        objects.channel = kChannel;
        check(objects.objects.size() == 2,
              std::string(step.name) + ": ONVIF XML에서 Human bbox 두 개 파싱");
        const double frameTimestamp = timestamp++;
        const auto output = pipeline.processObjectFrame(objects, frameTimestamp);
        dispatcher.submit(output.judgment.result);

        const auto& result = output.judgment.result;
        check(output.forklift_localized && near(output.forklift_world.x, forkliftWorld.x) &&
                  near(output.forklift_world.y, forkliftWorld.y),
              std::string(step.name) + ": ArUco ID 0 중심을 H로 지게차 좌표 복원");
        check(output.transformed_people == 2 && output.nearest.found &&
                  near(output.nearest.position.x, step.person.x) &&
                  near(output.nearest.position.y, step.person.y),
              std::string(step.name) + ": 두 사람 중 최근접 사람 선택");
        if (nearestTrackId < 0) nearestTrackId = output.nearest.track_id;
        check(output.nearest.track_id == nearestTrackId,
              std::string(step.name) + ": 이동 중에도 최근접 사람 track_id 유지");
        const auto people = pipeline.peopleStatus(frameTimestamp);
        check(people.tracks.size() == 2 && people.tracks.front().track_id > 0,
              std::string(step.name) + ": 현재 검출된 두 사람 트랙을 모니터링 상태에 제공");
        check(result.final_risk == step.expected && near(result.distance_mm, step.distance_mm),
              std::string(step.name) + ": 거리 " + std::to_string(step.distance_mm) +
                  "mm → " + toString(step.expected));
        check(result.camera_id == kCameraId && result.terminal_id == kTerminalId &&
                  result.exception == ExceptionState::NONE,
              std::string(step.name) + ": 채널·단말·센서 융합 결과 유지");
        check(published.size() == step.expected_publish_count,
              std::string(step.name) + ": 상태 변화 시에만 JSON 발행");
    }

    check(!published.empty() && published.back().find("\"camera_id\":\"CAM_01\"") != std::string::npos &&
              published.back().find("\"distance_mm\":60") != std::string::npos &&
              published.back().find("\"risk_level\":2") != std::string::npos,
          "최종 DANGER 결과가 distance_mm JSON 계약으로 발행됨");

    check(logger.flushWithin(std::chrono::seconds(3)), "판정 상태 변화 로그를 SQLite에 반영");
    logger.stop();
    const std::vector<int> expectedLevels{0, 1, 2, 3, 2};
    check(readRiskLevels(databasePath) == expectedLevels,
          "SQLite에 SAFE→CAUTION→DANGER→EMERGENCY→DANGER 순서 기록");
    removeDb(databasePath);

    std::cout << "=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
