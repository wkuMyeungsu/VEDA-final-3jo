// 고정 fixture/테스트 입력을 사용하는 채널 3 위험 판정 파이프라인 통합 테스트.
// 실제 장비 기반 E2E 지표로 집계하지 않는다.
// 운영 main과 같은 SafetyFramePipeline을 호출해 ArUco ID부터 JSON·SQLite까지 검증한다.

#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "logging/event_logger.hpp"
#include "logic/pipeline/safety_frame_pipeline.hpp"
#include "network/result_dispatcher.hpp"

#ifndef CHANNEL3_HOMOGRAPHY_PATH
#error "채널 3 호모그래피 테스트 파일 경로가 필요합니다."
#endif

namespace {

constexpr int kChannel = 3;
constexpr int kForkliftMarkerId = 0;
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
    config.danger_judgment = {300.0, 150.0, 40.0, 10.0, 100.0, 50.0, 2.0, 0.0};
    config.forklift_detection.marker_id = kForkliftMarkerId;
    config.homography.files[kChannel] = CHANNEL3_HOMOGRAPHY_PATH;
    config.homography.image_width_px = 2592;
    config.homography.image_height_px = 1520;
    config.handover.confirm_frames = 3;
    config.handover.lost_grace_ms = 500;
    config.tracking = {0.3, 1000.0, 5};
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
    forklift::logic::SafetyFramePipeline pipeline(config, kTerminalId, sensors);
    check(pipeline.homographyLoadErrors().empty(), "채널 3 실제 H 계약과 해상도 검증");

    const WorldPoint forkliftWorld{20.0, 120.0};
    ArucoFrame aruco;
    aruco.channel = kChannel;
    // 잘못된 ID를 앞에 배치해도 설정된 ID 0의 중심을 선택해야 한다.
    aruco.markers = {markerAt(7, {300.0, 220.0}), markerAt(kForkliftMarkerId, forkliftWorld)};
    const auto parsedAruco = parseArucoMetadata(arucoXml(aruco));
    check(parsedAruco && parsedAruco->markers.size() == 2,
          "채널 3 ArUco XML에서 ID와 네 꼭짓점 파싱");
    if (!parsedAruco) return 1;
    check(!pipeline.processArucoFrame(*parsedAruco), "첫 번째 ID 0 프레임에서는 채널 미확정");
    check(!pipeline.processArucoFrame(*parsedAruco), "두 번째 ID 0 프레임에서도 채널 미확정");
    const auto channel = pipeline.processArucoFrame(*parsedAruco);
    check(channel && *channel == kChannel && pipeline.activeCameraId() == kChannel,
          "ID 0을 3프레임 확인한 뒤 활성 채널 3 확정");

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
        const MetadataFrame objects = parseOnvifMetadata(
            objectXml({{320.0, 220.0}, step.person}));
        check(objects.objects.size() == 2,
              std::string(step.name) + ": ONVIF XML에서 Human bbox 두 개 파싱");
        const auto output = pipeline.processObjectFrame(objects, timestamp++);
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
        check(result.final_risk == step.expected && near(result.distance_mm, step.distance_mm),
              std::string(step.name) + ": 거리 " + std::to_string(step.distance_mm) +
                  "mm → " + toString(step.expected));
        check(result.camera_id == "CAM_03" && result.terminal_id == kTerminalId &&
                  result.exception == ExceptionState::NONE,
              std::string(step.name) + ": 채널·단말·센서 융합 결과 유지");
        check(published.size() == step.expected_publish_count,
              std::string(step.name) + ": 상태 변화 시에만 JSON 발행");
    }

    check(!published.empty() && published.back().find("\"camera_id\":\"CAM_03\"") != std::string::npos &&
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
