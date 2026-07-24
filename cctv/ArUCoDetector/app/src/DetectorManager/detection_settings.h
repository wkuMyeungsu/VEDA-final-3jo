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
    std::string calibration_path_pattern;
};

// path는 settings.json 경로
DetectionSettings LoadDetectionSettings(const std::string& path);

// settings를 path에 JSON으로 저장. 성공 여부 반환.
bool SaveDetectionSettings(const std::string& path, const DetectionSettings& settings);