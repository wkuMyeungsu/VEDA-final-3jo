#include "test_run_controller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "json_utility.h"

#if defined(__linux__)
#include <time.h>
#endif

namespace {

constexpr int kMaxPhaseSamples = 10000;
constexpr int kCameraFreshFrameTimeoutMs = 1500;
// 상태 API에는 최근 샘플만 남기고, 전체 원본은 실행 중 CSV 임시 파일로 순차 기록한다.
constexpr size_t kMaxRetainedSamples = 4096;

using Clock = std::chrono::steady_clock;

int ElapsedUs(const Clock::time_point& begin, const Clock::time_point& end) {
  return static_cast<int>(std::max<int64_t>(0,
      std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count()));
}

long long ProcessCpuUs() {
#if defined(__linux__)
  struct timespec timespec_value;
  if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timespec_value) == 0) {
    return static_cast<long long>(timespec_value.tv_sec) * 1000000LL +
           static_cast<long long>(timespec_value.tv_nsec) / 1000LL;
  }
#endif
  return static_cast<long long>(std::clock()) * 1000000LL / CLOCKS_PER_SEC;
}

template <typename Allocator>
JsonUtility::ValueType JsonString(const std::string& value, Allocator& allocator) {
  JsonUtility::ValueType result;
  result.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
  return result;
}

template <typename Allocator>
JsonUtility::ValueType JsonIntArray(const std::vector<int>& values, Allocator& allocator) {
  JsonUtility::ValueType result(JsonUtility::Type::kArrayType);
  for (int value : values) result.PushBack(value, allocator);
  return result;
}

std::string SerializeDocument(JsonUtility::JsonDocument* document) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document->Accept(writer);
  return std::string(buffer.GetString(), buffer.GetLength());
}

std::string ErrorJson(const std::string& error) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& allocator = document.GetAllocator();
  document.AddMember("error", JsonString(error, allocator), allocator);
  return SerializeDocument(&document);
}

std::string ModeToString(TestFrameMode mode) {
  switch (mode) {
    case TestFrameMode::kWhite: return "white";
    case TestFrameMode::kBlack: return "black";
    case TestFrameMode::kMarker: return "marker";
    case TestFrameMode::kCamera: return "camera";
  }
  return "camera";
}

bool ParseMode(const std::string& value, TestFrameMode* mode) {
  if (value == "camera") *mode = TestFrameMode::kCamera;
  else if (value == "white") *mode = TestFrameMode::kWhite;
  else if (value == "black") *mode = TestFrameMode::kBlack;
  else if (value == "marker") *mode = TestFrameMode::kMarker;
  else return false;
  return true;
}

bool ParseScale(const rapidjson::Value& value, std::string* id, double* factor) {
  if (!value.IsString()) return false;
  *id = value.GetString();
  if (*id == "full") *factor = 1.0;
  else if (*id == "half") *factor = 0.5;
  else if (*id == "quarter") *factor = 0.25;
  else return false;
  return true;
}

bool ExactIdSetMatch(const std::vector<int>& expected, const std::vector<int>& detected) {
  std::set<int> expected_set(expected.begin(), expected.end());
  std::set<int> detected_set(detected.begin(), detected.end());
  return expected_set == detected_set;
}

struct TimingSeries {
  std::vector<int> values;
  void Add(int value) { values.push_back(std::max(0, value)); }
  long long Sum() const {
    long long sum = 0;
    for (int value : values) sum += value;
    return sum;
  }
};

template <typename Allocator>
JsonUtility::ValueType JsonTiming(const TimingSeries& series, Allocator& allocator) {
  JsonUtility::ValueType result(JsonUtility::Type::kObjectType);
  result.AddMember("count", static_cast<uint64_t>(series.values.size()), allocator);
  if (series.values.empty()) {
    result.AddMember("min", rapidjson::Value().SetNull(), allocator);
    result.AddMember("avg", rapidjson::Value().SetNull(), allocator);
    result.AddMember("p50", rapidjson::Value().SetNull(), allocator);
    result.AddMember("p95", rapidjson::Value().SetNull(), allocator);
    result.AddMember("max", rapidjson::Value().SetNull(), allocator);
    return result;
  }
  std::vector<int> sorted = series.values;
  std::sort(sorted.begin(), sorted.end());
  long long sum = 0;
  for (int value : sorted) sum += value;
  const size_t p50 = std::min(sorted.size() - 1, (sorted.size() * 50 + 99) / 100);
  const size_t p95 = std::min(sorted.size() - 1, (sorted.size() * 95 + 99) / 100);
  result.AddMember("min", sorted.front(), allocator);
  result.AddMember("avg", static_cast<double>(sum) / sorted.size(), allocator);
  result.AddMember("p50", sorted[p50], allocator);
  result.AddMember("p95", sorted[p95], allocator);
  result.AddMember("max", sorted.back(), allocator);
  return result;
}

