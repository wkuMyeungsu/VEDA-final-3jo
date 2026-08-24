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
#include "logic/tracking/marker_channel_tracker.hpp"

namespace forklift::logic {

// 카메라 메타데이터 한 쌍을 실제 위험 판정까지 연결하는 서버의 핵심 파이프라인이다.
// main과 통합 테스트가 이 클래스를 함께 사용하므로 테스트용 배선이 운영 배선과 갈라지지 않는다.
class SafetyFramePipeline {
public:
    struct LocalizationStatus {
        // UI/API에서 해석하는 안정적인 진단 코드다.
        //   WAITING_FOR_ARUCO       : 아직 ArUco 프레임이 들어오지 않음
        //   MARKER_NOT_DETECTED     : ArUco 입력은 있으나 설정 ID가 없음
        //   HOMOGRAPHY_UNAVAILABLE  : 대상 마커는 있으나 좌표 변환 불가
        //   LOCALIZED                : 지게차 world 좌표 확보
        std::string status = "WAITING_FOR_ARUCO";
        int configured_marker_id = -1;
        bool localized = false;
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

    // 설정된 지게차 marker_id가 연속 검출돼 활성 stream이 바뀐 순간에만
    // 새 stream_id를 반환한다.
    std::optional<std::string> processArucoStreamFrame(const ArucoFrame& frame);

    // 최근 ArUco와 현재 사람 검출을 같은 stream의 H로 변환한 뒤 최근접 선택과
    // 단말별 위험 판정을 수행한다.
    ObjectFrameOutput processObjectFrame(const MetadataFrame& frame, double timestamp_s);
    void processValidObjectFrame(const MetadataFrame& frame, double timestamp_s,
                                 ObjectFrameOutput& output);

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
    void recordArucoObservation(const ArucoFrame& frame);
    void updateLocalizationResult(bool localized,
                                  bool marker_found,
                                  bool homography_available);

    int marker_id_;
    std::optional<ArucoFrame> last_aruco_;
    HomographyTransformer homography_;
    MarkerStreamTracker marker_tracker_;
    std::optional<std::string> active_stream_;
    std::string active_camera_id_;
    int active_channel_ = -1;
    double track_freshness_sec_;
    double people_timeout_sec_;
    CrossCameraTracker cross_camera_tracker_;
    JudgmentPipeline judgment_pipeline_;
    mutable std::mutex localization_mutex_;
    LocalizationStatus localization_status_;
    PeopleStatus people_status_;
};

}  // namespace forklift::logic
