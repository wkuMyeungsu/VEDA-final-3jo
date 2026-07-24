#include "detection_settings.h"

#include <fstream>
#include <sstream>

#include "json_utility.h"

// settings.json 형태:
// {
//   "dictionary_name": "DICT_4X4_50",
//   "poll_interval_ms": 1000,
//   "channels": [{"channel":1,"enabled":true,"undistort":true}, ...]
//   "calibration_path_pattern": "/mnt/opensdk/apps/ArUCoCalibration/app/bin/calib_result_ch{channel}.json"
// }
// 파일이 없거나 필드가 없으면 DetectionSettings의 기본값을 그대로 씀.

DetectionSettings LoadDetectionSettings(const std::string& path) {
  DetectionSettings settings;

  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return settings;
  }

  std::stringstream ss;
  ss << ifs.rdbuf();

  DeserializeDetectionSettings(ss.str(), settings);
  return settings;
}

std::string SerializeDetectionSettings(const DetectionSettings& settings) {
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  auto& alloc = doc.GetAllocator();

  doc.AddMember("dictionary_name", settings.dictionary_name, alloc);
  doc.AddMember("poll_interval_ms", settings.poll_interval_ms, alloc);
  doc.AddMember("calibration_path_pattern", settings.calibration_path_pattern, alloc);
  
  JsonUtility::ValueType channels_arr(JsonUtility::Type::kArrayType);
  for (const auto& cc : settings.channels) {
    JsonUtility::ValueType obj(JsonUtility::Type::kObjectType);
    obj.AddMember("channel", cc.channel, alloc);
    obj.AddMember("enabled", cc.enabled, alloc);
    obj.AddMember("undistort", cc.undistort, alloc);
    channels_arr.PushBack(obj, alloc);
  }
  doc.AddMember("channels", channels_arr, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  doc.Accept(writer);
  return strbuf.GetString();  
}

bool SaveDetectionSettings(const std::string& path, const DetectionSettings& settings) {
  std::string json = SerializeDetectionSettings(settings);
  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    return false;
  }
  ofs << json;
  return true;
}

bool DeserializeDetectionSettings(const std::string& json, DetectionSettings& settings) {
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(json);
  if (doc.HasParseError()) {
    return false;
  }

  if (doc.HasMember("dictionary_name") && doc["dictionary_name"].IsString()) {
    settings.dictionary_name = doc["dictionary_name"].GetString();
  }
  if (doc.HasMember("poll_interval_ms") && doc["poll_interval_ms"].IsInt()) {
    settings.poll_interval_ms = doc["poll_interval_ms"].GetInt();
  }
  if (doc.HasMember("calibration_path_pattern") && doc["calibration_path_pattern"].IsString()) {
    settings.calibration_path_pattern = doc["calibration_path_pattern"].GetString();
  }
  if (doc.HasMember("channels") && doc["channels"].IsArray()) {
    settings.channels.clear();
    for (auto& v : doc["channels"].GetArray()) {
        if (!v.IsObject()) continue;
        ChannelConfig cc;
        if (v.HasMember("channel") && v["channel"].IsInt())       cc.channel    = v["channel"].GetInt();
        if (v.HasMember("enabled") && v["enabled"].IsBool())      cc.enabled    = v["enabled"].GetBool();
        if (v.HasMember("undistort") && v["undistort"].IsBool())  cc.undistort  = v["undistort"].GetBool();
        settings.channels.push_back(cc);
    }
  }

  return true;
}



