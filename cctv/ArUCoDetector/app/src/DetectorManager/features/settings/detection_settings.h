#pragma once

#include <string>
#include <vector>

// poll_interval_ms 허용 범위. raw 파이프라인에서 이 값은 "채널별로 최신 raw 프레임에
// 검출을 실행하는 주기"다 (프레임 획득 자체는 카메라가 계속 push하므로 이 값과 무관).
//   하한 500ms : 채널당 검출(4MP ArUco) 실측 소요시간(~300~440ms, 4채널 동시 실행 시 더 걸림)보다
//                짧게 잡아도 실제로 더 빨라지지 않고 CPU만 계속 태운다.
//   상한 3000ms: 사각지대 충돌방지 용도상 반응 지연 상한. 안전 요구사항에 따라 조정 필요.
constexpr int kMinPollIntervalMs = 500;
constexpr int kMaxPollIntervalMs = 3000;

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

// DetectionSettings를 settings.json으로 읽고 쓰는 담당.
namespace DetectionSettingsIO {
    // path는 settings.json 경로
    DetectionSettings Load(const std::string& path);

    // settings를 path에 JSON으로 저장. 성공 여부 반환.
    bool Save(const std::string& path, const DetectionSettings& settings);

    std::string Serialize(const DetectionSettings& settings);

    // JSON 문자열 → 구조체. json에 있는 필드만 덮어씀(없는 필드는 인자 settings 값 유지).
    // "channels"가 있으면 기존 channels를 통째로 교체. 파싱 실패 시 false.
    bool Deserialize(const std::string& json, DetectionSettings& settings);

    // 기본 설정 (4채널 ON, undistort off). settings.json이 없을 때 초기값으로 사용.
    DetectionSettings Default();
}