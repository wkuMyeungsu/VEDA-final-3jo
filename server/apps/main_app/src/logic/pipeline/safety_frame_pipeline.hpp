#pragma once

#include <chrono>
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
                        const config::ForkliftDevice& device,
                        ISensorReader& sensors);

    // 설정된 지게차 marker_id가 연속 검출돼 활성 stream이 바뀐 순간에만
    // 새 stream_id를 반환한다.
    std::optional<std::string> processArucoStreamFrame(const ArucoFrame& frame);

    // 최근 ArUco와 현재 사람 검출을 같은 stream의 H로 변환한 뒤 최근접 선택과
    // 단말별 위험 판정을 수행한다.
    ObjectFrameOutput processObjectFrame(const MetadataFrame& frame, double timestamp_s);

    const std::string& activeCameraId() const { return active_camera_id_; }
    int activeChannel() const { return active_channel_; }
    const std::optional<std::string>& activeStreamId() const { return active_stream_; }
    int markerId() const { return marker_id_; }
    const std::map<std::string, std::string>& homographyStreamLoadErrors() const {
        return homography_.streamLoadErrors();
    }

private:
    int marker_id_;
    std::optional<ArucoFrame> last_aruco_;
    HomographyTransformer homography_;
    MarkerStreamTracker marker_tracker_;
    std::optional<std::string> active_stream_;
    std::string active_camera_id_;
    int active_channel_ = -1;
    CrossCameraTracker cross_camera_tracker_;
    JudgmentPipeline judgment_pipeline_;
};

}  // namespace forklift::logic
