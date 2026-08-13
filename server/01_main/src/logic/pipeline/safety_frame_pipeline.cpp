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
    last_aruco_ = frame;
    const auto changed = marker_tracker_.onArucoFrame(frame);
    if (changed) judgment_pipeline_.setActiveCameraId(*changed);
    return changed;
}

SafetyFramePipeline::ObjectFrameOutput SafetyFramePipeline::processObjectFrame(
    const MetadataFrame& frame, double timestamp_s) {
    ObjectFrameOutput output;
    const int channel = judgment_pipeline_.activeCameraId();

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
            forklift_world = homography_.pixelToWorld(channel, center);
            break;
        }
    }

    output.forklift_localized = forklift_world.has_value();
    output.forklift_world = forklift_world.value_or(WorldPoint{});

    std::vector<Detection> detections;
    for (const auto& object : frame.objects) {
        if (object.classInfo.type != "Human") continue;
        const auto world = homography_.pixelToWorld(
            channel, {object.bbox.groundX(), object.bbox.groundY()});
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
    return output;
}

}  // namespace forklift::logic