struct Aggregate {
  int worker_count = 1;
  int configured_worker_count = 1;
  int effective_worker_count = 1;
  int allowed_cpu_count = 1;
  int opencv_thread_count = 1;
  int channel = 0;
  std::string scale_id;
  double scale_factor = 1.0;
  std::string dictionary_name;
  int requested = 0;
  int warmup = 0;
  int measurement = 0;
  int success = 0;
  int failure = 0;
  int unscored = 0;
  int skip = 0;
  int error = 0;
  TimingSeries input_copy;
  TimingSeries queue_wait;
  TimingSeries dispatch_scan;
  TimingSeries frame_get;
  TimingSeries worker_setup;
  TimingSeries resize;
  TimingSeries preprocess;
  TimingSeries detect;
  TimingSeries coordinate_restore;
  TimingSeries send;
  TimingSeries processing_total;
  TimingSeries end_to_end;
  TimingSeries thread_cpu;
  TimingSeries batch_cycle;
  TimingSeries process_cpu;
  int throughput_count = 0;
  double worker_busy_ratio_sum = 0.0;
  int worker_busy_ratio_count = 0;
};

struct SampleRecord {
  std::string run_id;
  uint64_t sample_seq = 0;
  uint64_t cycle_index = 0;
  int worker_count = 1;
  int configured_worker_count = 1;
  int effective_worker_count = 1;
  int allowed_cpu_count = 1;
  int opencv_thread_count = 1;
  int channel = 0;
  std::string scale_id;
  double scale_factor = 1.0;
  std::string dictionary_name;
  std::string phase;
  std::string input_mode;
  int width = 0;
  int height = 0;
  size_t step = 0;
  int processed_width = 0;
  int processed_height = 0;
  uint64_t frame_generation = 0;
  std::string coordinate_space = "raw";
  std::vector<int> expected_ids;
  std::vector<int> detected_ids;
  std::string outcome;
  std::string reason;
  int rejected_count = 0;
  bool executed = false;
  int input_copy_us = 0;
  int queue_wait_us = 0;
  int dispatch_scan_us = 0;
  int frame_get_us = 0;
  int worker_setup_us = 0;
  int resize_us = 0;
  int preprocess_us = 0;
  int detect_us = 0;
  int coordinate_restore_us = 0;
  int send_us = 0;
  int processing_total_us = 0;
  int end_to_end_us = 0;
  int thread_cpu_us = 0;
  int batch_cycle_us = 0;
  int process_cpu_us = 0;
  double worker_busy_ratio = 0.0;
};

std::string AggregateKey(int worker_count, const std::string& scale_id, int channel) {
  return std::to_string(worker_count) + "\x1f" + scale_id + "\x1f" + std::to_string(channel);
}

template <typename Allocator>
JsonUtility::ValueType JsonAggregate(const Aggregate& aggregate, bool has_ground_truth,
                                     Allocator& allocator) {
  JsonUtility::ValueType value(JsonUtility::Type::kObjectType);
  value.AddMember("worker_count", aggregate.worker_count, allocator);
  value.AddMember("configured_worker_count", aggregate.configured_worker_count, allocator);
  value.AddMember("effective_worker_count", aggregate.effective_worker_count, allocator);
  value.AddMember("allowed_cpu_count", aggregate.allowed_cpu_count, allocator);
  value.AddMember("opencv_thread_count", aggregate.opencv_thread_count, allocator);
  value.AddMember("channel", aggregate.channel, allocator);
  value.AddMember("scale_id", JsonString(aggregate.scale_id, allocator), allocator);
  value.AddMember("scale_factor", aggregate.scale_factor, allocator);
  value.AddMember("dictionary_name", JsonString(aggregate.dictionary_name, allocator), allocator);
  value.AddMember("requested", aggregate.requested, allocator);
  value.AddMember("warmup", aggregate.warmup, allocator);
  value.AddMember("measurement", aggregate.measurement, allocator);
  value.AddMember("success", aggregate.success, allocator);
  value.AddMember("failure", aggregate.failure, allocator);
  value.AddMember("unscored", aggregate.unscored, allocator);
  value.AddMember("skip", aggregate.skip, allocator);
  value.AddMember("error", aggregate.error, allocator);
  const int denominator = aggregate.success + aggregate.failure;
  if (has_ground_truth && denominator > 0) {
    value.AddMember("detection_rate", static_cast<double>(aggregate.success) / denominator, allocator);
  } else {
    value.AddMember("detection_rate", rapidjson::Value().SetNull(), allocator);
  }
  JsonUtility::ValueType timings(JsonUtility::Type::kObjectType);
  timings.AddMember("input_copy_us", JsonTiming(aggregate.input_copy, allocator), allocator);
  timings.AddMember("queue_wait_us", JsonTiming(aggregate.queue_wait, allocator), allocator);
  timings.AddMember("dispatch_scan_us", JsonTiming(aggregate.dispatch_scan, allocator), allocator);
  timings.AddMember("frame_get_us", JsonTiming(aggregate.frame_get, allocator), allocator);
  timings.AddMember("worker_setup_us", JsonTiming(aggregate.worker_setup, allocator), allocator);
  timings.AddMember("resize_us", JsonTiming(aggregate.resize, allocator), allocator);
  timings.AddMember("preprocess_us", JsonTiming(aggregate.preprocess, allocator), allocator);
  timings.AddMember("detect_us", JsonTiming(aggregate.detect, allocator), allocator);
  timings.AddMember("coordinate_restore_us", JsonTiming(aggregate.coordinate_restore, allocator), allocator);
  timings.AddMember("send_us", JsonTiming(aggregate.send, allocator), allocator);
  timings.AddMember("processing_total_us", JsonTiming(aggregate.processing_total, allocator), allocator);
  timings.AddMember("end_to_end_us", JsonTiming(aggregate.end_to_end, allocator), allocator);
  timings.AddMember("thread_cpu_us", JsonTiming(aggregate.thread_cpu, allocator), allocator);
  timings.AddMember("batch_cycle_us", JsonTiming(aggregate.batch_cycle, allocator), allocator);
  timings.AddMember("process_cpu_us", JsonTiming(aggregate.process_cpu, allocator), allocator);
  value.AddMember("timings", timings, allocator);
  value.AddMember("throughput_count", aggregate.throughput_count, allocator);
  const double cycle_seconds = static_cast<double>(aggregate.batch_cycle.Sum()) / 1000000.0;
  value.AddMember("throughput_per_second",
                  cycle_seconds > 0.0 ? static_cast<double>(aggregate.throughput_count) / cycle_seconds : 0.0,
                  allocator);
  value.AddMember("worker_busy_ratio",
                  aggregate.worker_busy_ratio_count > 0
                      ? aggregate.worker_busy_ratio_sum / aggregate.worker_busy_ratio_count
                      : 0.0,
                  allocator);
  return value;
}

