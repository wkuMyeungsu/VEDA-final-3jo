#include "detector_manager.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <utility>

#include "i_app_dispatcher.h"
#include "i_metadata_manager.h"
#include "i_p_metadata_manager.h"
#include "i_p_stream_provider_manager_video_raw.h"
#include "i_pl_video_frame_raw.h"
#include "i_p_video_frame_raw.h"
#include "json_utility.h"

namespace {

constexpr const char* kSettingsPath = "settings.json";

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

std::string Serialize(JsonUtility::JsonDocument* document) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  document->Accept(writer);
  return std::string(buffer.GetString(), buffer.GetLength());
}

std::string ErrorsJson(const std::vector<std::string>& errors) {
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& allocator = document.GetAllocator();
  JsonUtility::ValueType array(JsonUtility::Type::kArrayType);
  for (const std::string& error : errors) array.PushBack(JsonString(error, allocator), allocator);
  document.AddMember("errors", array, allocator);
  return Serialize(&document);
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

struct ProcessStats {
  double cpu_percent = 0.0;
  double rss_mb = 0.0;
  double vsz_mb = 0.0;
  double mem_percent = 0.0;
  double total_ram_mb = 0.0;
  int threads = 1;
};

static unsigned long long g_last_proc_ticks = 0;
static std::chrono::steady_clock::time_point g_last_proc_time = std::chrono::steady_clock::now();
static std::mutex g_proc_stats_mtx;

ProcessStats GetProcessStats() {
  ProcessStats stats;
  std::lock_guard<std::mutex> lock(g_proc_stats_mtx);
#if defined(__linux__)
  // 1. 전체 시스템 RAM 크기 (/proc/meminfo)
  std::ifstream meminfo("/proc/meminfo");
  if (meminfo.is_open()) {
    std::string line;
    while (std::getline(meminfo, line)) {
      if (line.rfind("MemTotal:", 0) == 0) {
        unsigned long total_kb = 0;
        std::sscanf(line.c_str(), "MemTotal:\t%lu kB", &total_kb);
        stats.total_ram_mb = total_kb / 1024.0;
        break;
      }
    }
  }

  // 2. 프로세스 메모리 크기 (/proc/self/statm)
  std::ifstream statm("/proc/self/statm");
  if (statm.is_open()) {
    unsigned long vsz_pages = 0, rss_pages = 0;
    if (statm >> vsz_pages >> rss_pages) {
      const long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
      stats.rss_mb = (rss_pages * page_size_kb) / 1024.0;
      stats.vsz_mb = (vsz_pages * page_size_kb) / 1024.0;
      if (stats.total_ram_mb > 0) {
        stats.mem_percent = (stats.rss_mb / stats.total_ram_mb) * 100.0;
      }
    }
  }

  // 3. 프로세스 CPU 점유율 (/proc/self/stat)
  std::ifstream stat_file("/proc/self/stat");
  if (stat_file.is_open()) {
    std::string pid, comm, state;
    stat_file >> pid >> comm >> state;
    for (int i = 4; i <= 13; ++i) {
      std::string dummy;
      stat_file >> dummy;
    }
    unsigned long long utime = 0, stime = 0;
    stat_file >> utime >> stime;
    const unsigned long long current_ticks = utime + stime;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed_sec = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_proc_time).count() / 1000.0;
    if (g_last_proc_ticks > 0 && elapsed_sec > 0.1) {
      const double ticks_diff = static_cast<double>(current_ticks - g_last_proc_ticks);
      const double clk_tck = static_cast<double>(sysconf(_SC_CLK_TCK));
      const int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
      stats.cpu_percent = (ticks_diff / clk_tck) / elapsed_sec * 100.0 / (cpu_cores > 0 ? cpu_cores : 1);
      if (stats.cpu_percent < 0.0) stats.cpu_percent = 0.0;
      if (stats.cpu_percent > 100.0) stats.cpu_percent = 100.0;
    }
    g_last_proc_ticks = current_ticks;
    g_last_proc_time = now;
  }

  // 4. 스레드 수 (/proc/self/status)
  std::ifstream status_file("/proc/self/status");
  if (status_file.is_open()) {
    std::string line;
    while (std::getline(status_file, line)) {
      if (line.rfind("Threads:", 0) == 0) {
        std::sscanf(line.c_str(), "Threads:\t%d", &stats.threads);
        break;
      }
    }
  }
#endif
  return stats;
}

}  // namespace

