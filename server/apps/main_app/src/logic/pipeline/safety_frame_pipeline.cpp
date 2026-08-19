#include "logic/pipeline/safety_frame_pipeline.hpp"

#include <chrono>
#include <vector>

namespace forklift::logic {

SafetyFramePipeline::SafetyFramePipeline(const config::SafetyServerConfig& config,
                                         const std::string& terminal_id,
                                         ISensorReader& sensors)
    : marker_id_(config.forklift_detection.marker_id),
      homography_(config),
      marker_tracker_(marker_id_, config.handover.confirm_frames,
                      config.handover.lostGrace()),
      cross_camera_tracker_(config.tracking.iou_threshold,
                            config.tracking.world_distance_threshold_mm,
                            config.tracking.max_missed_frames),
      judgment_pipeline_(-1, terminal_id, sensors, config.danger_judgment,
                         config.handover.lostGrace()) {}

std::optional<int> SafetyFramePipeline::processArucoFrame(const ArucoFrame& frame) {
    if (!frame.stream_id.empty()) {
        const auto changed = processArucoStreamFrame(frame);
        if (!changed) return std::nullopt;
        return frame.channel;
    }
    last_aruco_ = frame;
    const auto changed = marker_tracker_.onArucoFrame(frame);
    if (changed) judgment_pipeline_.setActiveCameraId(*changed);
    return changed;
}

std::optional<std::string> SafetyFramePipeline::processArucoStreamFrame(const ArucoFrame& frame) {
    if (frame.stream_id.empty() || frame.channel < 1) return std::nullopt;
    bool seen = false;
    for (const auto& marker : frame.markers) if (marker.id == marker_id_) { seen = true; break; }
    auto& streak = stream_streaks_[frame.stream_id];
    streak = seen ? streak + 1 : 0;
    const auto now = std::chrono::steady_clock::now();

    // 현재 스트림은 마커가 보이는 동안 계속 유지한다.
    if (seen && active_stream_ && *active_stream_ == frame.stream_id)
        active_stream_last_seen_ = now;

    // 처음 발견한 스트림은 확인 프레임 수를 채운 뒤 담당 스트림으로 확정한다.
    if (!active_stream_) {
        if (seen && streak >= marker_tracker_.confirmFrames()) {
            active_stream_ = frame.stream_id;
            active_stream_last_seen_ = now;
            judgment_pipeline_.setActiveCameraId(frame.channel);
            last_aruco_ = frame;
            return active_stream_;
        }
    } else if (*active_stream_ == frame.stream_id) {
        // 마커가 없는 프레임도 저장한다. 다음 객체 프레임에서 이전 마커를
        // 현재 검출값으로 잘못 재사용하지 않도록 하기 위해서다.
        last_aruco_ = frame;
        if (seen) {
            active_stream_last_seen_ = now;
        }
    // 다른 스트림으로 옮길 때는 기존 스트림의 유실 유예 시간이 지난 뒤 전환한다.
    } else if (seen && streak >= marker_tracker_.confirmFrames() &&
               now - active_stream_last_seen_ >= marker_tracker_.lostGrace()) {
        active_stream_ = frame.stream_id;
        active_stream_last_seen_ = now;
        judgment_pipeline_.setActiveCameraId(frame.channel);
        last_aruco_ = frame;
        return active_stream_;
    }
    return std::nullopt;
}

SafetyFramePipeline::ObjectFrameOutput SafetyFramePipeline::processObjectFrame(
    const MetadataFrame& frame, double timestamp_s) {
    ObjectFrameOutput output;
    const int channel = judgment_pipeline_.activeCameraId();
    if (!frame.stream_id.empty() && (!active_stream_ || *active_stream_ != frame.stream_id)) {
        output.judgment = judgment_pipeline_.processFrame({}, false, {});
        output.judgment.result.stream_id = frame.stream_id;
        output.judgment.result.source_camera_id = frame.camera_id;
        output.judgment.result.channel = frame.channel;
        return output;
    }

    std::optional<WorldPoint> forklift_world;
    if (last_aruco_ && last_aruco_->channel == channel) {
        for (const auto& marker : last_aruco_->markers) {
            if (marker.id != marker_id_) continue;
            common::PixelPoint center;
            for (const auto& corner : marker.corners) {
                center.x += corner.x;
                center.y += corner.y;
            }
            center.x /= marker.corners.size();
            center.y /= marker.corners.size();
            forklift_world = frame.stream_id.empty()
                ? homography_.pixelToWorld(channel, center)
                : homography_.pixelToWorld(frame.stream_id, center);
            break;
        }
    }

    output.forklift_localized = forklift_world.has_value();
    output.forklift_world = forklift_world.value_or(WorldPoint{});

    std::vector<Detection> detections;
    for (const auto& object : frame.objects) {
        if (object.classInfo.type != "Human") continue;
        const auto world = frame.stream_id.empty()
            ? homography_.pixelToWorld(channel, {object.bbox.groundX(), object.bbox.groundY()})
            : homography_.pixelToWorld(frame.stream_id, {object.bbox.groundX(), object.bbox.groundY()});
        if (!world) continue;

        Detection detection;
        detection.camera_id = channel;
        detection.bbox = object.bbox;
        detection.world = *world;
        detection.timestamp_s = timestamp_s;
        detections.push_back(detection);
    }
    output.transformed_people = detections.size();

    if (output.forklift_localized) {
        const auto tracks = cross_camera_tracker_.update(detections, timestamp_s);
        output.nearest = selectNearestPerson(output.forklift_world, tracks);
    }

    output.judgment = judgment_pipeline_.processFrame(
        output.forklift_world, output.forklift_localized, output.nearest);
    output.judgment.result.stream_id = frame.stream_id;
    output.judgment.result.source_camera_id = frame.camera_id;
    output.judgment.result.channel = frame.channel;
    return output;
}

}  // namespace forklift::logic
