#include "logic/pipeline/safety_frame_pipeline.hpp"

#include <chrono>
#include <vector>

namespace forklift::logic {

SafetyFramePipeline::SafetyFramePipeline(const config::SafetyServerConfig& config,
                                         const config::ForkliftDevice& device,
                                         ISensorReader& sensors)
    : marker_id_(device.marker_id),
      homography_(config),
      marker_tracker_(device.marker_id, config.handover.confirm_frames,
                      config.handover.lostGrace()),
      cross_camera_tracker_(config.tracking.iou_threshold,
                            config.tracking.world_distance_threshold_mm,
                            config.tracking.max_missed_frames),
      judgment_pipeline_(device.terminal_id, sensors, config.danger_judgment,
                         device.collision_radius_mm, config.handover.lostGrace()) {}

std::optional<std::string> SafetyFramePipeline::processArucoStreamFrame(const ArucoFrame& frame) {
    if (frame.stream_id.empty() || frame.camera_id.empty() || frame.channel < 1) return std::nullopt;
    const auto changed = marker_tracker_.onArucoFrame(frame);
    const auto active = marker_tracker_.activeStream();
    if (!active) return std::nullopt;

    if (*active == frame.stream_id) {
        // 마커가 없는 프레임도 저장한다. 다음 객체 프레임에서 이전 마커를
        // 현재 검출값으로 잘못 재사용하지 않도록 하기 위해서다.
        last_aruco_ = frame;
    }
    if (!changed) return std::nullopt;

    active_stream_ = *changed;
    active_camera_id_ = frame.camera_id;
    active_channel_ = frame.channel;
    judgment_pipeline_.setActiveStream(frame.stream_id, frame.camera_id, frame.channel);
    last_aruco_ = frame;
    return active_stream_;
}

SafetyFramePipeline::ObjectFrameOutput SafetyFramePipeline::processObjectFrame(
    const MetadataFrame& frame, double timestamp_s) {
    ObjectFrameOutput output;
    if (frame.stream_id.empty() || frame.camera_id.empty() || frame.channel < 1) {
        output.judgment = judgment_pipeline_.processFrame({}, false, {});
        output.judgment.result.stream_id = frame.stream_id;
        output.judgment.result.source_camera_id = frame.camera_id;
        output.judgment.result.channel = frame.channel;
        return output;
    }

    std::optional<WorldPoint> forklift_world;
    if (last_aruco_) {
        for (const auto& marker : last_aruco_->markers) {
            if (marker.id != marker_id_) continue;
            common::PixelPoint center;
            for (const auto& corner : marker.corners) {
                center.x += corner.x;
                center.y += corner.y;
            }
            center.x /= marker.corners.size();
            center.y /= marker.corners.size();
            forklift_world = homography_.pixelToWorld(last_aruco_->stream_id, center);
            break;
        }
    }

    output.forklift_localized = forklift_world.has_value();
    output.forklift_world = forklift_world.value_or(WorldPoint{});

    std::vector<Detection> detections;
    for (const auto& object : frame.objects) {
        if (object.classInfo.type != "Human") continue;
        const auto world = homography_.pixelToWorld(
            frame.stream_id, {object.bbox.groundX(), object.bbox.groundY()});
        if (!world) continue;

        Detection detection;
        detection.stream_id = frame.stream_id;
        detection.camera_id = frame.camera_id;
        detection.channel = frame.channel;
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
