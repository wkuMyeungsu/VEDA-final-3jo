#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "config_loader/safety_server_config.hpp"
#include "input/aruco_metadata_parser.hpp"
#include "input/onvif_metadata_parser.hpp"
#include "logic/homography/homography_transformer.hpp"
#include "logic/judgment/judgment_pipeline.h"
#include "logic/tracking/cross_camera_reid.h"

namespace forklift::logic {

// 카메라 메타데이터 한 쌍을 실제 위험 판정까지 연결하는 서버의 핵심 파이프라인이다.
// main과 통합 테스트가 이 클래스를 함께 사용하므로 테스트용 배선이 운영 배선과 갈라지지 않는다.
class SafetyFramePipeline {
public:
    struct ArucoStreamStatus {
        std::string stream_id;
        std::string camera_id;
        int channel = -1;
        std::string last_frame_utc;
        std::string last_target_marker_seen_utc;
        bool target_marker_visible = false;
        std::vector<int> marker_ids;
    };

    struct LocalizationStatus {
        // UI/API에서 해석하는 안정적인 진단 코드다.
        //   WAITING_FOR_ARUCO       : 아직 ArUco 프레임이 들어오지 않음
        //   MARKER_NOT_DETECTED     : ArUco 입력은 있으나 설정 ID가 없음
        //   HOMOGRAPHY_UNAVAILABLE  : 대상 마커는 있으나 좌표 변환 불가
        //   LOCALIZED                : 지게차 world 좌표 확보
        std::string status = "WAITING_FOR_ARUCO";
        int configured_marker_id = -1;
        bool localized = false;
        bool has_position = false;
        WorldPoint position{};
        std::string active_stream_id;
        std::string active_camera_id;
        int active_channel = -1;
        std::string last_aruco_frame_utc;
        std::string last_aruco_frame_stream_id;
        int last_aruco_frame_channel = -1;
        std::string last_target_marker_seen_utc;
        std::string last_target_marker_stream_id;
        int last_target_marker_channel = -1;
        std::string last_observed_markers_utc;
        std::string last_observed_markers_stream_id;
        int last_observed_markers_channel = -1;
        std::vector<int> last_observed_marker_ids;
        std::vector<ArucoStreamStatus> aruco_streams;
    };

    // 모니터링에 필요한 현재 사람 트랙의 최소 정보다. 원시 영상이나 매 프레임
    // bbox를 runtime snapshot에 넣지 않고, 유효한 트랙만 월드 좌표로 제공한다.
    struct PersonStatus {
        int track_id = -1;
        std::string stream_id;
        std::string camera_id;
        int channel = -1;
        WorldPoint position{};
        double distance_mm = -1.0;
        double last_seen_s = 0.0;
        int missed_frames = 0;
        std::string observed_utc;
    };

    struct PeopleStatus {
        std::string last_update_utc;
        double last_update_s = -1.0;
        std::vector<PersonStatus> tracks;
    };

    struct ObjectFrameOutput {
        PipelineOutput judgment;
        bool forklift_localized = false;
        WorldPoint forklift_world{};
        NearestPersonResult nearest;
        std::size_t transformed_people = 0;
    };

    SafetyFramePipeline(const config::SafetyServerConfig& config,
                        const config::ForkliftDevice& device,
                        ISensorReader& sensors,
                        bool ignore_sensor_input = false);

    // 이 단말에 등록된 마커가 보이면 그 화면으로 즉시 배정한다. 여러 카메라에
    // 동시에 있으면 더 크게 보이는 쪽을 고르고, 마커가 있는 동안에는 매 확인마다
    // 현재 배정 stream_id를 돌려준다.
    std::optional<std::string> processArucoStreamFrame(const ArucoFrame& frame);

    // 최근 ArUco와 현재 사람 검출을 같은 stream의 H로 변환한 뒤 최근접 선택과
    // 단말별 위험 판정을 수행한다.
    ObjectFrameOutput processObjectFrame(const MetadataFrame& frame, double timestamp_s);
    // 카메라별 객체 프레임을 모두 반영한 최신 스냅숏으로 단말 판정을 한 번만 수행한다.
    // 중앙 루프의 주기 Tick에서만 호출해 카메라 A/B가 서로의 결과를 덮어쓰지 않게 한다.
    ObjectFrameOutput processAggregatedFrame(double timestamp_s);
    // 객체 프레임은 트랙/카메라별 최신 관측만 갱신하고, 최종 판정은 Tick에서 수행한다.
    void updateObjectFrame(const MetadataFrame& frame, double timestamp_s);
    void processValidObjectFrame(const MetadataFrame& frame, double timestamp_s,
                                 ObjectFrameOutput& output, bool evaluate = true);