std::string CsvEscape(const std::string& value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
  std::string escaped = "\"";
  for (char character : value) {
    if (character == '\"') escaped += "\"\"";
    else escaped += character;
  }
  escaped += '\"';
  return escaped;
}

template <typename T>
std::string CsvNumber(T value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << value;
  return stream.str();
}

std::string IdsJson(const std::vector<int>& ids) {
  std::ostringstream stream;
  stream << '[';
  for (size_t index = 0; index < ids.size(); ++index) {
    if (index != 0) stream << ',';
    stream << ids[index];
  }
  stream << ']';
  return stream.str();
}

void WriteCsvLine(std::ofstream* output, const std::vector<std::string>& values) {
  for (size_t index = 0; index < values.size(); ++index) {
    if (index != 0) *output << ',';
    *output << CsvEscape(values[index]);
  }
  *output << "\r\n";
}

const std::vector<std::string>& SampleCsvHeader() {
  static const std::vector<std::string> header = {
      "run_id", "sample_seq", "cycle_index", "worker_count", "configured_worker_count",
      "effective_worker_count", "allowed_cpu_count", "opencv_thread_count", "channel",
      "scale_id", "scale_factor", "dictionary_name", "phase", "input_mode",
      "width", "height", "step", "processed_width", "processed_height", "frame_generation",
      "coordinate_space", "expected_ids", "detected_ids", "outcome",
      "executed", "reason", "rejected_count", "input_copy_us", "queue_wait_us", "dispatch_scan_us",
      "frame_get_us", "worker_setup_us",
      "resize_us", "preprocess_us", "detect_us", "coordinate_restore_us", "send_us",
      "processing_total_us", "end_to_end_us", "thread_cpu_us", "batch_cycle_us", "process_cpu_us",
      "worker_busy_ratio"};
  return header;
}

std::vector<std::string> SampleCsvValues(const SampleRecord& sample) {
  return {sample.run_id, CsvNumber(sample.sample_seq), CsvNumber(sample.cycle_index),
          CsvNumber(sample.worker_count), CsvNumber(sample.configured_worker_count),
          CsvNumber(sample.effective_worker_count), CsvNumber(sample.allowed_cpu_count),
          CsvNumber(sample.opencv_thread_count), CsvNumber(sample.channel), sample.scale_id,
          CsvNumber(sample.scale_factor), sample.dictionary_name, sample.phase,
          sample.input_mode, CsvNumber(sample.width), CsvNumber(sample.height), CsvNumber(sample.step),
          CsvNumber(sample.processed_width), CsvNumber(sample.processed_height),
          CsvNumber(sample.frame_generation), sample.coordinate_space,
          IdsJson(sample.expected_ids), IdsJson(sample.detected_ids), sample.outcome,
          sample.executed ? "true" : "false", sample.reason, CsvNumber(sample.rejected_count),
          CsvNumber(sample.input_copy_us), CsvNumber(sample.queue_wait_us),
          CsvNumber(sample.dispatch_scan_us), CsvNumber(sample.frame_get_us),
          CsvNumber(sample.worker_setup_us),
          CsvNumber(sample.resize_us), CsvNumber(sample.preprocess_us), CsvNumber(sample.detect_us),
          CsvNumber(sample.coordinate_restore_us), CsvNumber(sample.send_us),
          CsvNumber(sample.processing_total_us), CsvNumber(sample.end_to_end_us),
          CsvNumber(sample.thread_cpu_us), CsvNumber(sample.batch_cycle_us),
          CsvNumber(sample.process_cpu_us), CsvNumber(sample.worker_busy_ratio)};
}

}  // namespace

