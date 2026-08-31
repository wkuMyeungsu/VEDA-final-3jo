#include "logic/pipeline/safety_frame_pipeline.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
      view_freshness_(std::max(std::chrono::milliseconds(0),
                               std::chrono::milliseconds(config.tracking.track_freshness_ms))),
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
    if (config.site_map.configured()) {
        work_area_boundary_.reserve(config.site_map.boundary.size());
        for (const auto& point : config.site_map.boundary)
            work_area_boundary_.push_back({point.x_mm, point.y_mm});
    }
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

    auto& stream_status = aruco_stream_status_[frame.stream_id];
    stream_status.stream_id = frame.stream_id;
    stream_status.camera_id = frame.camera_id;
    stream_status.channel = frame.channel;
    stream_status.last_frame_utc = frame_utc;
    stream_status.marker_ids.clear();
    stream_status.marker_ids.reserve(frame.markers.size());
    for (const auto& marker : frame.markers)
        stream_status.marker_ids.push_back(marker.id);
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

void SafetyFramePipeline::refreshGlobalForkliftSighting(
    std::chrono::steady_clock::time_point now) {
    const WorldSighting* fresh = bestFreshSighting(now);
    // 대상 마커를 못 본 한 스트림의 프레임이 다른 스트림의 유효 관측을 지우면 안 된다.
    // 유효 관측이 있으면 화면에서 더 크게 보이는 쪽을 지게차 위치로 채택한다.
    if (fresh) forklift_sighting_ = *fresh;
    any_target_marker_visible_ = fresh != nullptr;
    any_target_with_homography_ = fresh != nullptr;
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

bool SafetyFramePipeline::insideWorkArea(const WorldPoint& point) const {
    return pointInPolygon(point, work_area_boundary_);
}

NearestPersonResult SafetyFramePipeline::nearestAcrossCameras(
    const WorldPoint& forklift, double now_s) const {
    // 트래커 결과를 기본값으로 사용하되, 카메라별 최신 원시 관측도 함께 비교한다.
    // 한 스트림의 마지막 프레임이 다른 스트림의 최신 결과를 덮어쓰지 않게 하는
    // 전역 최소거리 선택이다. 작업 구역 밖 사람은 후보에서 뺀다.
    std::vector<Track> inside_tracks;
    inside_tracks.reserve(latest_tracks_.size());
    for (const auto& track : latest_tracks_) {
        if (insideWorkArea(track.last_world)) inside_tracks.push_back(track);
    }
    NearestPersonResult result = selectNearestPerson(
        forklift, inside_tracks, now_s, people_timeout_sec_);
    for (const auto& entry : latest_people_observations_) {
        const auto& observation = entry.second;
        if (observation.timestamp_s < 0.0 || now_s < observation.timestamp_s ||
            now_s - observation.timestamp_s > people_timeout_sec_)
            continue;
        for (const auto& detection : observation.detections) {
            if (!insideWorkArea(detection.world)) continue;
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

double SafetyFramePipeline::markerPixelArea(
    const std::array<forklift::common::PixelPoint, 4>& corners) {
    double sum = 0.0;
    for (std::size_t index = 0; index < corners.size(); ++index) {
        const auto& a = corners[index];
        const auto& b = corners[(index + 1) % corners.size()];
        sum += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return std::abs(sum) * 0.5;
}

std::optional<SafetyFramePipeline::WorldSighting> SafetyFramePipeline::extractForkliftSighting(
    const ArucoFrame& frame, std::chrono::steady_clock::time_point now) const {
    struct Candidate {
        WorldPoint world{};
        double area = 0.0;
    };
    std::vector<Candidate> candidates;
    for (const auto& marker : frame.markers) {
        if (marker.id != marker_id_ || marker.corners.empty()) continue;
        common::PixelPoint center{};
        for (const auto& corner : marker.corners) {
            center.x += corner.x;
            center.y += corner.y;
        }
        center.x /= static_cast<float>(marker.corners.size());
        center.y /= static_cast<float>(marker.corners.size());
        const auto world = homography_.pixelToWorld(frame.stream_id, center, marker_height_mm_);
        if (!world) continue;
        Candidate candidate;
        candidate.world = *world;
        candidate.area = markerPixelArea(marker.corners);
        candidates.push_back(candidate);
    }
    if (candidates.empty()) return std::nullopt;

    const auto best = std::max_element(
        candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.area < b.area; });
    WorldSighting sighting;
    sighting.pos = best->world;
    sighting.seen = now;
    sighting.stream_id = frame.stream_id;
    sighting.pixel_area = best->area;
    return sighting;
}

const SafetyFramePipeline::WorldSighting* SafetyFramePipeline::bestFreshSighting(
    std::chrono::steady_clock::time_point now) const {
    const WorldSighting* best = nullptr;
    for (const auto& entry : stream_sightings_) {
        const auto& sighting = entry.second;
        if (now < sighting.seen) continue;
        if (view_freshness_.count() > 0 && now - sighting.seen > view_freshness_) continue;
        if (!best || sighting.pixel_area > best->pixel_area) {
            best = &sighting;
            continue;
        }
        if (sighting.pixel_area < best->pixel_area) continue;
        // 크기가 같으면 이미 배정된 화면을 유지해 카메라 사이 진동을 막는다.
        if (active_stream_ && sighting.stream_id == *active_stream_) best = &sighting;
        else if (!active_stream_ || best->stream_id != *active_stream_) {
            if (sighting.seen > best->seen) best = &sighting;
        }
    }
    return best;
}

std::optional<std::string> SafetyFramePipeline::selectAssignment(
    const ArucoFrame& frame, std::chrono::steady_clock::time_point now) {
    const WorldSighting* best = bestFreshSighting(now);
    if (!best) return std::nullopt;

    // 한 프레임만 크게 잡히거나 마커 면적이 조금 요동쳐도 화면이 바뀌지 않게,
    // 현재 화면이 아직 유효하면 1.3배 이상 더 클 때만 바꾼다.
    if (active_stream_) {
        const auto active_it = stream_sightings_.find(*active_stream_);
        if (active_it != stream_sightings_.end()) {
            const auto& active = active_it->second;
            const bool active_fresh =
                now >= active.seen &&
                (view_freshness_.count() == 0 || now - active.seen <= view_freshness_);
            if (active_fresh && best->stream_id != *active_stream_ &&
                best->pixel_area < active.pixel_area * 1.3)
                best = &active;
        }
    }

    const auto identity = stream_identity_.find(best->stream_id);
    if (identity == stream_identity_.end()) return std::nullopt;
    if (!active_stream_ || *active_stream_ != best->stream_id) {
        activateStream(best->stream_id, identity->second.first, identity->second.second,
                       best->stream_id == frame.stream_id ? &frame : nullptr);
    }
    return best->stream_id;
}

std::optional<std::string> SafetyFramePipeline::processArucoStreamFrame(const ArucoFrame& frame) {
    if (frame.stream_id.empty() || frame.camera_id.empty() || frame.channel < 1) return std::nullopt;
    recordArucoObservation(frame);
    const auto now = std::chrono::steady_clock::now();

    const auto extracted = extractForkliftSighting(frame, now);
    const bool homography_here = homography_.hasStream(frame.stream_id);
    if (extracted) {
        stream_sightings_[frame.stream_id] = *extracted;
        last_aruco_ = frame;
    } else {
        stream_sightings_.erase(frame.stream_id);
    }
    {
        std::lock_guard<std::mutex> lock(localization_mutex_);
        auto& stream_status = aruco_stream_status_[frame.stream_id];
        stream_status.target_marker_visible = extracted.has_value();
        if (extracted) {
            const std::string& frame_utc =
                !frame.utcTime.empty() ? frame.utcTime : frame.serverReceivedUtc;
            stream_status.last_target_marker_seen_utc = frame_utc;
            localization_status_.last_target_marker_seen_utc = frame_utc;
            localization_status_.last_target_marker_stream_id = frame.stream_id;
            localization_status_.last_target_marker_channel = frame.channel;
        }
    }

    refreshGlobalForkliftSighting(now);
    if (any_target_with_homography_) marker_missing_since_.reset();
    else if (!marker_missing_since_) marker_missing_since_ = now;

    const bool global_localized = resolvedForkliftWorld(now).has_value();
    if (extracted || anyTargetMarkerVisible() || !global_localized)
        updateLocalizationResult(global_localized, anyTargetMarkerVisible(),
                                 global_localized || (extracted.has_value() && homography_here));

    return selectAssignment(frame, now);
}

void SafetyFramePipeline::activateStream(const std::string& stream_id,
                                         const std::string& camera_id, int channel,
                                         const ArucoFrame* triggering_frame) {
    active_stream_ = stream_id;
    active_camera_id_ = camera_id;
    active_channel_ = channel;
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
    // Qt 운전자 단말은 stream_id == 활성 화면일 때만 HUD/FPGA에 위험도를 반영한다.
    // 최근접 사람이 다른 채널에 있어도 공지 식별자는 배정 화면을 쓴다.
    // 관측 출처는 source_camera_id에만 남겨 이벤트 DB/서버 로그가 추적할 수 있게 한다.
    output.judgment.result.stream_id = active_stream_.value_or("");
    output.judgment.result.camera_id = active_camera_id_;
    output.judgment.result.channel = active_channel_;
    if (output.nearest.found)
        output.judgment.result.source_camera_id = output.nearest.camera_id;
    else
        output.judgment.result.source_camera_id = active_camera_id_;
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
