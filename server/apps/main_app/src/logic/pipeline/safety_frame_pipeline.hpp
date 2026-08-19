#pragma once

#include <map>
#include <optional>
#include <string>

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
    struct ObjectFrameOutput {
        PipelineOutput judgment;
        bool forklift_localized = false;
        WorldPoint forklift_world{};
        NearestPersonResult nearest;
        std::size_t transformed_people = 0;
    };

    SafetyFramePipeline(const config::SafetyServerConfig& config,
                        const std::string& terminal_id,
                        ISensorReader& sensors);

    // 설정된 지게차 marker_id가 연속 검출돼 활성 채널이 바뀐 순간에만 채널 번호를 반환한다.
    std::optional<int> processArucoFrame(const ArucoFrame& frame);
    std::optional<std::string> processArucoStreamFrame(const ArucoFrame& frame);

    // 최근 ArUco와 현재 사람 검출을 같은 채널 H로 변환한 뒤 최근접 선택과 위험 판정을 수행한다.
    ObjectFrameOutput processObjectFrame(const MetadataFrame& frame, double timestamp_s);

    int activeCameraId() const { return judgment_pipeline_.activeCameraId(); }
    const std::optional<std::string>& activeStreamId() const { return active_stream_; }
    int markerId() const { return marker_tracker_.markerId(); }
    const std::map<int, std::string>& homographyLoadErrors() const {
        return homography_.loadErrors();
    }
    const std::map<std::string, std::string>& homographyStreamLoadErrors() const {
        return homography_.streamLoadErrors();
    }

private:
    int marker_id_;
    std::optional<ArucoFrame> last_aruco_;
    HomographyTransformer homography_;
    MarkerChannelTracker marker_tracker_;
    std::map<std::string, int> stream_streaks_;
    std::optional<std::string> active_stream_;
    std::chrono::steady_clock::time_point active_stream_last_seen_{};
    CrossCameraTracker cross_camera_tracker_;
    JudgmentPipeline judgment_pipeline_;
};

}  // namespace forklift::logic