struct TestRunController::RunConfig {
  struct Scale {
    std::string id;
    double factor = 1.0;
  };
  std::vector<Scale> scales;
  std::vector<int> channels;
  std::vector<int> worker_counts;
  TestFrameMode mode = TestFrameMode::kCamera;
  std::vector<int> expected_ids;
  int warmup_samples = 2;
  int measurement_samples = 10;
  bool has_ground_truth = false;
  TestFrameMode previous_mode = TestFrameMode::kCamera;
  DetectionSettings previous_settings;
  std::string dictionary_name;
  std::string run_id;
};

struct TestRunController::RunState {
  std::string status = "idle";
  std::string run_id;
  std::string error;
  std::string current_phase;
  int current_worker_count = 0;
  std::string current_scale_id;
  uint64_t current_cycle_index = 0;
  uint64_t current_sample_number = 0;
  RunConfig config;
  uint64_t completed_samples = 0;
  uint64_t total_samples = 0;
  std::map<std::string, Aggregate> aggregates;
  std::vector<SampleRecord> samples;
  std::string export_directory;
};

TestRunController::TestRunController(RawFrameStore* raw_store,
                                     DetectionDispatcher* dispatcher)
    : raw_store_(raw_store), dispatcher_(dispatcher), state_(new RunState()) {}

TestRunController::~TestRunController() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_requested_ = true;
  }
  if (thread_.joinable()) thread_.join();
}

bool TestRunController::ParseStartRequest(const std::string& request_body,
                                          const DetectionSettings& settings,
                                          const std::vector<int>& active_channels,
                                          RunConfig* config, std::string* error) const {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  document.Parse(request_body);
  if (document.HasParseError() || !document.IsObject()) {
    *error = "request body must be a JSON object";
    return false;
  }
  config->scales.clear();
  if (document.HasMember("scales")) {
    if (!document["scales"].IsArray() || document["scales"].Empty()) {
      *error = "scales must be a non-empty array";
      return false;
    }
    std::set<std::string> seen;
    for (auto it = document["scales"].Begin(); it != document["scales"].End(); ++it) {
      RunConfig::Scale scale;
      if (!ParseScale(*it, &scale.id, &scale.factor) || !seen.insert(scale.id).second) {
        *error = "scales must contain unique full, half, or quarter values";
        return false;
      }
      config->scales.push_back(scale);
    }
  } else {
    config->scales.push_back({"full", 1.0});
  }

  config->channels.clear();
  if (document.HasMember("channels")) {
    if (!document["channels"].IsArray() || document["channels"].Empty()) {
      *error = "channels must be a non-empty integer array";
      return false;
    }
    std::set<int> seen;
    for (auto it = document["channels"].Begin(); it != document["channels"].End(); ++it) {
      if (!it->IsInt() || it->GetInt() < 1 || it->GetInt() > 4 ||
          !seen.insert(it->GetInt()).second ||
          std::find(active_channels.begin(), active_channels.end(), it->GetInt()) == active_channels.end()) {
        *error = "channels must contain unique active channels in range 1..4";
        return false;
      }
      config->channels.push_back(it->GetInt());
    }
  } else {
    config->channels = active_channels;
  }
  if (config->channels.empty()) {
    *error = "no active channels are available";
    return false;
  }

  config->worker_counts.clear();
  if (document.HasMember("worker_counts")) {
    if (!document["worker_counts"].IsArray() || document["worker_counts"].Empty()) {
      *error = "worker_counts must be a non-empty array containing 1 and/or 2";
      return false;
    }
    std::set<int> seen;
    for (auto it = document["worker_counts"].Begin(); it != document["worker_counts"].End(); ++it) {
      if (!it->IsInt() || (it->GetInt() != 1 && it->GetInt() != 2) ||
          !seen.insert(it->GetInt()).second) {
        *error = "worker_counts must be [1], [2], or [1,2]";
        return false;
      }
      config->worker_counts.push_back(it->GetInt());
    }
    const bool valid_worker_list =
        config->worker_counts == std::vector<int>{1} ||
        config->worker_counts == std::vector<int>{2} ||
        config->worker_counts == std::vector<int>{1, 2};
    if (!valid_worker_list) {
      *error = "worker_counts must be [1], [2], or [1,2]";
      return false;
    }
  } else {
    // 별도 선택이 없으면 설정값이 아니라 현재 CPU 제한을 반영한 실제 worker 수를
    // 기본 후보로 사용해 degraded runtime을 테스트 화면에 그대로 드러낸다.
    config->worker_counts.push_back(
        CollectRuntimeInfo(settings.detection_worker_count).effective_worker_count);
  }

  config->mode = TestFrameMode::kCamera;
  if (document.HasMember("input_mode")) {
    if (!document["input_mode"].IsString() ||
        !ParseMode(document["input_mode"].GetString(), &config->mode)) {
      *error = "input_mode must be camera, white, black, or marker";
      return false;
    }
  }
  config->expected_ids.clear();
  if (document.HasMember("expected_ids")) {
    if (!document["expected_ids"].IsArray()) {
      *error = "expected_ids must be an integer array";
      return false;
    }
    std::set<int> seen;
    for (auto it = document["expected_ids"].Begin(); it != document["expected_ids"].End(); ++it) {
      if (!it->IsInt() || it->GetInt() < 0 || !seen.insert(it->GetInt()).second) {
        *error = "expected_ids must contain unique non-negative integers";
        return false;
      }
      config->expected_ids.push_back(it->GetInt());
    }
  } else if (config->mode == TestFrameMode::kMarker) {
    config->expected_ids.push_back(0);
  }

  config->warmup_samples = 2;
  config->measurement_samples = 10;
  if (document.HasMember("warmup_samples")) {
    if (!document["warmup_samples"].IsInt() || document["warmup_samples"].GetInt() < 0 ||
        document["warmup_samples"].GetInt() > kMaxPhaseSamples) {
      *error = "warmup_samples must be an integer from 0 to 10000";
      return false;
    }
    config->warmup_samples = document["warmup_samples"].GetInt();
  }
  if (document.HasMember("measurement_samples")) {
    if (!document["measurement_samples"].IsInt() || document["measurement_samples"].GetInt() < 1 ||
        document["measurement_samples"].GetInt() > kMaxPhaseSamples) {
      *error = "measurement_samples must be an integer from 1 to 10000";
      return false;
    }
    config->measurement_samples = document["measurement_samples"].GetInt();
  }
  config->dictionary_name = settings.dictionary_name;
  if (document.HasMember("dictionary_name")) {
    if (!document["dictionary_name"].IsString() ||
        !IsSupportedArucoDictionary(document["dictionary_name"].GetString())) {
      *error = "dictionary_name is not supported";
      return false;
    }
    config->dictionary_name = document["dictionary_name"].GetString();
  }
  config->has_ground_truth = config->mode != TestFrameMode::kCamera || !config->expected_ids.empty();
  return true;
}

