#include "logic/pipeline/safety_frame_pipeline.hpp"

#include <chrono>
#include <utility>
#include <vector>

namespace forklift::logic {

SafetyFramePipeline::SafetyFramePipeline(const config::SafetyServerConfig& config,
                                         const config::ForkliftDevice& device,
                                         ISensorReader& sensors,
                                         bool ignore_sensor_input)
    : marker_id_(device.marker_id),
      homography_(config),
      marker_tracker_(device.marker_id, config.handover.confirm_frames,
                      config.handover.lostGrace()),
      track_freshness_sec_(config.tracking.track_freshness_ms / 1000.0),
      people_timeout_sec_(config.tracking.track_timeout_ms / 1000.0),
      cross_camera_tracker_(config.tracking.iou_threshold,
                            config.tracking.world_distance_threshold_mm,
                            config.tracking.track_timeout_ms / 1000.0),
      judgment_pipeline_(device.terminal_id, sensors, config.danger_judgment,
                         device.collision_radius_mm, config.handover.lostGrace(),
                         ignore_sensor_input) {
    localization_status_.configured_marker_id = marker_id_;
}

void SafetyFramePipeline::recordArucoObservation(const ArucoFrame& frame) {
    std::lock_guard<std::mutex> lock(localization_mutex_);
    localization_status_.configured_marker_id = marker_id_;
    localization_status_.last_aruco_frame_utc =
        frame.utcTime.empty() ? frame.serverReceivedUtc : frame.utcTime;
    localization_status_.last_aruco_frame_stream_id = frame.stream_id;
    localization_status_.last_aruco_frame_channel = frame.channel;

    bool target_marker_seen = false;
    for (const auto& marker : frame.markers) {
        if (marker.id == marker_id_) target_marker_seen = true;
    }
    if (target_marker_seen) {
        localization_status_.last_target_marker_seen_utc =
            frame.utcTime.empty() ? frame.serverReceivedUtc : frame.utcTime;
        localization_status_.last_target_marker_stream_id = frame.stream_id;
        localization_status_.last_target_marker_channel = frame.channel;
    }
    if (!frame.markers.empty()) {
        localization_status_.last_observed_markers_utc =
            frame.utcTime.empty() ? frame.serverReceivedUtc : frame.utcTime;
        localization_status_.last_observed_markers_stream_id = frame.stream_id;
        localization_status_.last_observed_markers_channel = frame.channel;
        localization_status_.last_observed_marker_ids.clear();
        localization_status_.last_observed_marker_ids.reserve(frame.markers.size());
        for (const auto& marker : frame.markers)
            localization_status_.last_observed_marker_ids.push_back(marker.id);
    }
}

void SafetyFramePipeline::updateLocalizationResult(bool localized,
                                                    bool marker_found,
                                                    bool homography_available) {
    std::lock_guard<std::mutex> lock(localization_mutex_);
    localization_status_.configured_marker_id = marker_id_;
    localization_status_.localized = localized;
    localization_status_.active_stream_id = active_stream_.value_or("");
    localization_status_.active_camera_id = active_camera_id_;
    localization_status_.active_channel = active_channel_;
    if (localized) {
        localization_status_.status = "LOCALIZED";
    } else if (!marker_found) {
        localization_status_.status = localization_status_.last_aruco_frame_utc.empty()
                                          ? "WAITING_FOR_ARUCO"
                                          : "MARKER_NOT_DETECTED";
    } else if (!homography_available) {
        localization_status_.status = "HOMOGRAPHY_UNAVAILABLE";
    } else {
        localization_status_.status = "HOMOGRAPHY_UNAVAILABLE";
    }
}

SafetyFramePipeline::LocalizationStatus SafetyFramePipeline::localizationStatus() const {
    std::lock_guard<std::mutex> lock(localization_mutex_);
    return localization_status_;
}

SafetyFramePipeline::PeopleStatus SafetyFramePipeline::peopleStatus(double now_s) const {
    std::lock_guard<std::mutex> lock(localization_mutex_);
    PeopleStatus result;
    result.last_update_utc = people_status_.last_update_utc;
    result.last_update_s = people_status_.last_update_s;
    result.tracks.reserve(people_status_.tracks.size());
    for (const auto& track : people_status_.tracks) {
        if (now_s - track.last_seen_s > people_timeout_sec_) continue;
        result.tracks.push_back(track);
    }
    return result;
}

std::optional<std::string> SafetyFramePipeline::processArucoStreamFrame(const ArucoFrame& frame) {
    if (frame.stream_id.empty() || frame.camera_id.empty() || frame.channel < 1) return std::nullopt;
    recordArucoObservation(frame);
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
    {
        std::lock_guard<std::mutex> lock(localization_mutex_);
        localization_status_.active_stream_id = active_stream_.value_or("");
        localization_status_.active_camera_id = active_camera_id_;
        localization_status_.active_channel = active_channel_;
    }
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
    bool marker_found = false;
    bool homography_available = false;
    if (last_aruco_) {
        for (const auto& marker : last_aruco_->markers) {
            if (marker.id != marker_id_) continue;
            marker_found = true;
            homography_available = homography_.hasStream(last_aruco_->stream_id);
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
    updateLocalizationResult(output.forklift_localized, marker_found, homography_available);

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
        detection.observed_utc = frame.utcTime.empty() ? frame.serverReceivedUtc : frame.utcTime;
        detections.push_back(detection);
    }
    output.transformed_people = detections.size();

    // 사람 트래킹은 지게차 마커가 잠시 사라져도 계속 갱신한다. 위치가 없는
    // 동안에는 위험 판정에 사용하지 않지만, 모니터링에서는 검출 현황을
    // 그대로 확인할 수 있어야 한다.
    const auto tracks = cross_camera_tracker_.update(detections, timestamp_s);
    PeopleStatus people;
    people.last_update_utc = frame.utcTime.empty() ? frame.serverReceivedUtc : frame.utcTime;
    people.last_update_s = timestamp_s;
    people.tracks.reserve(tracks.size());
    for (const auto& track : tracks) {
        PersonStatus person;
        person.track_id = track.track_id;
        person.stream_id = track.stream_id;
        person.camera_id = track.camera_id;
        person.channel = track.channel;
        person.position = track.last_world;
        person.distance_mm = output.forklift_localized
                                 ? euclideanDistance(output.forklift_world, track.last_world)
                                 : -1.0;
        person.last_seen_s = track.last_seen_s;
        person.missed_frames = track.missed_frames;
        person.observed_utc = track.observed_utc;
        people.tracks.push_back(std::move(person));
    }
    {
        std::lock_guard<std::mutex> lock(localization_mutex_);
        people_status_ = std::move(people);
    }

    if (output.forklift_localized) {
        output.nearest = selectNearestPerson(output.forklift_world, tracks,
                                             timestamp_s, track_freshness_sec_);
    }

    output.judgment = judgment_pipeline_.processFrame(
        output.forklift_world, output.forklift_localized, output.nearest);
    output.judgment.result.stream_id = frame.stream_id;
    output.judgment.result.source_camera_id = frame.camera_id;
    output.judgment.result.channel = frame.channel;
    return output;
}

}  // namespace forklift::logic
