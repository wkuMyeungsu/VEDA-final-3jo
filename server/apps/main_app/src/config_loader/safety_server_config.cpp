#include "config_loader/safety_server_config.hpp"
#include "logging/logger.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <cstdlib>

#include "common/platform.hpp"
#include <nlohmann/json.hpp>

namespace forklift::config {
namespace {
using nlohmann::json;

[[noreturn]] void schema(const std::string& path, const std::string& detail) {
    throw SafetyServerConfigError(SafetyServerConfigError::Code::SchemaInvalid, path, detail);
}

const json& object(const json& parent, const char* key, const std::string& path) {
    auto it = parent.find(key);
    if (it == parent.end() || !it->is_object()) schema(path, std::string("\"") + key + "\" 항목은 객체여야 함");
    return *it;
}

template <typename T>
T value(const json& parent, const char* section, const char* key, const std::string& path) {
    auto it = parent.find(key);
    if (it == parent.end()) schema(path, std::string("필수 항목 누락: \"") + section + "." + key + "\"");
    try { return it->get<T>(); }
    catch (...) { schema(path, std::string("항목 형식 오류: \"") + section + "." + key + "\""); }
}

void positive(double v, const char* key, const std::string& path, bool zero_ok = false) {
    if (!std::isfinite(v) || v < 0.0 || (!zero_ok && v == 0.0))
        schema(path, std::string("\"") + key + "\" 값이 허용 범위를 벗어남");
}

std::vector<SiteMapPoint> mapPolygon(const json& value, const std::string& path,
                                     const std::string& field) {
    if (!value.is_array() || value.size() < 3)
        schema(path, "\"" + field + "\"는 점 3개 이상의 배열이어야 함");
    std::vector<SiteMapPoint> points;
    points.reserve(value.size());
    for (const auto& point : value) {
        if (!point.is_array() || point.size() != 2 || !point[0].is_number() ||
            !point[1].is_number())
            schema(path, "\"" + field + "\" 좌표는 [x_mm, y_mm] 형식이어야 함");
        const double x = point[0].get<double>();
        const double y = point[1].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y))
            schema(path, "\"" + field + "\" 좌표는 유한한 수여야 함");
        points.push_back({x, y});
    }
    double twice_area = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& current = points[index];
        const auto& next = points[(index + 1) % points.size()];
        twice_area += current.x_mm * next.y_mm - next.x_mm * current.y_mm;
    }
    if (std::abs(twice_area) < 1e-6)
        schema(path, "\"" + field + "\" 면적은 0보다 커야 함");
    return points;
}
}  // namespace

SafetyServerConfigError::SafetyServerConfigError(Code code, std::string path, const std::string& detail)
    : std::runtime_error("[safety_server_config] " + toString(code) + " (" + path + "): " + detail),
      code_(code), path_(std::move(path)) {}

std::string toString(SafetyServerConfigError::Code code) {
    switch (code) {
        case SafetyServerConfigError::Code::FileNotFound: return "FILE_NOT_FOUND";
        case SafetyServerConfigError::Code::ParseFailed: return "PARSE_FAILED";
        case SafetyServerConfigError::Code::SchemaInvalid: return "SCHEMA_INVALID";
    }
    return "UNKNOWN";
}

static std::filesystem::path getExecutableDirectory() {
    return forklift::platform::executableDirectory();
}

std::string resolveConfigDirectory() {
    const auto exe_dir = getExecutableDirectory();
    const std::filesystem::path exe_candidates[] = {
        exe_dir / ".." / ".." / "config" / "safety",          // build/apps/main_app -> server/config/safety
        exe_dir / ".." / ".." / ".." / "config" / "safety",   // deeper build folder
        exe_dir / ".." / "config" / "safety",
        exe_dir / "config" / "safety",
    };
    for (const auto& candidate : exe_candidates) {
        if (std::filesystem::is_directory(candidate)) {
            return candidate.lexically_normal().string();
        }
    }

    const char* candidates[] = {
        "/etc/forklift_safety/safety",
        "/etc/forklift_safety",
        "../config/safety",
        "server/config/safety",
        "config/safety",
        "../../config/safety",
    };
    for (const char* candidate : candidates) {
        if (std::filesystem::is_directory(candidate)) return candidate;
    }
    return candidates[2]; // 기본값: server/config/safety
}

