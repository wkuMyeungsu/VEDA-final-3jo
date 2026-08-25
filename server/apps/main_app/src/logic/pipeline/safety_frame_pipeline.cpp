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
      fov_grace_(std::max(std::chrono::milliseconds(0), config.handover.lostGrace())),
      activation_confirm_(std::max(1, config.handover.confirm_frames)),
      track_freshness_sec_(config.tracking.track_freshness_ms / 1000.0),
      people_timeout_sec_(config.tracking.track_timeout_ms / 1000.0),
      cross_camera_tracker_(config.tracking.iou_threshold,
                            config.tracking.world_distance_threshold_mm,
                            config.tracking.track_timeout_ms / 1000.0),
      judgment_pipeline_(device.terminal_id, sensors, config.danger_judgment,
                         device.collision_radius_mm, config.handover.lostGrace(),
                         ignore_sensor_input) {
    localization_status_.configured_marker_id = marker_id_;
    for (const auto& stream : config.streams)
        stream_identity_[stream.stream_id] = {stream.camera_id, stream.channel};
}

void SafetyFramePipeline::recordArucoObservation(const ArucoFrame& frame) {
    std::lock_guard<std::mutex> lock(localization_mutex_);
    // 프레임 발생 시각이 없으면 서버 수신 시각으로 대체한다.
    const std::string& frame_utc =
        !frame.utcTime.empty() ? frame.utcTime : frame.serverReceivedUtc;
    localization_status_.configured_marker_id = marker_id_;
    localization_status_.last_aruco_frame_utc = frame_utc;
    localization_status_.last_aruco_frame_stream_id = frame.stream_id;
    localization_status_.last_aruco_frame_channel = frame.channel;

    bool target_marker_seen = false;
    for (const auto& marker : frame.markers) {
        if (marker.id == marker_id_) target_marker_seen = true;
    }
    if (target_marker_seen) {
        localization_status_.last_target_marker_seen_utc = frame_utc;
        localization_status_.last_target_marker_stream_id = frame.stream_id;
        localization_status_.last_target_marker_channel = frame.channel;
    }
    if (!frame.markers.empty()) {
        localization_status_.last_observed_markers_utc = frame_utc;
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
    const auto now = std::chrono::steady_clock::now();

    // 1) 이 화면의 지게차 마커 중심을 월드 좌표로 바꿔 단일 추적점을 갱신한다.
    //    어느 스트림의 관측이든 같은 지게차이므로 하나의 위치로 융합된다.
    bool marker_here = false;
    for (const auto& marker : frame.markers) {
        if (marker.id != marker_id_) continue;
        marker_here = true;
        common::PixelPoint center;
        for (const auto& corner : marker.corners) {
            center.x += corner.x;
            center.y += corner.y;
        }
        center.x /= static_cast<float>(marker.corners.size());
        center.y /= static_cast<float>(marker.corners.size());
        if (auto world = homography_.pixelToWorld(frame.stream_id, center))
            forklift_sighting_ = WorldSighting{*world, now, frame.stream_id};
        break;
    }

    // 2) 활성 스트림이 없으면: 같은 화면에서 연속 확정된 뒤 채택한다(오검출 방지).
    if (!active_stream_) {
        if (marker_here && forklift_sighting_ && forklift_sighting_->stream_id == frame.stream_id) {
            if (activation_streak_stream_ != frame.stream_id) {
                activation_streak_stream_ = frame.stream_id;
                activation_streak_ = 0;
            }
            ++activation_streak_;
            if (activation_streak_ >= activation_confirm_) {
                activateStream(frame.stream_id, frame.camera_id, frame.channel, &frame);
                return active_stream_;
            }
        } else {
            activation_streak_ = 0;
            activation_streak_stream_.clear();
        }
        return std::nullopt;
    }

    // 3) 액티브 유지 판단: 직접 보이거나, 추적 위치가 액티브 화면 안에 있으면 유지.
    //    "같은 마커가 여러 화면에 동시 잡힘"은 여기서 전환 사유가 되지 않는다.
    bool in_fov = false;
    if (frame.stream_id == *active_stream_) {
        if (marker_here) in_fov = true;
        last_aruco_ = frame;
    }
    if (!in_fov && forklift_sighting_) {
        const auto size = homography_.imageSize(*active_stream_);
        if (auto pixel = homography_.worldToPixel(*active_stream_, forklift_sighting_->pos)) {
            in_fov = size.has_value() && pixel->x >= 0.0f && pixel->y >= 0.0f &&
                     pixel->x < static_cast<float>(size->first) &&
                     pixel->y < static_cast<float>(size->second);
        }
    }
    if (in_fov) {
        out_of_fov_since_.reset();
        return std::nullopt;
    }

    // 4) FOV 이탈 유예 시간 경과 후에만 전환한다.
    if (!out_of_fov_since_) out_of_fov_since_ = now;
    if (now - *out_of_fov_since_ < fov_grace_) return std::nullopt;

    std::string target;
    if (marker_here && frame.stream_id != *active_stream_) target = frame.stream_id;
    else if (forklift_sighting_ && forklift_sighting_->stream_id != *active_stream_)
        target = forklift_sighting_->stream_id;
    const auto identity = stream_identity_.find(target);
    if (target.empty() || target == *active_stream_ || identity == stream_identity_.end())
        return std::nullopt;

    activateStream(target, identity->second.first, identity->second.second, nullptr);
    return active_stream_;
}

void SafetyFramePipeline::activateStream(const std::string& stream_id,
                                         const std::string& camera_id, int channel,
                                         const ArucoFrame* triggering_frame) {
    active_stream_ = stream_id;
    active_camera_id_ = camera_id;
    active_channel_ = channel;
    out_of_fov_since_.reset();
    activation_streak_ = 0;
    activation_streak_stream_.clear();
    judgment_pipeline_.setActiveStream(stream_id, camera_id, channel);
    if (triggering_frame) last_aruco_ = *triggering_frame;
    std::lock_guard<std::mutex> lock(localization_mutex_);
    localization_status_.active_stream_id = active_stream_.value_or("");
    localization_status_.active_camera_id = active_camera_id_;
    localization_status_.active_channel = active_channel_;
}


SafetyFramePipeline::ObjectFrameOutput SafetyFramePipeline::processObjectFrame(
    const MetadataFrame& frame, double timestamp_s) {
    ObjectFrameOutput output;
    if (!frame.stream_id.empty() && !frame.camera_id.empty() && frame.channel >= 1) {
        processValidObjectFrame(frame, timestamp_s, output);
    } else {
        output.judgment = judgment_pipeline_.processFrame({}, false, {});
    }
    output.judgment.result.stream_id = frame.stream_id;
    output.judgment.result.source_camera_id = frame.camera_id;
    output.judgment.result.channel = frame.channel;
    return output;
}

void SafetyFramePipeline::processValidObjectFrame(
    const MetadataFrame& frame, double timestamp_s, ObjectFrameOutput& output) {

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
}

}  // namespace forklift::logic
