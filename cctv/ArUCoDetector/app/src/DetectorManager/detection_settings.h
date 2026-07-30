#pragma once

#include <string>
#include <vector>

struct DetectionSettings {
    std::string dictionary_name = "DICT_4X4_50";
    int poll_interval_ms = 1000;
    std::vector<int> channels;                      // 지금 검출 대상인 채널 목록 (안쓰는 채널은 여기서 빼면 됨.)
    std::string calibration_path_pattern;
};

// path는 settings.json 경로
DetectionSettings LoadDetectionSettings(const std::string& path);

// settings를 path에 JSON으로 저장. 성공 여부 반환.
bool SaveDetectionSettings(const std::string& path, const DetectionSettings& settings);