std::string resolveCommonConfigDirectory(const std::string& config_dir) {
    const std::filesystem::path app_dir(config_dir);
    if (config_dir == "/etc/forklift_safety/safety" || config_dir == "/etc/forklift_safety") {
        return "/etc/forklift_safety";
    }
    const auto sibling_common = app_dir.parent_path().parent_path() / "config";
    if (std::filesystem::is_directory(sibling_common)) return sibling_common.lexically_normal().string();

    const auto exe_dir = getExecutableDirectory();
    const std::filesystem::path exe_candidates[] = {
        exe_dir / ".." / ".." / "config",
        exe_dir / ".." / "config",
        exe_dir / "config",
    };
    for (const auto& candidate : exe_candidates) {
        if (std::filesystem::is_directory(candidate)) {
            return candidate.lexically_normal().string();
        }
    }

    const char* candidates[] = {
        "/etc/forklift_safety",
        "../config",
        "server/config",
        "config",
        "../../config",
    };
    for (const char* candidate : candidates) {
        if (std::filesystem::is_directory(candidate)) return candidate;
    }
    return candidates[1]; // 기본값: server/config
}

SafetyServerConfig loadMultiCameraServerConfigImpl(const std::string& config_dir,
                                                   const std::string& common_config_dir) {
    // 위험·센서·MQTT 정책은 config/safety에 두고, 카메라·단말 목록과 H는
    // 두 앱이 함께 쓰는 server/config에서 읽는다.
    const std::filesystem::path dir(config_dir);
    const std::filesystem::path common_dir(common_config_dir.empty() ? config_dir : common_config_dir);
    const std::filesystem::path camera_path = common_dir / "camera_list.json";
    const std::filesystem::path model_path = common_dir / "camera_model.json";
    const std::filesystem::path device_path = common_dir / "forklift_device_config.json";
    const std::filesystem::path site_map_path = common_dir / "site_map.json";
    const std::filesystem::path danger_path = dir / "danger_judgment_config.json";
    const std::filesystem::path system_path = dir / "system_config.json";

    auto read = [](const std::filesystem::path& path) {
        std::ifstream input(path);
        if (!input) throw SafetyServerConfigError(SafetyServerConfigError::Code::FileNotFound,
                                                    path.string(), "운영 설정 파일을 열 수 없음");
        json value;
        try { input >> value; }
        catch (const json::parse_error& e) {
            throw SafetyServerConfigError(SafetyServerConfigError::Code::ParseFailed,
                                          path.string(), e.what());
        }
        if (!value.is_object()) schema(path.string(), "최상위 JSON은 객체여야 함");
        return value;
    };
    const auto cameras = read(camera_path);
    const auto models = read(model_path);
    const auto devices = read(device_path);
    const auto danger = read(danger_path);
    const auto system = read(system_path);
    SafetyServerConfig c;
    c.source_path = camera_path.string();

    // 공장 외곽과 가림 구역은 카메라 투영값이 아니라 같은 mm 월드 좌표계의
    // 별도 운영 설정이다. 파일이 없으면 기존 설치와 호환되게 지도만 비활성화한다.
    if (std::filesystem::exists(site_map_path)) {
        const auto site_map = read(site_map_path);
        if (value<std::string>(site_map, "site_map", "unit", site_map_path.string()) != "mm")
            schema(site_map_path.string(), "unit은 mm여야 함");
        c.site_map.name = site_map.value("name", std::string("작업 구역"));
        c.site_map.boundary = mapPolygon(
            site_map.at("boundary"), site_map_path.string(), "boundary");
        const auto& zones = site_map.value("zones", json::array());
        if (!zones.is_array()) schema(site_map_path.string(), "zones는 배열이어야 함");
        std::set<std::string> zone_ids;
        for (const auto& item : zones) {
            if (!item.is_object()) schema(site_map_path.string(), "zone은 객체여야 함");
            SiteMapZone zone;
            zone.id = value<std::string>(item, "zones", "id", site_map_path.string());
            zone.label = item.value("label", zone.id);
            zone.kind = value<std::string>(item, "zones", "kind", site_map_path.string());
            if (zone.id.empty() || !zone_ids.insert(zone.id).second)
                schema(site_map_path.string(), "zone id가 비었거나 중복됨");
            if (zone.kind != "excluded" && zone.kind != "blind")
                schema(site_map_path.string(), "zone kind는 excluded 또는 blind여야 함");
            zone.polygon = mapPolygon(
                item.at("polygon"), site_map_path.string(), "zones[].polygon");
            c.site_map.zones.push_back(std::move(zone));
        }
        if (site_map.contains("H_shared_to_site")) {
            const auto& matrix = site_map.at("H_shared_to_site");
            if (!matrix.is_array() || matrix.size() != 3)
                schema(site_map_path.string(), "H_shared_to_site는 3x3 행렬이어야 함");
            for (int row = 0; row < 3; ++row) {
                if (!matrix.at(row).is_array() || matrix.at(row).size() != 3)
                    schema(site_map_path.string(), "H_shared_to_site는 3x3 행렬이어야 함");
                for (int col = 0; col < 3; ++col) {
                    if (!matrix.at(row).at(col).is_number())
                        schema(site_map_path.string(), "H_shared_to_site에 유효하지 않은 숫자가 있음");
                    const double v = matrix.at(row).at(col).get<double>();
                    if (!std::isfinite(v))
                        schema(site_map_path.string(), "H_shared_to_site에 NaN 또는 Inf가 있음");
                    c.site_map.h_shared_to_site[static_cast<std::size_t>(row * 3 + col)] = v;
                }
            }
            c.site_map.has_shared_to_site = true;
        }
    }

    const auto& unit = object(danger, "units", danger_path.string());
    if (value<std::string>(unit, "units", "world", danger_path.string()) != "mm" ||
        value<std::string>(unit, "units", "distance", danger_path.string()) != "mm")
        schema(danger_path.string(), "world/distance 단위는 mm여야 함");
    const auto& d = object(danger, "danger_judgment", danger_path.string());
    c.danger_judgment.caution_threshold_mm = value<double>(d,"danger_judgment","caution_threshold_mm",danger_path.string());
    c.danger_judgment.danger_threshold_mm = value<double>(d,"danger_judgment","danger_threshold_mm",danger_path.string());
    c.danger_judgment.emergency_threshold_mm = value<double>(d,"danger_judgment","emergency_threshold_mm",danger_path.string());
    c.danger_judgment.emergency_release_margin_mm = value<double>(d,"danger_judgment","emergency_release_margin_mm",danger_path.string());
    c.danger_judgment.tof_caution_mm = value<double>(d,"danger_judgment","tof_caution_mm",danger_path.string());
    c.danger_judgment.tof_danger_mm = value<double>(d,"danger_judgment","tof_danger_mm",danger_path.string());
    c.danger_judgment.impact_accel_threshold_g = value<double>(d,"danger_judgment","impact_accel_threshold_g",danger_path.string());
    positive(c.danger_judgment.caution_threshold_mm, "caution_threshold_mm", danger_path.string());
    positive(c.danger_judgment.danger_threshold_mm, "danger_threshold_mm", danger_path.string());
    positive(c.danger_judgment.emergency_threshold_mm, "emergency_threshold_mm", danger_path.string());
    positive(c.danger_judgment.emergency_release_margin_mm, "emergency_release_margin_mm", danger_path.string(), true);
    positive(c.danger_judgment.tof_caution_mm, "tof_caution_mm", danger_path.string());
    positive(c.danger_judgment.tof_danger_mm, "tof_danger_mm", danger_path.string());
    positive(c.danger_judgment.impact_accel_threshold_g, "impact_accel_threshold_g", danger_path.string());
    if (!(c.danger_judgment.emergency_threshold_mm < c.danger_judgment.danger_threshold_mm &&
          c.danger_judgment.danger_threshold_mm < c.danger_judgment.caution_threshold_mm))
        schema(danger_path.string(), "거리 임계값은 emergency < danger < caution 순서여야 함");
    if (c.danger_judgment.tof_danger_mm > c.danger_judgment.tof_caution_mm)
        schema(danger_path.string(), "ToF 임계값은 danger <= caution 순서여야 함");

    // 카메라 모델이 허용하는 채널 수를 먼저 읽는다. 카메라 파일에 임의의 채널을
    // 적어 두어도 모델 정의와 맞지 않으면 기동하지 않는다.
    const auto& model_list = models.at("models");
    if (!model_list.is_array() || model_list.empty()) schema(model_path.string(), "models는 비어 있지 않은 배열이어야 함");
    std::map<std::string, int> model_channels;
    for (const auto& model : model_list) {
        const std::string name = model.at("model").get<std::string>();
        const int count = model.at("channel_count").get<int>();
        if (name.empty() || count < 1 || model_channels.count(name)) schema(model_path.string(), "카메라 모델 정의 오류/중복");
        model_channels[name] = count;
    }
    const auto& list = cameras.at("cameras");
    if (!list.is_array() || list.empty()) schema(camera_path.string(), "cameras는 비어 있지 않은 배열이어야 함");
    std::map<std::string, bool> camera_ids, stream_ids;
    std::map<std::pair<std::string,int>, bool> channels;
    // camera_id는 물리 장비 이름이고, stream_id는 장비+채널로 서버가 자동 생성한다.
    // 그래서 서로 다른 장비의 channel 1도 서로 충돌하지 않는다.
    for (const auto& cam : list) {
        const std::string camera_id = cam.at("camera_id").get<std::string>();
        const std::string camera_model = cam.at("model").get<std::string>();
        const auto model_it = model_channels.find(camera_model);
        if (model_it == model_channels.end())
            schema(camera_path.string(), "지원하지 않는 카메라 모델: " + camera_model);
        const int expected_channels = model_it->second;
        if (camera_id.empty() || camera_ids[camera_id]) schema(camera_path.string(), "camera_id 중복");
        camera_ids[camera_id] = true;
        const auto& channel_list = cam.at("channels");
        if (!channel_list.is_array() || channel_list.empty()) schema(camera_path.string(), "channels 오류");
        std::set<int> configured_channels;
        for (const auto& item : channel_list) {
            CameraStreamConfig s;
            s.camera_id = camera_id;
            s.camera_model = camera_model;
            s.camera_channel_count = expected_channels;
            const auto channelLabel = [&](int channel) {
                return camera_id + " " + std::to_string(channel) + "번 채널";
            };
            try {
                s.channel = item.at("channel").get<int>();
                s.rtsp_url = item.at("rtsp_url").get<std::string>();
                s.homography_file = item.at("homography_file").get<std::string>();
                s.image_width_px = item.at("image_width_px").get<int>();
                s.image_height_px = item.at("image_height_px").get<int>();
            } catch (const std::exception& error) {
                std::string itemLabel = camera_id + " 채널 항목";
                if (item.contains("channel") && item.at("channel").is_number_integer()) {
                    itemLabel = channelLabel(item.at("channel").get<int>());
                }
                LOG_WARN("CONFIG", itemLabel + " 제외 · JSON 파싱 실패 · " + error.what());
                continue;
            }

            std::string invalidReason;
            if (s.channel < 1 || s.channel > expected_channels) {
                invalidReason = std::to_string(expected_channels) + "채널 모델 지원 범위 초과";
            } else if (s.rtsp_url.rfind("rtsp://", 0) != 0) {
                invalidReason = "RTSP 주소 형식 오류";
            } else if (s.homography_file.empty()) {
                invalidReason = "좌표변환 파일 경로가 비어 있음";
            } else if (s.image_width_px < 1 || s.image_height_px < 1) {
                invalidReason = "영상 해상도 설정 오류";
            } else if (channels[{camera_id, s.channel}]) {
                invalidReason = "동일 카메라 내 채널 번호 중복";
            }
            if (!invalidReason.empty()) {
                LOG_WARN("CONFIG", channelLabel(s.channel) + " 제외 · " + invalidReason);
                continue;
            }
            channels[{camera_id,s.channel}] = true;
            s.stream_id = camera_id + "_CH_" + (s.channel < 10 ? "0" : "") + std::to_string(s.channel);
            if (stream_ids[s.stream_id]) {
                LOG_WARN("CONFIG", channelLabel(s.channel) + " 제외 · 스트림 ID 중복 · " + s.stream_id);
                continue;
            }
            stream_ids[s.stream_id] = true;
            if (!std::filesystem::exists(common_dir / s.homography_file)) {
                LOG_WARN("CONFIG", channelLabel(s.channel) + " 제외 · 좌표변환 파일 없음 · " + s.homography_file);
                continue;
            }
            const auto h_path = (common_dir / s.homography_file).lexically_normal();
            // H는 실행 중 매 프레임마다 읽지 않는다. 기동 시 단위·행렬·해상도를
            // 모두 확인하고 메모리에 올려, 잘못된 좌표가 위험 판정으로 흘러가지 않게 한다.
            try {
                std::ifstream h_input(h_path);
                json h; h_input >> h;
                if (h.at("map_unit").get<std::string>() != "mm")
                    schema(h_path.string(), "map_unit은 mm여야 함");
                if (h.contains("channel") && h.at("channel").get<int>() != s.channel)
                    schema(h_path.string(), "H 파일 내부 channel이 camera_list와 다름");
                const auto& size = h.at("image_size");
                if (size.at("width").get<int>() != s.image_width_px ||
                    size.at("height").get<int>() != s.image_height_px)
                    schema(h_path.string(), "H 해상도가 camera_list와 다름");
                const auto& matrix = h.at("H_camera_pixels_to_shared_map");
                if (!matrix.is_array() || matrix.size() != 3)
                    schema(h_path.string(), "H_camera_pixels_to_shared_map는 3x3이어야 함");
                for (const auto& row : matrix) {
                    if (!row.is_array() || row.size() != 3) schema(h_path.string(), "H_camera_pixels_to_shared_map는 3x3이어야 함");
                    for (const auto& cell : row) if (!cell.is_number() || !std::isfinite(cell.get<double>()))
                        schema(h_path.string(), "H_camera_pixels_to_shared_map에 유효하지 않은 수가 있음");
                }
            } catch (const SafetyServerConfigError& error) {
                LOG_WARN("CONFIG", channelLabel(s.channel) + " 제외 · 좌표변환 파일 규격 오류 · " + error.what());
                continue;
            } catch (const std::exception& e) {
                LOG_WARN("CONFIG", channelLabel(s.channel) + " 제외 · 좌표변환 파일 읽기 실패 · " + e.what());
                continue;
            }
            configured_channels.insert(s.channel);
            c.homography.stream_files[s.stream_id] = h_path.string();
            c.homography.stream_image_sizes[s.stream_id] = {s.image_width_px, s.image_height_px};
            c.streams.push_back(std::move(s));
        }
        if (configured_channels.size() != static_cast<std::size_t>(expected_channels)) {
            LOG_WARN("CONFIG", camera_id + " " + std::to_string(expected_channels) + "채널 모델 · " +
                                   std::to_string(configured_channels.size()) + "개 채널로 시작");
        }
    }
    // 일부 채널 제외는 허용하지만, CCTV 입력이 하나도 없는 상태를 정상 서버로
    // 광고해서는 안 된다. 이 경우 systemd가 실패를 감지하고 운영자가 확인하게 한다.
    if (c.streams.empty()) {
        schema(camera_path.string(), "유효한 호모그래피가 있는 활성 CCTV 스트림이 없음");
    }
    const auto& fl = devices.at("forklifts");
    if (!fl.is_array() || fl.empty()) schema(device_path.string(), "forklifts는 비어 있지 않은 배열이어야 함");
    std::map<std::string,bool> terminals; std::map<int,bool> markers;
    for (const auto& item : fl) {
        ForkliftDevice f{item.at("terminal_id").get<std::string>(), item.at("marker_id").get<int>(), item.at("collision_radius_mm").get<double>()};
        if (item.contains("marker_height_mm")) {
            if (!item.at("marker_height_mm").is_number())
                schema(device_path.string(), "marker_height_mm는 숫자여야 함");
            f.marker_height_mm = item.at("marker_height_mm").get<double>();
        }
        // 반경 0은 기존 중심점 거리 판정을 그대로 쓰겠다는 의미이므로 허용한다.
        // 마커 높이 0은 지면 호모그래피를 그대로 쓰겠다는 의미이므로 허용한다.
        if (f.terminal_id.empty() || terminals[f.terminal_id] || f.marker_id < 0 || markers[f.marker_id] ||
            !std::isfinite(f.collision_radius_mm) || f.collision_radius_mm < 0 ||
            !std::isfinite(f.marker_height_mm) || f.marker_height_mm < 0)
            schema(device_path.string(), "terminal/marker/radius/height 중복 또는 범위 오류");
        terminals[f.terminal_id] = true; markers[f.marker_id] = true; c.forklifts.push_back(std::move(f));
    }
    // MQTT, 핸드오버, 추적, 센서, 스트림 정책은 모든 TERM이 공유한다.
    const auto& n = object(system, "network", system_path.string());
    c.network.mqtt_host = value<std::string>(n,"network","mqtt_host",system_path.string());
    const int port = value<int>(n,"network","mqtt_port",system_path.string());
    c.network.mqtt_port = static_cast<uint16_t>(port);
    c.network.result_heartbeat_ms = value<int>(n,"network","result_heartbeat_ms",system_path.string());
    c.network.tls_enabled = n.value("tls_enabled", false);
    c.network.ca_cert_path = n.value("ca_cert_path", std::string{});
    c.network.client_cert_path = n.value("client_cert_path", std::string{});
    c.network.client_key_path = n.value("client_key_path", std::string{});
    const auto resolve_common_path = [&common_dir](std::string& value) {
        if (!value.empty() && !std::filesystem::path(value).is_absolute())
            value = (common_dir / value).lexically_normal().string();
    };
    resolve_common_path(c.network.ca_cert_path);
    resolve_common_path(c.network.client_cert_path);
    resolve_common_path(c.network.client_key_path);
    if (port < 1 || port > 65535 || c.network.mqtt_host.empty() || c.network.result_heartbeat_ms < 1)
        schema(system_path.string(), "MQTT 설정 범위 오류");
    if (c.network.tls_enabled && (c.network.ca_cert_path.empty() || c.network.client_cert_path.empty() || c.network.client_key_path.empty()))
        schema(system_path.string(), "TLS 사용 시 인증서 3종이 필요함");
    if (c.network.tls_enabled &&
        (!std::filesystem::exists(c.network.ca_cert_path) ||
         !std::filesystem::exists(c.network.client_cert_path) ||
         !std::filesystem::exists(c.network.client_key_path)))
        schema(system_path.string(), "TLS 인증서 파일을 찾을 수 없음");

    const auto& hand = object(system, "handover", system_path.string());
    c.handover.confirm_frames = value<int>(hand, "handover", "confirm_frames", system_path.string());
    c.handover.lost_grace_ms = value<int>(hand, "handover", "lost_grace_ms", system_path.string());
    const auto& tracking = object(system, "tracking", system_path.string());
    c.tracking.iou_threshold = value<double>(tracking, "tracking", "iou_threshold", system_path.string());
    c.tracking.world_distance_threshold_mm = value<double>(tracking, "tracking", "world_distance_threshold_mm", system_path.string());
    c.tracking.track_freshness_ms = value<int>(tracking, "tracking", "track_freshness_ms", system_path.string());
    c.tracking.track_timeout_ms = value<int>(tracking, "tracking", "track_timeout_ms", system_path.string());
    const auto& sensor = object(system, "sensor", system_path.string());
    c.sensor.stub_tof_distance_mm = value<double>(sensor, "sensor", "stub_tof_distance_mm", system_path.string());
    c.sensor.stale_timeout_ms = value<int>(sensor, "sensor", "stale_timeout_ms", system_path.string());
    const auto& stream = object(system, "stream", system_path.string());
    c.stream.rtsp_latency_ms = value<int>(stream, "stream", "rtsp_latency_ms", system_path.string());
    c.stream.appsink_max_buffers = value<int>(stream, "stream", "appsink_max_buffers", system_path.string());
    // 이전 운영 설정과의 호환성을 위해 새 큐 상한은 생략 시 안전한 기본값을 쓴다.
    c.stream.metadata_queue_capacity = stream.value("metadata_queue_capacity", 256);
    c.stream.eos_force_timeout_s = value<int>(stream, "stream", "eos_force_timeout_s", system_path.string());
    c.stream.connect_timeout_s = value<int>(stream, "stream", "connect_timeout_s", system_path.string());
    c.stream.max_retries = value<int>(stream, "stream", "max_retries", system_path.string());
    c.stream.retry_delay_s = value<int>(stream, "stream", "retry_delay_s", system_path.string());
    if (c.handover.confirm_frames < 1 || c.handover.lost_grace_ms < 0 ||
        !std::isfinite(c.tracking.iou_threshold) || c.tracking.iou_threshold < 0 || c.tracking.iou_threshold > 1 ||
        !std::isfinite(c.tracking.world_distance_threshold_mm) || c.tracking.world_distance_threshold_mm <= 0 ||
        c.tracking.track_freshness_ms < 1 || c.tracking.track_timeout_ms < 1 ||
        c.tracking.track_freshness_ms >= c.tracking.track_timeout_ms ||
        c.sensor.stale_timeout_ms < 1 || c.stream.rtsp_latency_ms < 0 ||
        c.stream.appsink_max_buffers < 1 || c.stream.metadata_queue_capacity < 1 ||
        c.stream.metadata_queue_capacity > 100000 || c.stream.eos_force_timeout_s < 1 || c.stream.connect_timeout_s < 1 ||
        c.stream.max_retries < 1 || c.stream.retry_delay_s < 0 || !std::isfinite(c.sensor.stub_tof_distance_mm) ||
        c.sensor.stub_tof_distance_mm < 0)
        schema(system_path.string(), "handover/tracking/sensor/stream 설정 범위 오류");
    const auto& out = object(system, "output_storage", system_path.string());
    std::filesystem::path storage_dir;
    if (const char* configured_data_dir = std::getenv("FORKLIFT_SAFETY_DATA_DIR");
        configured_data_dir && *configured_data_dir) {
        storage_dir = configured_data_dir;
    } else if (common_dir.string().rfind("/etc/forklift_safety", 0) == 0) {
        storage_dir = "/var/log/forklift_safety";
    } else {
        storage_dir = common_dir.parent_path() / "var" / "main_app";
    }
    if (out.contains("enable_object_csv_logging") && out.at("enable_object_csv_logging").is_boolean()) {
        c.output_storage.enable_object_csv_logging = out.at("enable_object_csv_logging").get<bool>();
    }
    if (out.contains("enable_aruco_csv_logging") && out.at("enable_aruco_csv_logging").is_boolean()) {
        c.output_storage.enable_aruco_csv_logging = out.at("enable_aruco_csv_logging").get<bool>();
    }
    if (out.contains("enable_latency_csv_logging") && out.at("enable_latency_csv_logging").is_boolean()) {
        c.output_storage.enable_latency_csv_logging = out.at("enable_latency_csv_logging").get<bool>();
    }
    if (out.contains("runtime_status") && !out.at("runtime_status").is_string())
        schema(system_path.string(), "output_storage.runtime_status는 문자열이어야 함");
    c.output_storage.object_csv = (storage_dir / value<std::string>(out,"output_storage","object_csv",system_path.string())).lexically_normal().string();
    c.output_storage.aruco_csv = (storage_dir / value<std::string>(out,"output_storage","aruco_csv",system_path.string())).lexically_normal().string();
    c.output_storage.event_db = (storage_dir / value<std::string>(out,"output_storage","event_db",system_path.string())).lexically_normal().string();
    c.output_storage.latency_csv = (storage_dir / value<std::string>(out,"output_storage","latency_csv",system_path.string())).lexically_normal().string();
    const std::string configured_runtime_status = out.value("runtime_status", std::string{});
    const std::filesystem::path runtime_status_path = configured_runtime_status.empty()
        ? (storage_dir / "runtime/runtime-status.json")
        : std::filesystem::path(configured_runtime_status);
    c.output_storage.runtime_status = (runtime_status_path.is_absolute()
        ? runtime_status_path
        : (storage_dir / runtime_status_path)).lexically_normal().string();
    for (const auto* p : {&c.output_storage.runtime_status,
                          &c.output_storage.object_csv,
                          &c.output_storage.aruco_csv,&c.output_storage.event_db,&c.output_storage.latency_csv})
        if (p->empty()) schema(system_path.string(), "출력 경로가 비어 있음");
    return c;
}

SafetyServerConfig loadMultiCameraServerConfig(const std::string& config_dir,
                                               const std::string& common_config_dir) {
    try {
        return loadMultiCameraServerConfigImpl(config_dir, common_config_dir);
    } catch (const SafetyServerConfigError&) {
        throw;
    } catch (const json::exception& error) {
        throw SafetyServerConfigError(SafetyServerConfigError::Code::SchemaInvalid,
                                      config_dir, std::string("설정 형식 오류: ") + error.what());
    } catch (const std::exception& error) {
        throw SafetyServerConfigError(SafetyServerConfigError::Code::SchemaInvalid,
                                      config_dir, std::string("설정 검증 실패: ") + error.what());
    }
}

}  // namespace forklift::config