bool TestRunController::Start(const std::string& request_body, int* status_code,
                              std::string* response) {
  if (status_code == nullptr || response == nullptr || raw_store_ == nullptr || dispatcher_ == nullptr) return false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_->status == "running") {
      *status_code = 409;
      *response = ErrorJson("test run already active");
      return false;
    }
  }
  if (thread_.joinable()) thread_.join();

  DetectionSettings settings = dispatcher_->GetSettings();
  std::vector<int> active_channels = dispatcher_->ActiveChannels();
  RunConfig config;
  std::string error;
  if (!ParseStartRequest(request_body, settings, active_channels, &config, &error)) {
    *status_code = 400;
    *response = ErrorJson(error);
    return false;
  }
  config.previous_mode = raw_store_->GetFrameMode();
  config.previous_settings = settings;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    config.run_id = "test-run-" + std::to_string(next_run_number_++);
    state_.reset(new RunState());
    state_->status = "running";
    state_->run_id = config.run_id;
    state_->config = config;
    state_->total_samples = static_cast<uint64_t>(config.worker_counts.size()) * config.scales.size() *
        config.channels.size() *
        static_cast<uint64_t>(config.warmup_samples + config.measurement_samples);
    cancel_requested_ = false;
  }
  try {
    dispatcher_->Stop();
    raw_store_->SetMarkerDictionary(StringToDict(config.dictionary_name));
    raw_store_->SetFrameMode(config.mode);
    thread_ = std::thread(&TestRunController::Run, this, config);
  } catch (const std::exception& exception) {
    raw_store_->SetFrameMode(config.previous_mode);
    dispatcher_->Start(settings);
    std::lock_guard<std::mutex> lock(mutex_);
    state_->status = "error";
    state_->error = exception.what();
    *status_code = 500;
    *response = ErrorJson(state_->error);
    return false;
  }
  *status_code = 200;
  *response = std::string("{\"run_id\":\"") + config.run_id + "\",\"status\":\"running\"}";
  return true;
}

bool TestRunController::IsCancelRequested() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cancel_requested_;
}

void TestRunController::OnDispatcherCompletion(const DispatcherCompletion& completion) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == nullptr || state_->status != "running") return;
  completions_.push_back(completion);
  completion_condition_.notify_all();
}