DetectorManager::DetectorManager()
    : DetectorManager(_DetectorManager_Id, "DetectorManager") {}

DetectorManager::DetectorManager(ClassID id, const char* name)
    : Component(id, name),
      dispatcher_(&raw_store_, [this](int channel, const DetectionResult& result) {
        SendMetadata(channel, result);
      }) {
  std::cerr << "[DetectorManager][Startup] constructor complete" << std::endl;
}

DetectorManager::~DetectorManager() {
  test_run_controller_.reset();
  dispatcher_.Stop();
}

bool DetectorManager::Initialize() {
  std::cerr << "[DetectorManager][Startup] Initialize begin" << std::endl;
  // OpenCV 내부 스레드 중복 생성 방지 및 1:1 코어 전담 매핑
  cv::setNumThreads(1);
  cv::setUseOptimized(true);
  RegisterURI();
  std::cerr << "[DetectorManager][Startup] RegisterURI complete" << std::endl;
  const bool ok = Component::Initialize();
  std::cerr << "[DetectorManager][Startup] Component::Initialize returned " << ok
            << std::endl;
  if (!ok) {
    return false;
  }
  std::cerr << "[DetectorManager][Startup] RestartWorkers begin" << std::endl;
  RestartWorkers();
  std::cerr << "[DetectorManager][Startup] RestartWorkers complete" << std::endl;
  std::cerr << "[DetectorManager][Startup] TestRunController begin" << std::endl;
  test_run_controller_.reset(new TestRunController(&raw_store_, &dispatcher_));
  std::cerr << "[DetectorManager][Startup] TestRunController complete" << std::endl;
  return ok;
}

bool DetectorManager::ProcessAEvent(Event* event) {
  switch (event->GetType()) {
    case static_cast<int32_t>(IAppDispatcher::EEventType::eHttpRequest):
      HandleHttpRequest(event);
      break;
    case static_cast<int32_t>(IPMetadataManager::EEventType::eMetadataRequest):
      ProcessMetadata(event);
      break;
    case static_cast<int32_t>(IPStreamProviderManagerVideoRaw::EEventType::eVideoRawData):
      ProcessRawVideo(event);
      break;
    case static_cast<int32_t>(IPStreamProviderManagerVideoRaw::EEventType::eVideoConnect):
      AppendLog(GetCurrentTimeToString() + " [RawVideo] connected");
      break;
    case static_cast<int32_t>(IPStreamProviderManagerVideoRaw::EEventType::eVideoDisconnect):
      AppendLog(GetCurrentTimeToString() + " [RawVideo] disconnected");
      break;
    default:
      Component::ProcessAEvent(event);
      break;
  }
  return true;
}

