#pragma once

#include <string>
#include <vector>

struct ChannelConfig {
    int channel = 0;
    bool enabled = false;
    bool undistort = false;
};

struct DetectionSettings {
    std::string dictionary_name = "DICT_4X4_50";
    int poll_interval_ms = 1000;
    std::vector<ChannelConfig> channels;    // 채널별 검출/왜곡보정 설정
    std::string calibration_path;
};

// path는 settings.json 경로
DetectionSettings LoadDetectionSettings(const std::string& path);

// settings를 path에 JSON으로 저장. 성공 여부 반환.
bool SaveDetectionSettings(const std::string& path, const DetectionSettings& settings);

std::string SerializeDetectionSettings(const DetectionSettings& settings);

// JSON 문자열 → 구조체. json에 있는 필드만 덮어씀(없는 필드는 인자 settings 값 유지).
// "channels"가 있으면 기존 channels를 통째로 교체. 파싱 실패 시 false.
bool DeserializeDetectionSettings(const std::string& json, DetectionSettings& settings);