void TestRunController::Run(RunConfig config) {
  // 테스트 입력과 결과 집계만 여기서 제어하고, 검출 실행은 운영과 같은
  // RawFrameStore -> DetectionDispatcher -> worker -> Metadata 경로에 맡긴다.
  {
    bool cancelled = false;
    std::string run_error;
    const int total_cycles = config.warmup_samples + config.measurement_samples;

    auto add_sample = [this, &config](SampleRecord sample) {
      std::lock_guard<std::mutex> lock(mutex_);
      sample.sample_seq = state_->completed_samples;
      state_->samples.push_back(sample);
      ++state_->completed_samples;
      if (state_->samples.size() > kMaxRetainedSamples) state_->samples.erase(state_->samples.begin());
      const std::string key = AggregateKey(sample.worker_count, sample.scale_id, sample.channel);
      Aggregate& aggregate = state_->aggregates[key];
      aggregate.worker_count = sample.worker_count;
      aggregate.configured_worker_count = sample.configured_worker_count;
      aggregate.effective_worker_count = sample.effective_worker_count;
      aggregate.allowed_cpu_count = sample.allowed_cpu_count;
      aggregate.opencv_thread_count = sample.opencv_thread_count;
      aggregate.channel = sample.channel;
      aggregate.scale_id = sample.scale_id;
      aggregate.scale_factor = sample.scale_factor;
      aggregate.dictionary_name = sample.dictionary_name;
      ++aggregate.requested;
      if (sample.phase == "warmup") {
        ++aggregate.warmup;
        return;
      }
      ++aggregate.measurement;
      if (sample.outcome == "success") ++aggregate.success;
      else if (sample.outcome == "failure") ++aggregate.failure;
      else if (sample.outcome == "unscored") ++aggregate.unscored;
      else if (sample.outcome == "skip") ++aggregate.skip;
      else ++aggregate.error;
      aggregate.input_copy.Add(sample.input_copy_us);
      aggregate.queue_wait.Add(sample.queue_wait_us);
      aggregate.dispatch_scan.Add(sample.dispatch_scan_us);
      aggregate.frame_get.Add(sample.frame_get_us);
      aggregate.worker_setup.Add(sample.worker_setup_us);
      aggregate.batch_cycle.Add(sample.batch_cycle_us);
      aggregate.process_cpu.Add(sample.process_cpu_us);
      aggregate.worker_busy_ratio_sum += sample.worker_busy_ratio;
      ++aggregate.worker_busy_ratio_count;
      if (!sample.executed) return;
      aggregate.resize.Add(sample.resize_us);
      aggregate.preprocess.Add(sample.preprocess_us);
      aggregate.detect.Add(sample.detect_us);
      aggregate.coordinate_restore.Add(sample.coordinate_restore_us);
      aggregate.send.Add(sample.send_us);
      aggregate.processing_total.Add(sample.processing_total_us);
      aggregate.end_to_end.Add(sample.end_to_end_us);
      aggregate.thread_cpu.Add(sample.thread_cpu_us);
      ++aggregate.throughput_count;
    };

    try {
      dispatcher_->SetCompletionObserver(
          [this](const DispatcherCompletion& completion) { OnDispatcherCompletion(completion); });
      for (int worker_count : config.worker_counts) {
        for (const auto& scale : config.scales) {
            if (IsCancelRequested()) {
              cancelled = true;
              break;
            }
            DetectionSettings trial = config.previous_settings;
            trial.dictionary_name = config.dictionary_name;
            trial.detection_worker_count = worker_count;
            trial.channels.clear();
            for (int channel : config.channels) {
              ChannelConfig channel_config;
              channel_config.channel = channel;
              channel_config.enabled = true;
              channel_config.scale = scale.factor;
              trial.channels.push_back(channel_config);
            }
            dispatcher_->Start(trial);
            const DispatcherStatus runtime_status = dispatcher_->GetStatus();
            if (!runtime_status.running) {
              throw std::runtime_error("dispatcher did not start for test variant");
            }

            for (int cycle = 0; cycle < total_cycles; ++cycle) {
              if (IsCancelRequested()) {
                cancelled = true;
                break;
              }
              const std::string phase = cycle < config.warmup_samples ? "warmup" : "measurement";
              {
                std::lock_guard<std::mutex> lock(mutex_);
                state_->current_phase = phase;
                state_->current_worker_count = worker_count;
                state_->current_scale_id = scale.id;
                state_->current_cycle_index = static_cast<uint64_t>(cycle);
                state_->current_sample_number = state_->completed_samples + 1;
              }
              std::map<int, uint64_t> generation_before;
              for (int channel : config.channels) generation_before[channel] = raw_store_->GetGeneration(channel);
              {
                std::lock_guard<std::mutex> lock(mutex_);
                completions_.clear();
              }
              const Clock::time_point cycle_begin = Clock::now();
              const long long process_cpu_begin = ProcessCpuUs();
              for (int channel : config.channels) {
                raw_store_->RequestFrame(channel);
                if (config.mode != TestFrameMode::kCamera) {
                  const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(1000);
                  while (!raw_store_->PutSynthetic(channel)) {
                    if (IsCancelRequested()) break;
                    if (Clock::now() >= deadline) {
                      throw std::runtime_error("synthetic frame was not accepted by dispatcher");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    raw_store_->RequestFrame(channel);
                  }
                }
              }

              std::map<int, DispatcherCompletion> completed;
              {
                std::unique_lock<std::mutex> lock(mutex_);
                const bool ready = completion_condition_.wait_for(
                    lock, std::chrono::milliseconds(kCameraFreshFrameTimeoutMs), [&]() {
                      for (const DispatcherCompletion& completion : completions_) {
                        if (generation_before.count(completion.channel) &&
                            completion.snapshot.generation > generation_before[completion.channel]) {
                          completed[completion.channel] = completion;
                        }
                      }
                      return completed.size() == config.channels.size() || cancel_requested_;
                    });
                (void)ready;
                for (const DispatcherCompletion& completion : completions_) {
                  if (generation_before.count(completion.channel) &&
                      completion.snapshot.generation > generation_before[completion.channel]) {
                    completed[completion.channel] = completion;
                  }
                }
              }
              const int cycle_wall_us = ElapsedUs(cycle_begin, Clock::now());
              const long long process_cpu_delta = std::max<long long>(0, ProcessCpuUs() - process_cpu_begin);
              long long thread_cpu_sum = 0;
              for (const auto& item : completed) thread_cpu_sum += item.second.output.timing.thread_cpu_us;
              const double busy_ratio = cycle_wall_us > 0
                  ? static_cast<double>(thread_cpu_sum) /
                        (static_cast<double>(cycle_wall_us) * std::max(1, runtime_status.runtime.effective_worker_count))
                  : 0.0;

              for (int channel : config.channels) {
                SampleRecord sample;
                sample.run_id = config.run_id;
                sample.cycle_index = static_cast<uint64_t>(cycle);
                sample.worker_count = worker_count;
                sample.configured_worker_count = runtime_status.runtime.configured_worker_count;
                sample.effective_worker_count = runtime_status.runtime.effective_worker_count;
                sample.allowed_cpu_count = runtime_status.runtime.allowed_cpu_count;
                sample.opencv_thread_count = runtime_status.runtime.opencv_thread_count;
                sample.channel = channel;
                sample.scale_id = scale.id;
                sample.scale_factor = scale.factor;
                sample.dictionary_name = config.dictionary_name;
                sample.phase = phase;
                sample.input_mode = ModeToString(config.mode);
                sample.expected_ids = config.expected_ids;
                sample.batch_cycle_us = cycle_wall_us;
                sample.process_cpu_us = static_cast<int>(std::min<long long>(
                    process_cpu_delta, std::numeric_limits<int>::max()));
                sample.worker_busy_ratio = busy_ratio;
                const auto completion = completed.find(channel);
                if (completion == completed.end()) {
                  sample.outcome = "skip";
                  sample.reason = "timeout waiting for dispatcher completion";
                  add_sample(std::move(sample));
                  continue;
                }
                const DispatcherCompletion& value = completion->second;
                const PipelineOutput& output = value.output;
                sample.width = value.snapshot.width();
                sample.height = value.snapshot.height();
                sample.step = value.snapshot.step();
                sample.frame_generation = value.snapshot.generation;
                sample.detected_ids = output.result.ids;
                sample.rejected_count = output.result.rejected_count;
                sample.processed_width = output.processed_width;
                sample.processed_height = output.processed_height;
                sample.coordinate_space = output.coordinate_space;
                sample.input_copy_us = output.timing.input_copy_us;
                sample.queue_wait_us = output.timing.queue_wait_us;
                sample.dispatch_scan_us = output.timing.dispatch_scan_us;
                sample.frame_get_us = output.timing.frame_get_us;
                sample.worker_setup_us = output.timing.worker_setup_us;
                sample.resize_us = output.timing.resize_us;
                sample.preprocess_us = output.timing.preprocess_us;
                sample.detect_us = output.timing.detect_us;
                sample.coordinate_restore_us = output.timing.coordinate_restore_us;
                sample.send_us = output.timing.send_us;
                sample.processing_total_us = output.timing.processing_total_us;
                sample.end_to_end_us = output.timing.end_to_end_us;
                sample.thread_cpu_us = output.timing.thread_cpu_us;
                sample.executed = output.error.empty();
                if (!output.error.empty()) {
                  sample.outcome = "error";
                  sample.reason = output.error;
                } else if (!config.has_ground_truth) {
                  sample.outcome = "unscored";
                  sample.reason = "ground truth is not supplied for camera input";
                } else if (ExactIdSetMatch(config.expected_ids, sample.detected_ids)) {
                  sample.outcome = "success";
                } else {
                  sample.outcome = "failure";
                  sample.reason = "detected ids do not exactly match expected_ids";
                }
                add_sample(std::move(sample));
              }
            }
            dispatcher_->Stop();
            if (cancelled) break;
          }
        if (cancelled) break;
      }
    } catch (const std::exception& exception) {
      run_error = exception.what();
    } catch (...) {
      run_error = "unknown dispatcher test error";
    }

    dispatcher_->Stop();
    dispatcher_->SetCompletionObserver(DetectionDispatcher::CompletionObserver());
    try {
      raw_store_->SetFrameMode(config.previous_mode);
      dispatcher_->Start(config.previous_settings);
      std::filesystem::path directory = std::filesystem::path("storage") / "test_runs" / config.run_id;
      std::filesystem::create_directories(directory);
      std::lock_guard<std::mutex> lock(mutex_);
      state_->export_directory = directory.string();
      WriteExportsLocked(state_.get());
    } catch (const std::exception& exception) {
      if (run_error.empty()) run_error = std::string("test restoration/export failed: ") + exception.what();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    int executed_measurements = 0;
    int skipped_measurements = 0;
    int failed_measurements = 0;
    for (const auto& item : state_->aggregates) {
      const Aggregate& aggregate = item.second;
      executed_measurements += aggregate.throughput_count;
      skipped_measurements += aggregate.skip;
      failed_measurements += aggregate.error;
    }
    if (!run_error.empty()) {
      state_->status = "error";
      state_->error = run_error;
    } else if (cancelled || cancel_requested_) {
      state_->status = "cancelled";
    } else if (executed_measurements == 0) {
      state_->status = "error";
      state_->error = "no executed measurement (skip=" + std::to_string(skipped_measurements) +
          ", error=" + std::to_string(failed_measurements) + ")";
    } else {
      state_->status = "completed";
    }
    return;
  }
}


// CSV는 상태 JSON과 별개의 서버 산출물이다. 배열은 공백 없는 JSON 문자열로
// 기록하고 각 셀을 RFC 4180 규칙으로 escape한다.
void TestRunController::WriteExportsLocked(RunState* state_ptr) {
  if (state_ptr == nullptr) return;
  RunState& state = *state_ptr;
  const std::filesystem::path directory(state.export_directory);
  // 정상 실행은 Run()이 전체 샘플을 임시 CSV에 순차 기록한다. 예외로 스트림을
  // 열지 못한 경우에도 상태에 남은 제한 샘플로 다운로드 파일을 만들 수 있게 한다.
  const std::filesystem::path samples_path = directory / "samples.csv";
  if (!std::filesystem::exists(samples_path)) {
    std::ofstream samples(samples_path, std::ios::binary | std::ios::trunc);
    samples.write("\xEF\xBB\xBF", 3);
    WriteCsvLine(&samples, SampleCsvHeader());
    for (const SampleRecord& sample : state.samples) {
      WriteCsvLine(&samples, SampleCsvValues(sample));
    }
  }

}

std::string TestRunController::CancelJson() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_->status == "running") {
    cancel_requested_ = true;
    completion_condition_.notify_all();
  }
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& allocator = document.GetAllocator();
  document.AddMember("run_id", JsonString(state_->run_id, allocator), allocator);
  document.AddMember("status", JsonString(state_->status, allocator), allocator);
  document.AddMember("cancel_requested", state_->status == "running", allocator);
  return SerializeDocument(&document);
}

