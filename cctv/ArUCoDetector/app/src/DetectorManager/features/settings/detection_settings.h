#pragma once

#include <string>
#include <vector>

// 운영 설정은 검출 주기를 저장하지 않는다. dispatcher가 snapshot 수신과 완료
// 순서를 기준으로 다음 채널을 고르므로 고정 sleep 값은 런타임 계약을 흐린다.
constexpr int kDetectionSettingsSchemaVersion = 4;
constexpr int kMinDetectionWorkerCount = 1;
constexpr int kMaxDetectionWorkerCount = 2;

struct ChannelConfig {
  int channel = 0;
  bool enabled = false;
  double scale = 1.0;
};

struct DetectionSettings {
  int schema_version = kDetectionSettingsSchemaVersion;
  std::string dictionary_name = "DICT_4X4_50";
  int detection_worker_count = 2;
  std::vector<ChannelConfig> channels;
};

// 설정 검증 실패는 호출자가 HTTP 400 필드 오류로 그대로 노출할 수 있도록
// 첫 번째 오류가 아닌 모든 오류를 수집한다.
bool ValidateDetectionSettings(const DetectionSettings& settings,
                               std::vector<std::string>* errors = nullptr);

namespace DetectionSettingsIO {
  DetectionSettings Load(const std::string& path);
  std::string Serialize(const DetectionSettings& settings);

  // 임시 파일에 먼저 쓰고 rename하여 전원 장애 중 반쪽 JSON이 남지 않게 한다.
  bool Save(const std::string& path, const DetectionSettings& settings);

  // schema v1~3의 dictionary/channels/enabled를 보존하고, schema 2의 0.5x
  // 운영 기본값은 원본 해상도로 한 번 승격한다. 기존 profile/profile_id는 무시한다.
  bool Deserialize(const std::string& json, DetectionSettings& settings,
                  std::vector<std::string>* errors = nullptr);

  DetectionSettings Default();
}
