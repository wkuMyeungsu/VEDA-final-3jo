#include "detection_settings.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

#include "aruco_detector.h"
#include "json_utility.h"

namespace {

void AddError(std::vector<std::string>* errors, const std::string& message) {
  if (errors != nullptr) errors->push_back(message);
}

template <typename Allocator>
JsonUtility::ValueType StringValue(const std::string& value, Allocator& allocator) {
  JsonUtility::ValueType result;
  result.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
  return result;
}

bool IsFiniteScale(double value) {
  return std::isfinite(value) && (value == 1.0 || value == 0.5 || value == 0.25);
}

}  // namespace

bool ValidateDetectionSettings(const DetectionSettings& settings,
                               std::vector<std::string>* errors) {
  const size_t before = errors == nullptr ? 0 : errors->size();
  if (settings.schema_version != kDetectionSettingsSchemaVersion) {
    AddError(errors, "schema_version must be 4");
  }
  if (!IsSupportedArucoDictionary(settings.dictionary_name)) {
    AddError(errors, "dictionary_name is not supported: " + settings.dictionary_name);
  }
  if (settings.detection_worker_count < kMinDetectionWorkerCount ||
      settings.detection_worker_count > kMaxDetectionWorkerCount) {
    AddError(errors, "detection_worker_count must be 1 or 2");
  }
  if (settings.channels.empty()) AddError(errors, "channels must not be empty");
  std::set<int> channels;
  for (const auto& channel : settings.channels) {
    const std::string prefix = "channels[" + std::to_string(channel.channel) + "]";
    if (channel.channel < 1 || channel.channel > 4) AddError(errors, prefix + ".channel must be in 1..4");
    if (!channels.insert(channel.channel).second) AddError(errors, prefix + ".channel must be unique");
    if (!IsFiniteScale(channel.scale)) AddError(errors, prefix + ".scale must be one of 1.0, 0.5, 0.25");
  }
  return errors == nullptr || errors->size() == before;
}

namespace DetectionSettingsIO {

DetectionSettings Default() {
  DetectionSettings settings;
  for (int channel = 1; channel <= 4; ++channel) {
    ChannelConfig config;
    config.channel = channel;
    config.enabled = true;
    config.scale = 1.0;
    settings.channels.push_back(config);
  }
  return settings;
}

std::string Serialize(const DetectionSettings& settings) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& allocator = document.GetAllocator();
  document.AddMember("schema_version", settings.schema_version, allocator);
  document.AddMember("dictionary_name", StringValue(settings.dictionary_name, allocator), allocator);
  document.AddMember("detection_worker_count", settings.detection_worker_count, allocator);
  JsonUtility::ValueType channels(JsonUtility::Type::kArrayType);
  for (const auto& channel : settings.channels) {
    JsonUtility::ValueType object(JsonUtility::Type::kObjectType);
    object.AddMember("channel", channel.channel, allocator);
    object.AddMember("enabled", channel.enabled, allocator);
    object.AddMember("scale", channel.scale, allocator);
    channels.PushBack(object, allocator);
  }
  document.AddMember("channels", channels, allocator);
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document.Accept(writer);
  return std::string(buffer.GetString(), buffer.GetLength());
}

bool Deserialize(const std::string& json, DetectionSettings& settings,
                 std::vector<std::string>* errors) {
  std::vector<std::string> local_errors;
  std::vector<std::string>* output = errors == nullptr ? &local_errors : errors;
  const size_t initial_error_count = output->size();
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  document.Parse(json);
  if (document.HasParseError() || !document.IsObject()) {
    AddError(output, "request body must be a JSON object");
    return false;
  }

  DetectionSettings parsed = Default();
  int source_schema = 1;
  if (document.HasMember("schema_version")) {
    if (!document["schema_version"].IsInt()) AddError(output, "schema_version must be an integer");
    else source_schema = document["schema_version"].GetInt();
  }
  if (source_schema < 1 || source_schema > kDetectionSettingsSchemaVersion) {
    AddError(output, "schema_version is not supported");
  }
  parsed.schema_version = kDetectionSettingsSchemaVersion;
  if (document.HasMember("dictionary_name")) {
    if (!document["dictionary_name"].IsString()) AddError(output, "dictionary_name must be a string");
    else parsed.dictionary_name = document["dictionary_name"].GetString();
  }
  if (document.HasMember("detection_worker_count")) {
    if (!document["detection_worker_count"].IsInt()) AddError(output, "detection_worker_count must be an integer");
    else parsed.detection_worker_count = document["detection_worker_count"].GetInt();
  }
  if (document.HasMember("channels")) {
    if (!document["channels"].IsArray() || document["channels"].Empty()) {
      AddError(output, "channels must be a non-empty array");
    } else {
      parsed.channels.clear();
      for (auto it = document["channels"].Begin(); it != document["channels"].End(); ++it) {
        if (!it->IsObject()) {
          AddError(output, "channels[] must be an object");
          continue;
        }
        ChannelConfig channel;
        if (!it->HasMember("channel") || !(*it)["channel"].IsInt()) {
          AddError(output, "channels[].channel must be an integer");
        } else {
          channel.channel = (*it)["channel"].GetInt();
        }
        if (it->HasMember("enabled")) {
          if (!(*it)["enabled"].IsBool()) AddError(output, "channels[].enabled must be a boolean");
          else channel.enabled = (*it)["enabled"].GetBool();
        }
        if (it->HasMember("scale")) {
          if (!(*it)["scale"].IsNumber()) AddError(output, "channels[].scale must be a number");
          else channel.scale = (*it)["scale"].GetDouble();
        }
        parsed.channels.push_back(channel);
      }
    }
  }
  if (source_schema == 2) {
    for (auto& channel : parsed.channels) {
      if (channel.scale == 0.5) channel.scale = 1.0;
    }
  }

  const bool valid = ValidateDetectionSettings(parsed, output);
  settings = parsed;
  return valid && output->size() == initial_error_count;
}

DetectionSettings Load(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) return Default();
  std::stringstream contents;
  contents << input.rdbuf();
  DetectionSettings settings = Default();
  std::vector<std::string> errors;
  if (!Deserialize(contents.str(), settings, &errors)) return settings;
  return settings;
}

bool Save(const std::string& path, const DetectionSettings& settings) {
  std::vector<std::string> errors;
  if (!ValidateDetectionSettings(settings, &errors)) return false;
  const std::string temporary = path + ".tmp";
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) return false;
  const std::string json = Serialize(settings);
  output.write(json.data(), static_cast<std::streamsize>(json.size()));
  output.flush();
  if (!output.good()) {
    output.close();
    std::remove(temporary.c_str());
    return false;
  }
  output.close();
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

}  // namespace DetectionSettingsIO