bool TestRunController::IsActive() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_->status == "running";
}

bool TestRunController::GetExportPath(const std::string& kind, std::string* path) const {
  if (path == nullptr || kind != "samples") return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_->export_directory.empty() || state_->status == "running") return false;
  *path = (std::filesystem::path(state_->export_directory) / (kind + ".csv")).string();
  return std::filesystem::exists(*path);
}

std::string TestRunController::StatusJson() const {
  RunState snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = *state_;
  }
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& allocator = document.GetAllocator();
  document.AddMember("status", JsonString(snapshot.status, allocator), allocator);
  document.AddMember("run_id", JsonString(snapshot.run_id, allocator), allocator);
  JsonUtility::ValueType progress(JsonUtility::Type::kObjectType);
  progress.AddMember("completed_samples", snapshot.completed_samples, allocator);
  progress.AddMember("total_samples", snapshot.total_samples, allocator);
  document.AddMember("progress", progress, allocator);
  JsonUtility::ValueType current(JsonUtility::Type::kObjectType);
  current.AddMember("phase", JsonString(snapshot.current_phase, allocator), allocator);
  current.AddMember("worker_count", snapshot.current_worker_count, allocator);
  current.AddMember("scale_id", JsonString(snapshot.current_scale_id, allocator), allocator);
  current.AddMember("cycle_index", snapshot.current_cycle_index, allocator);
  current.AddMember("sample_number", snapshot.current_sample_number, allocator);
  document.AddMember("current", current, allocator);
  JsonUtility::ValueType input(JsonUtility::Type::kObjectType);
  input.AddMember("input_mode", JsonString(ModeToString(snapshot.config.mode), allocator), allocator);
  input.AddMember("dictionary_name", JsonString(snapshot.config.dictionary_name, allocator), allocator);
  input.AddMember("expected_ids", JsonIntArray(snapshot.config.expected_ids, allocator), allocator);
  input.AddMember("warmup_samples", snapshot.config.warmup_samples, allocator);
  input.AddMember("measurement_samples", snapshot.config.measurement_samples, allocator);
  input.AddMember("has_ground_truth", snapshot.config.has_ground_truth, allocator);
  JsonUtility::ValueType workers(JsonUtility::Type::kArrayType);
  for (int count : snapshot.config.worker_counts) workers.PushBack(count, allocator);
  input.AddMember("worker_counts", workers, allocator);
  document.AddMember("input", input, allocator);
  document.AddMember("channels", JsonIntArray(snapshot.config.channels, allocator), allocator);
  JsonUtility::ValueType scales(JsonUtility::Type::kArrayType);
  for (const auto& scale : snapshot.config.scales) scales.PushBack(JsonString(scale.id, allocator), allocator);
  document.AddMember("scales", scales, allocator);
  JsonUtility::ValueType aggregates(JsonUtility::Type::kArrayType);
  for (const auto& item : snapshot.aggregates) {
    aggregates.PushBack(JsonAggregate(item.second, snapshot.config.has_ground_truth, allocator), allocator);
  }
  document.AddMember("aggregates", aggregates, allocator);
  JsonUtility::ValueType export_object(JsonUtility::Type::kObjectType);
  export_object.AddMember("samples_csv", JsonString("/test/run/export/samples", allocator), allocator);
  document.AddMember("export", export_object, allocator);
  if (snapshot.error.empty()) document.AddMember("error", rapidjson::Value().SetNull(), allocator);
  else document.AddMember("error", JsonString(snapshot.error, allocator), allocator);
  return SerializeDocument(&document);
}
