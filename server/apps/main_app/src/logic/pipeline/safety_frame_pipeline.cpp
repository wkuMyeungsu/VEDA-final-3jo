#include "logic/pipeline/safety_frame_pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

#include "logging/logger.hpp"

namespace forklift::logic {

SafetyFramePipeline::SafetyFramePipeline(const config::SafetyServerConfig& config,
                                         const config::ForkliftDevice& device,
                                         ISensorReader& sensors,
                                         bool ignore_sensor_input)
    : marker_id_(device.marker_id),
      marker_height_mm_(std::max(0.0, device.marker_height_mm)),
      homography_(config),
      fov_grace_(std::max(std::chrono::milliseconds(0), config.handover.lostGrace())),
      activation_confirm_(std::max(1, config.handover.confirm_frames)),
      people_timeout_sec_(config.tracking.track_timeout_ms / 1000.0),
      world_distance_threshold_mm_(std::max(0.0, config.tracking.world_distance_threshold_mm)),
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
    auto& stream_status = aruco_stream_status_[frame.stream_id];
    stream_status.stream_id = frame.stream_id;
    stream_status.camera_id = frame.camera_id;
    stream_status.channel = frame.channel;
    stream_status.last_frame_utc = frame_utc;
    stream_status.target_marker_visible = target_marker_seen;
    stream_status.marker_ids.clear();
    stream_status.marker_ids.reserve(frame.markers.size());
    for (const auto& marker : frame.markers)
        stream_status.marker_ids.push_back(marker.id);
    if (target_marker_seen)
        stream_status.last_target_marker_seen_utc = frame_utc;
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
    localization_status_.has_position = localized && forklift_sighting_.has_value();
    if (localization_status_.has_position)
        localization_status_.position = forklift_sighting_->pos;
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

void SafetyFramePipeline::refreshGlobalForkliftSighting() {
    std::optional<WorldSighting> newest;
    bool marker_visible = false;
    bool homography_available = false;
    {
        std::lock_guard<std::mutex> lock(localization_mutex_);
        for (const auto& entry : aruco_stream_status_) {
            const auto& status = entry.second;
            if (!status.target_marker_visible) continue;
            marker_visible = true;
            const auto sighting = stream_sightings_.find(entry.first);
            if (sighting == stream_sightings_.end()) continue;
            homography_available = true;
            if (!newest || sighting->second.seen > newest->seen)
                newest = sighting->second;
        }
    }
    // 대상 마커를 못 본 한 스트림의 프레임이 다른 스트림의 유효 관측을 지우면 안 된다.
    // 유효 관측이 하나라도 있으면 가장 최근 관측을 전역 지게차 위치로 채택한다.
    if (newest) forklift_sighting_ = *newest;
    any_target_marker_visible_ = marker_visible;
    any_target_with_homography_ = homography_available;
}

std::optional<WorldPoint> SafetyFramePipeline::resolvedForkliftWorld(
    std::chrono::steady_clock::time_point now) const {
    if (!forklift_sighting_) return std::nullopt;
    if (any_target_with_homography_) return forklift_sighting_->pos;

    // 모든 스트림이 잠깐 마커를 놓친 경우에만 기존 위치를 lost_grace 동안 재사용한다.
    // 다른 스트림이 대상을 보고 있거나 호모그래피가 없는 관측만 있는 경우에는
    // 그 상태를 숨기지 않는다.
    if (!any_target_marker_visible_ && marker_missing_since_ &&
        now >= *marker_missing_since_ && now - *marker_missing_since_ <= fov_grace_)
        return forklift_sighting_->pos;
    return std::nullopt;
}

bool SafetyFramePipeline::anyTargetMarkerVisible() const {
    return any_target_marker_visible_;
}

NearestPersonResult SafetyFramePipeline::nearestAcrossCameras(
    const WorldPoint& forklift, double now_s) const {
    // 트래커 결과를 기본값으로 사용하되, 카메라별 최신 원시 관측도 함께 비교한다.
    // 한 스트림의 마지막 프레임이 다른 스트림의 최신 결과를 덮어쓰지 않게 하는
    // 전역 최소거리 선택이다.
    NearestPersonResult result = selectNearestPerson(
        forklift, latest_tracks_, now_s, people_timeout_sec_);
    for (const auto& entry : latest_people_observations_) {
        const auto& observation = entry.second;
        if (observation.timestamp_s < 0.0 || now_s < observation.timestamp_s ||
            now_s - observation.timestamp_s > people_timeout_sec_)
            continue;
        for (const auto& detection : observation.detections) {
            const double distance = euclideanDistance(forklift, detection.world);
            if (distance >= result.distance_mm) continue;

            result.found = true;
            result.track_id = -1;
            result.stream_id = detection.stream_id;
            result.camera_id = detection.camera_id;
            result.channel = detection.channel;
            result.position = detection.world;
            result.distance_mm = distance;

            // 원시 관측에도 가능한 경우 기존 트랙 ID를 붙여 UI/로그 식별자를 유지한다.
            double best_match = world_distance_threshold_mm_;
            if (best_match > 0.0) {
                for (const auto& track : latest_tracks_) {
                    if (now_s < track.last_seen_s ||
                        now_s - track.last_seen_s > people_timeout_sec_)
                        continue;
                    const double match_distance =
                        euclideanDistance(track.last_world, detection.world);
                    if (match_distance <= best_match) {
                        best_match = match_distance;
                        result.track_id = track.track_id;
                    }
                }
            }
        }
    }
    return result;
}

SafetyFramePipeline::LocalizationStatus SafetyFramePipeline::localizationStatus() const {
    std::lock_guard<std::mutex> lock(localization_mutex_);
    LocalizationStatus result = localization_status_;
    result.aruco_streams.clear();
    result.aruco_streams.reserve(aruco_stream_status_.size());
    for (const auto& entry : aruco_stream_status_)
        result.aruco_streams.push_back(entry.second);
    return result;
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

    // 1) 이 화면의 지게차 마커 중심을 월드 좌표로 바꿔 스트림별 관측을 갱신한다.
    //    최종 위치는 아래에서 모든 스트림의 유효 관측을 합쳐 선택한다.
    bool marker_here = false;
    bool homography_here = false;
    bool localized_here = false;
    for (const auto& marker : frame.markers) {
        if (marker.id != marker_id_) continue;
        marker_here = true;
        homography_here = homography_.hasStream(frame.stream_id);
        if (marker.corners.empty()) {
            stream_sightings_.erase(frame.stream_id);
            break;
        }
        common::PixelPoint center;
        for (const auto& corner : marker.corners) {
            center.x += corner.x;
            center.y += corner.y;
        }
        center.x /= static_cast<float>(marker.corners.size());
        center.y /= static_cast<float>(marker.corners.size());
        if (auto world = homography_.pixelToWorld(frame.stream_id, center, marker_height_mm_)) {
            stream_sightings_[frame.stream_id] = WorldSighting{*world, now, frame.stream_id};
            localized_here = true;
        } else {
            stream_sightings_.erase(frame.stream_id);
        }
        break;
    }
    if (!marker_here) stream_sightings_.erase(frame.stream_id);
    refreshGlobalForkliftSighting();
    if (marker_here) {
        marker_missing_since_.reset();
        // ArUco 입력만으로 대상 ID와 월드 좌표가 이미 확정됐는데도 다음 객체
        // 메타데이터가 올 때까지 WAITING_FOR_ARUCO로 남지 않게 즉시 반영한다.
        // 다른 채널의 빈 ArUco 프레임은 이 상태를 다시 미검출로 덮어쓰지 않는다.
    }
    const bool global_localized = resolvedForkliftWorld(now).has_value();
    if (marker_here || anyTargetMarkerVisible() || !global_localized)
        updateLocalizationResult(global_localized, anyTargetMarkerVisible(),
                                 global_localized || homography_here);

    // 2) 활성 스트림이 없으면: 같은 화면에서 연속 확정된 뒤 채택한다(오검출 방지).
    if (!active_stream_) {
        if (marker_here && localized_here && forklift_sighting_ &&
            forklift_sighting_->stream_id == frame.stream_id) {
            const int streak = ++activation_streaks_[frame.stream_id];
            if (streak >= activation_confirm_) {
                activateStream(frame.stream_id, frame.camera_id, frame.channel, &frame);
                return active_stream_;
            }
        } else {
            // 다른 채널의 미검출은 현재 후보 채널의 연속 확인을 깨지 않는다.
            // 자신의 프레임에서 마커를 놓친 경우에만 해당 카운터를 초기화한다.
            activation_streaks_[frame.stream_id] = 0;
        }
        return std::nullopt;
    }

    // 3) 액티브 유지 판단: 직접 보이거나, 추적 위치가 액티브 화면 안에 있으면 유지.
    //    "같은 마커가 여러 화면에 동시 잡힘"은 여기서 전환 사유가 되지 않는다.
    bool in_fov = false;
    if (frame.stream_id == *active_stream_) {
        if (marker_here) {
            in_fov = true;
            marker_missing_since_.reset();
        } else if (!marker_missing_since_) {
            marker_missing_since_ = now;
        }
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
    activation_streaks_.clear();
    marker_missing_since_.reset();
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
        output = processAggregatedFrame(timestamp_s);
    }
    output.judgment.result.stream_id = frame.stream_id;
    output.judgment.result.source_camera_id = frame.camera_id;
    output.judgment.result.channel = frame.channel;
    return output;
}

void SafetyFramePipeline::updateObjectFrame(const MetadataFrame& frame, double timestamp_s) {
    if (frame.stream_id.empty() || frame.camera_id.empty() || frame.channel < 1) return;
    ObjectFrameOutput ignored;
    processValidObjectFrame(frame, timestamp_s, ignored, false);
}

SafetyFramePipeline::ObjectFrameOutput SafetyFramePipeline::processAggregatedFrame(
    double timestamp_s) {
    ObjectFrameOutput output;
    const auto now = std::chrono::steady_clock::now();
    const auto forklift_world = resolvedForkliftWorld(now);
    output.forklift_localized = forklift_world.has_value();
    if (forklift_world) output.forklift_world = *forklift_world;

    updateLocalizationResult(output.forklift_localized, anyTargetMarkerVisible(),
                             any_target_with_homography_);
    if (output.forklift_localized)
        output.nearest = nearestAcrossCameras(output.forklift_world, timestamp_s);
    output.judgment = judgment_pipeline_.processFrame(
        output.forklift_world, output.forklift_localized, output.nearest);
    // 최종 판정은 전체 스트림의 최근접 관측으로 계산한다. 결과 식별자도
    // 활성 핸드오버 채널이 아니라 실제로 선택된 관측의 출처를 가리켜야
    // CH_02/CH_03 중 어느 입력이 집계에 기여했는지 운영 로그에서 확인할 수 있다.
    if (output.nearest.found) {
        output.judgment.result.stream_id = output.nearest.stream_id;
        output.judgment.result.source_camera_id = output.nearest.camera_id;
        output.judgment.result.channel = output.nearest.channel;
    } else {
        output.judgment.result.stream_id = active_stream_.value_or("");
        output.judgment.result.source_camera_id = active_camera_id_;
        output.judgment.result.channel = active_channel_;
    }
    return output;
}

void SafetyFramePipeline::processValidObjectFrame(
    const MetadataFrame& frame, double timestamp_s, ObjectFrameOutput& output,
    bool evaluate) {

    const auto now = std::chrono::steady_clock::now();
    const auto forklift_world = resolvedForkliftWorld(now);
    const bool marker_found = anyTargetMarkerVisible();
    const bool homography_available = any_target_with_homography_;

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
    latest_people_observations_[frame.stream_id] =
        StreamPeopleObservation{timestamp_s, detections};

    // 사람 트래킹은 지게차 마커가 잠시 사라져도 계속 갱신한다. 위치가 없는
    // 동안에는 위험 판정에 사용하지 않지만, 모니터링에서는 검출 현황을
    // 그대로 확인할 수 있어야 한다.
    const auto tracks = cross_camera_tracker_.update(detections, timestamp_s);
    latest_tracks_ = tracks;
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
        output.nearest = nearestAcrossCameras(output.forklift_world, timestamp_s);
    }

    if (evaluate) {
        output.judgment = judgment_pipeline_.processFrame(
            output.forklift_world, output.forklift_localized, output.nearest);
    }
}

}  // namespace forklift::logic