bool DetectorManager::HandleHttpRequest(Event* event) {
  if (event == nullptr || event->IsReply()) return true;
  auto* serializable = reinterpret_cast<OpenAppSerializable*>(event->GetBaseObjectArgument());
  const std::string path = serializable->GetFCGXParam("PATH_INFO");
  const std::string method = serializable->GetFCGXParam("REQUEST_METHOD");

  if (path == "/settings") {
    if (method == "GET") HandleGetSettings(serializable);
    else if (method == "POST") {
      if (test_run_controller_ && test_run_controller_->IsActive()) {
        serializable->SetStatusCode(409);
        serializable->SetResponseBody("{\"error\":\"test run active\"}");
      } else HandlePostSettings(serializable);
    } else {
      serializable->SetStatusCode(405);
      serializable->SetResponseBody("method not allowed");
    }
  } else if (path == "/status") {
    if (method != "GET") {
      serializable->SetStatusCode(405);
      serializable->SetResponseBody("method not allowed");
    } else {
      HandleGetStatus(serializable);
    }
  } else if (path == "/logs") {
    if (method != "GET") {
      serializable->SetStatusCode(405);
      serializable->SetResponseBody("method not allowed");
    } else {
      HandleGetLogs(serializable);
    }
  } else if (path == "/test/run/start") {
    if (method != "POST") {
      serializable->SetStatusCode(405);
      serializable->SetResponseBody("method not allowed");
    } else if (!test_run_controller_) {
      serializable->SetStatusCode(503);
      serializable->SetResponseBody("{\"error\":\"test controller unavailable\"}");
    } else {
      int status_code = 200;
      std::string response;
      test_run_controller_->Start(serializable->GetRequestBody(), &status_code, &response);
      serializable->SetStatusCode(status_code);
      serializable->SetResponseBody(response.c_str(), response.size());
    }
  } else if (path == "/test/run/status") {
    if (method != "GET") {
      serializable->SetStatusCode(405);
      serializable->SetResponseBody("method not allowed");
    } else if (!test_run_controller_) {
      serializable->SetStatusCode(503);
      serializable->SetResponseBody("{\"error\":\"test controller unavailable\"}");
    } else {
      const std::string response = test_run_controller_->StatusJson();
      serializable->SetResponseBody(response.c_str(), response.size());
    }
  } else if (path == "/test/run/cancel") {
    if (method != "POST") {
      serializable->SetStatusCode(405);
      serializable->SetResponseBody("method not allowed");
    } else if (!test_run_controller_) {
      serializable->SetStatusCode(503);
      serializable->SetResponseBody("{\"error\":\"test controller unavailable\"}");
    } else {
      const std::string response = test_run_controller_->CancelJson();
      serializable->SetResponseBody(response.c_str(), response.size());
    }
  } else if (path == "/test/run/export/samples") {
    if (method != "GET") {
      serializable->SetStatusCode(405);
      serializable->SetResponseBody("method not allowed");
    } else {
      const std::string prefix = "/test/run/export/";
      HandleGetExport(serializable, path.substr(prefix.size()));
    }
  }
  return true;
}

void DetectorManager::RegisterURI() {
  Vector<String> methods;
  methods.push_back("GET");
  methods.push_back("POST");
  const char* paths[] = {"/settings", "/status", "/logs", "/test/run/start",
                         "/test/run/status", "/test/run/cancel", "/test/run/export/samples"};
  for (const char* path : paths) {
    auto* uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String(path), GetInstanceName(), methods);
    SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, uri);
  }
}

std::string DetectorManager::GetCurrentTimeToString() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now) + (9 * 3600);
  std::tm kst;
#if defined(_WIN32)
  gmtime_s(&kst, &time);
#else
  gmtime_r(&time, &kst);
#endif
  std::ostringstream stream;
  stream << std::put_time(&kst, "%Y-%m-%dT%H:%M:%S+09:00");
  return stream.str();
}

void DetectorManager::SendMetadata(int channel, const DetectionResult& result) {
  const uint64_t now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
  const std::string xml = MetadataXmlBuilder::BuildMarkerMetadataXml(
      channel, result.ids, result.corners, now_ms);
  auto metadata = StringMetadata(channel, now_ms);
  metadata.Set(xml);
  auto* request = new ("MetadataRequest") IPMetadataManager::StringMetadataRequest();
  request->SetStringMetadata(std::move(metadata));
  const std::string target = "MetadataManager_" + std::to_string(channel - 1);
  SendNoReplyEvent(target, static_cast<int32_t>(IMetadataManager::EEventType::eRequestRawMetadata), 0, request);
  AppendLog(GetCurrentTimeToString() + " [ch" + std::to_string(channel) + "] markers=" +
            std::to_string(result.ids.size()));
}

