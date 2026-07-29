#include "camera_credentials.h"

#include <fstream>
#include <sstream>

#include "dispatcher_serialize.h"

namespace {
// calib_result_ch*.json과 동일하게 실행 CWD 기준 상대 경로 (config.example.json 참고).
constexpr const char* kConfigPath = "config.local.json";
}  // namespace

CameraCredentials LoadCameraCredentials() {
  CameraCredentials creds;

  std::ifstream ifs(kConfigPath);
  if (!ifs.is_open()) {
    return creds;
  }

  std::stringstream ss;
  ss << ifs.rdbuf();

  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(ss.str());
  if (doc.HasParseError()) {
    return creds;
  }

  if (doc.HasMember("admin_user")) {
    creds.admin_user = doc["admin_user"].GetString();
  }
  if (doc.HasMember("admin_pass")) {
    creds.admin_pass = doc["admin_pass"].GetString();
  }
  return creds;
}