    const std::string& activeCameraId() const { return active_camera_id_; }
    int activeChannel() const { return active_channel_; }
    const std::optional<std::string>& activeStreamId() const { return active_stream_; }
    int markerId() const { return marker_id_; }
    const std::map<std::string, std::string>& homographyStreamLoadErrors() const {
        return homography_.streamLoadErrors();
    }
    LocalizationStatus localizationStatus() const;
    PeopleStatus peopleStatus(double now_s) const;

private:
    // 지게차 마커의 최근 월드 관측. 어느 스트림이 봤든 하나의 위치로 융합한다
    // (단말당 지게차 1대 가정 - forklifts 설정이 단일 마커 ID 기준).
    struct WorldSighting {
        WorldPoint pos{};
        std::chrono::steady_clock::time_point seen{};
        std::string stream_id;
        double pixel_area = 0.0;
    };

    void recordArucoObservation(const ArucoFrame& frame);
    void refreshGlobalForkliftSighting(std::chrono::steady_clock::time_point now);
    std::optional<WorldPoint> resolvedForkliftWorld(
        std::chrono::steady_clock::time_point now) const;
    bool anyTargetMarkerVisible() const;
    bool insideWorkArea(const WorldPoint& point) const;
    NearestPersonResult nearestAcrossCameras(const WorldPoint& forklift,
                                             double now_s) const;
    void updateLocalizationResult(bool localized,
                                  bool marker_found,
                                  bool homography_available);
    void activateStream(const std::string& stream_id, const std::string& camera_id,
                        int channel, const ArucoFrame* triggering_frame);
    std::optional<WorldSighting> extractForkliftSighting(
        const ArucoFrame& frame, std::chrono::steady_clock::time_point now) const;
    const WorldSighting* bestFreshSighting(
        std::chrono::steady_clock::time_point now) const;
    std::optional<std::string> selectAssignment(
        const ArucoFrame& frame, std::chrono::steady_clock::time_point now);
    static double markerPixelArea(const std::array<forklift::common::PixelPoint, 4>& corners);

    struct StreamPeopleObservation {
        double timestamp_s = -1.0;
        std::vector<Detection> detections;
    };

    int marker_id_;
    double marker_height_mm_ = 0.0;
    std::optional<ArucoFrame> last_aruco_;
    HomographyTransformer homography_;
    std::chrono::milliseconds fov_grace_{};
    std::chrono::milliseconds view_freshness_{};
    std::optional<WorldSighting> forklift_sighting_;
    // 활성 스트림이 대상 마커를 빠뜨린 첫 프레임 시각. 이 시점부터
    // lost_grace_ms 동안만 직전 유효 위치를 재사용한다.
    std::optional<std::chrono::steady_clock::time_point> marker_missing_since_;
    std::map<std::string, std::pair<std::string, int>> stream_identity_;  // stream_id -> (camera_id, channel)
    std::optional<std::string> active_stream_;
    std::string active_camera_id_;
    int active_channel_ = -1;
    double people_timeout_sec_;
    double world_distance_threshold_mm_;
    std::vector<WorldPoint> work_area_boundary_;
    CrossCameraTracker cross_camera_tracker_;
    JudgmentPipeline judgment_pipeline_;
    mutable std::mutex localization_mutex_;
    LocalizationStatus localization_status_;
    std::map<std::string, ArucoStreamStatus> aruco_stream_status_;
    // 현재 각 스트림이 마지막으로 본 대상 마커의 유효한 월드 좌표. 한 스트림의
    // 미검출 프레임이 다른 스트림의 유효 관측을 지우지 않도록 분리해 둔다.
    std::map<std::string, WorldSighting> stream_sightings_;
    bool any_target_marker_visible_ = false;
    bool any_target_with_homography_ = false;
    std::vector<Track> latest_tracks_;
    // 최종 판정용으로 각 카메라의 최신 사람 관측을 보존한다. Tick에서 전체 스트림을
    // 한 번에 합쳐 가장 가까운 사람을 고르므로 이벤트 도착 순서가 결과를 바꾸지 않는다.
    std::map<std::string, StreamPeopleObservation> latest_people_observations_;
    PeopleStatus people_status_;
};

}  // namespace forklift::logic