void DetectorManager::ProcessMetadata(Event* event) {
  if (event == nullptr || event->IsReply()) return;
  auto attachment = event->GetAttachment<IPMetadataManager::MetadataOutput>();
  if (attachment) {
    std::cout << "[DetectorManager][MetadataEcho] channel=" << attachment->channel()
              << " output=" << attachment->output() << std::endl;
  }
}

void DetectorManager::ProcessRawVideo(Event* event) {
  if (event == nullptr || event->IsReply()) return;
  auto blob = event->GetBlobArgument();
  event->ClearBaseObjectArgument();
  std::pair<std::variant<BaseObject*, char*>, uint64_t> raw_argument(
      static_cast<char*>(blob.GetRawData()), blob.GetSize());
  // Event source UId는 장비에서 raw component 이름과 다를 수 있다. RawImage의
  // chan_id로 PopRawImage 대상을 정해 source buffer를 정확히 반환한다.
  auto* raw_frame = new ("GetImage") IPLVideoFrameRaw();
  raw_frame->DeserializeBaseObject(raw_frame, raw_argument);
  std::shared_ptr<RawImage> image(raw_frame->GetRawImage());
  if (image && raw_store_.GetFrameMode() == TestFrameMode::kCamera &&
      dispatcher_.IsChannelActive(static_cast<int>(image->chan_id) + 1)) {
    if (image->plane[0].vir_addr == nullptr || image->width == 0 || image->height == 0 ||
        image->pitch < image->width) {
      AppendLog(GetCurrentTimeToString() + " [RawVideo] invalid luma plane");
    } else {
      const int channel = static_cast<int>(image->chan_id) + 1;
      cv::Mat gray(static_cast<int>(image->height), static_cast<int>(image->width), CV_8UC1,
                   reinterpret_cast<void*>(image->plane[0].vir_addr), static_cast<size_t>(image->pitch));
      raw_store_.Put(channel, gray);
    }
  }
  if (image) {
    const std::string target = "SPMgrVideoRaw_" + std::to_string(image->chan_id);
    auto* pop = new ("PopRawImage") PopEvent(image->chan_id, image->seq);
    SendNoReplyEvent(target, static_cast<int32_t>(IPVideoFrameRaw::EventType::kPopRawImage), 0, pop);
  }
  blob.ClearResource();
  delete raw_frame;
}

void DetectorManager::AppendLog(const std::string& line) {
  std::lock_guard<std::mutex> lock(logs_mtx_);
  recent_logs_.push_back(line);
  while (recent_logs_.size() > kMaxLogs) recent_logs_.pop_front();
}

void DetectorManager::HandleGetLogs(OpenAppSerializable* oas) {
  std::string body;
  {
    std::lock_guard<std::mutex> lock(logs_mtx_);
    for (const auto& line : recent_logs_) body += line + "\n";
  }
  oas->SetResponseBody(body.c_str(), body.size());
}

void DetectorManager::HandleGetSettings(OpenAppSerializable* oas) {
  const DetectionSettings settings = dispatcher_.GetSettings();
  const std::string json = DetectionSettingsIO::Serialize(settings);
  oas->SetResponseBody(json.c_str(), json.size());
}

void DetectorManager::HandlePostSettings(OpenAppSerializable* oas) {
  DetectionSettings settings;
  std::vector<std::string> errors;
  if (!DetectionSettingsIO::Deserialize(oas->GetRequestBody(), settings, &errors)) {
    oas->SetStatusCode(400);
    oas->SetResponseBody(ErrorsJson(errors));
    return;
  }
  if (!DetectionSettingsIO::Save(kSettingsPath, settings)) {
    oas->SetStatusCode(500);
    oas->SetResponseBody("{\"error\":\"settings save failed\"}");
    return;
  }
  RestartWorkers();
  oas->SetResponseBody("{\"result\":\"ok\",\"applied\":true}");
}

void DetectorManager::RestartWorkers() {
  dispatcher_.Stop();
  std::ifstream input(kSettingsPath, std::ios::binary);
  DetectionSettings settings;
  if (!input.is_open()) {
    settings = DetectionSettingsIO::Default();
    DetectionSettingsIO::Save(kSettingsPath, settings);
  } else {
    std::stringstream contents;
    contents << input.rdbuf();
    std::vector<std::string> errors;
    if (!DetectionSettingsIO::Deserialize(contents.str(), settings, &errors)) {
      AppendLog("settings rejected: " + (errors.empty() ? std::string("invalid settings") : errors.front()));
      // 잘못된 설정을 기본값으로 바꾸지 않고 dispatcher가 degraded 원인을 상태 API에
      // 노출하도록 파싱 결과를 그대로 전달한다.
      dispatcher_.Start(settings);
      const DispatcherStatus status = dispatcher_.GetStatus();
      if (status.runtime.degraded) {
        AppendLog("runtime degraded: " + status.runtime.degraded_reason);
      }
      return;
    }
  }
  dispatcher_.Start(settings);
  const DispatcherStatus status = dispatcher_.GetStatus();
  if (status.runtime.degraded) {
    AppendLog("runtime degraded: " + status.runtime.degraded_reason);
  }
}

void DetectorManager::HandleGetStatus(OpenAppSerializable* oas) {
  const DispatcherStatus status = dispatcher_.GetStatus();
  JsonUtility::JsonDocument document(JsonUtility::Type::kObjectType);
  auto& allocator = document.GetAllocator();
  document.AddMember("running", status.running, allocator);
  document.AddMember("uptime_ms", status.uptime_ms, allocator);
  document.AddMember("schema_version", kDetectionSettingsSchemaVersion, allocator);
  document.AddMember("configured_worker_count", status.runtime.configured_worker_count, allocator);
  document.AddMember("effective_worker_count", status.runtime.effective_worker_count, allocator);
  document.AddMember("active_workers", status.active_workers, allocator);
  document.AddMember("online_cpu_count", status.runtime.online_cpu_count, allocator);
  document.AddMember("allowed_cpu_count", status.runtime.allowed_cpu_count, allocator);
  document.AddMember("opencv_thread_count", status.runtime.opencv_thread_count, allocator);
  document.AddMember("degraded", status.runtime.degraded, allocator);
  document.AddMember("degraded_reason", JsonString(status.runtime.degraded_reason, allocator), allocator);
  document.AddMember("in_flight_channels", JsonIntArray(status.in_flight_channels, allocator), allocator);
  document.AddMember("frame_mode", JsonString(ModeToString(raw_store_.GetFrameMode()), allocator), allocator);

  const ProcessStats proc_stats = GetProcessStats();
  JsonUtility::ValueType proc_obj(JsonUtility::Type::kObjectType);
  proc_obj.AddMember("cpu_percent", proc_stats.cpu_percent, allocator);
  proc_obj.AddMember("rss_mb", proc_stats.rss_mb, allocator);
  proc_obj.AddMember("vsz_mb", proc_stats.vsz_mb, allocator);
  proc_obj.AddMember("mem_percent", proc_stats.mem_percent, allocator);
  proc_obj.AddMember("total_ram_mb", proc_stats.total_ram_mb, allocator);
  proc_obj.AddMember("threads", proc_stats.threads, allocator);
  document.AddMember("process", proc_obj, allocator);

  JsonUtility::ValueType channels(JsonUtility::Type::kArrayType);
  std::string last_sent;
  for (int channel_number = 1; channel_number <= 4; ++channel_number) {
    DispatcherChannelStatus inactive_channel;
    inactive_channel.channel = channel_number;
    inactive_channel.state = "disabled";
    auto channel_it = status.channels.find(channel_number);
    const DispatcherChannelStatus& channel = channel_it == status.channels.end()
        ? inactive_channel : channel_it->second;
    const RawFrameStore::ChannelStats raw = raw_store_.GetStats(channel.channel);
    JsonUtility::ValueType object(JsonUtility::Type::kObjectType);
    object.AddMember("channel", channel.channel, allocator);
    object.AddMember("running", channel.running, allocator);
    object.AddMember("state", JsonString(channel.state, allocator), allocator);
    object.AddMember("scale", channel.scale, allocator);
    object.AddMember("marker_count", channel.marker_count, allocator);
    object.AddMember("marker_ids", JsonIntArray(channel.marker_ids, allocator), allocator);
    object.AddMember("rejected_count", channel.rejected_count, allocator);
    object.AddMember("input_copy_us", channel.input_copy_us, allocator);
    object.AddMember("queue_wait_us", channel.queue_wait_us, allocator);
    object.AddMember("dispatch_scan_us", channel.dispatch_scan_us, allocator);
    object.AddMember("frame_get_us", channel.frame_get_us, allocator);
    object.AddMember("worker_setup_us", channel.worker_setup_us, allocator);
    object.AddMember("resize_us", channel.resize_us, allocator);
    object.AddMember("preprocess_us", channel.preprocess_us, allocator);
    object.AddMember("detect_us", channel.detect_us, allocator);
    object.AddMember("coordinate_restore_us", channel.coordinate_restore_us, allocator);
    object.AddMember("send_us", channel.send_us, allocator);
    object.AddMember("processing_total_us", channel.processing_total_us, allocator);
    object.AddMember("end_to_end_us", channel.end_to_end_us, allocator);
    object.AddMember("thread_cpu_us", channel.thread_cpu_us, allocator);
    object.AddMember("channel_cycle_us", channel.channel_cycle_us, allocator);
    object.AddMember("frame_generation", channel.frame_generation, allocator);
    object.AddMember("completed_count", channel.completed_count, allocator);
    object.AddMember("pending_snapshot", raw.pending, allocator);
    object.AddMember("in_flight_snapshot", raw.in_flight, allocator);
    object.AddMember("callback_count", raw.callback_count, allocator);
    object.AddMember("clone_count", raw.clone_count, allocator);
    object.AddMember("skipped_count", raw.skipped_count, allocator);
    object.AddMember("consumed_count", raw.consumed_count, allocator);
    object.AddMember("last_error", JsonString(channel.last_error, allocator), allocator);
    object.AddMember("last_detect", JsonString(channel.last_detect, allocator), allocator);
    channels.PushBack(object, allocator);
    if (channel.last_detect > last_sent) last_sent = channel.last_detect;
  }
  document.AddMember("channels", channels, allocator);
  document.AddMember("last_sent", JsonString(last_sent, allocator), allocator);
  const std::string json = ::Serialize(&document);
  oas->SetResponseBody(json.c_str(), json.size());
}

void DetectorManager::HandleGetExport(OpenAppSerializable* oas, const std::string& kind) {
  if (test_run_controller_ == nullptr) {
    oas->SetStatusCode(404);
    oas->SetResponseBody("{\"error\":\"no test run\"}");
    return;
  }
  std::string path;
  if (!test_run_controller_->GetExportPath(kind, &path)) {
    oas->SetStatusCode(test_run_controller_->IsActive() ? 409 : 404);
    oas->SetResponseBody("{\"error\":\"export is not available\"}");
    return;
  }
  std::ifstream input(path, std::ios::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  const std::string body = contents.str();
  oas->SetResponseBody(body.c_str(), body.size());
}

extern "C" {
DetectorManager* create_component(void* mem_manager) {
  std::cerr << "[DetectorManager][Startup] create_component begin" << std::endl;
  Component::allocator = decltype(Component::allocator)(mem_manager);
  Event::allocator = decltype(Event::allocator)(mem_manager);
  std::cerr << "[DetectorManager][Startup] allocators configured" << std::endl;
  DetectorManager* component = new ("DetectorManager") DetectorManager();
  std::cerr << "[DetectorManager][Startup] create_component complete" << std::endl;
  return component;
}

void destroy_component(DetectorManager* pointer) { delete pointer; }
}
