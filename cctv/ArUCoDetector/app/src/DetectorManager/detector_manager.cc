#include "detector_manager.h"
#include "detection_settings.h"

#include <unistd.h>
#include <chrono>

#include "debug_detect_handler.h"  // 테스트 전용 (나중에 통째로 삭제)
#include "dispatcher_serialize.h"
#include "i_app_dispatcher.h"
#include "i_log_manager.h"
#include "i_metadata_manager.h"
#include "i_p_metadata_manager.h"
#include "i_p_open_platform_manager.h"

#include "aruco_detector.h"      // StringToDict
#include "camera_credentials.h"  // LoadCameraCredentials

namespace {
  constexpr const char* kSettingsPath = "settings.json"; // 실행 CWD(app/bin) 기준 경로. config.local.json과 동일 규약

  // calibration_path가 비어있을 때 쓸 기본 경로 ({channel} 은 채널 번호로 치환).
  constexpr const char* kDefaultCalibPath = "/mnt/opensdk/apps/ArUCoCalibration/app/bin/calib_result_ch{channel}.json";

  // path 안의 "{channel}" 을 실제 채널 번호로 치환.
  std::string ResolveCalibPath(const std::string& path, int channel) {
    std::string out = path;
    const std::string token = "{channel}";
    auto pos = out.find(token);
    if (pos != std::string::npos) {
      out.replace(pos, token.size(), std::to_string(channel));
    }
    return out;
  }

  auto eventToArgumentBuffer = [](Event* event) {
    auto blob = event->GetBlobArgument();
    std::pair<std::variant<BaseObject*, char*>, uint64_t> ret((char*)blob.GetRawData(),  // variant
                                                              blob.GetSize());           // size
    return ret;
  };
}

DetectorManager::DetectorManager() : DetectorManager(_DetectorManager_Id, "DetectorManager") {}

DetectorManager::DetectorManager(ClassID id, const char* name) : Component(id, name) {}

DetectorManager::~DetectorManager() {
  workers_.clear();   // 워커 스레드 모두 정지·join (this가 유효한 동안)
}

bool DetectorManager::Initialize() {
  RegisterURI();
  bool ok = Component::Initialize();
  RestartWorkers();   // 저장된 settings대로 채널 워커 기동
  return ok;
}

bool DetectorManager::ProcessAEvent(Event* event) {
  switch (event->GetType()) {
    case (int32_t)IAppDispatcher::EEventType::eHttpRequest: {
      HandleHttpRequest(event);
      break;
    }
    case static_cast<int32_t>(IPOpenPlatformManager::EAppEventType::eNetworkSettingChanged): {
      std::cout << "Network setting is changed!" << std::endl;
      setting_changed_time_ = GetCurrentTimeToString();
      break;
    }
    case static_cast<int32_t>(IPMetadataManager::EEventType::eMetadataRequest): {
      ProcessMetadata(event);
      break;
    }
    default:
      Component::ProcessAEvent(event);
      break;
  }
  return true;
}

bool DetectorManager::HandleHttpRequest(Event* event) {
  if (event->IsReply()) {
  } else {
    auto* oas = reinterpret_cast<OpenAppSerializable*>(event->GetBaseObjectArgument());
    auto path_info = oas->GetFCGXParam("PATH_INFO");

    if (path_info == "/writeeventlog") {
      auto body = oas->GetRequestBody();
      JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
      doc.Parse(body);

      if (doc.HasParseError()) {
        oas->SetStatusCode(400);
        oas->SetResponseBody("request body parse error");
        return false;
      }

      if (doc.HasMember("log")) {
        std::string log_msg = doc["log"].GetString();
        auto* log = new Log(Log::LogType::EVENT_LOG, Log::LogDetailType::EVENT_OPENAPP, 0, time(NULL), String(log_msg));
        SendNoReplyEvent("LogManager", static_cast<int>(ILogManager::EEvent::eWrite), 0, log);
      }
    } else if (path_info == "/checksetting") {
      JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
      auto& alloc = doc.GetAllocator();
      doc.AddMember("latest_changed", setting_changed_time_, alloc);

      rapidjson::StringBuffer strbuf;
      rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
      doc.Accept(writer);

      oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
    } else if (path_info == "/detectonce") {
      // 테스트 전용 (나중에 통째로 삭제). SendMetadata를 콜백으로 주입해서 debug 핸들러가 호출.
      DebugDetectHandler::HandleDetectOnce(
          oas, [this](int channel, const std::vector<int>& ids, const std::vector<std::vector<cv::Point2f>>& corners) {
            SendMetadata(channel, ids, corners);
          });
    } else if (path_info == "/settings") {
      auto method = oas->GetFCGXParam("REQUEST_METHOD");
      if(method == "GET") {
        HandleGetSettings(oas);
      } else if (method == "POST") {
        HandlePostSettings(oas);
      } else {
        oas->SetStatusCode(405);
        oas->SetResponseBody("method not allowed");
      }
    }
  }
  return true;
}

void DetectorManager::RegisterURI() {
  printf("[DetectorManager] Register URI\n");

  Vector<String> methods;
  methods.push_back("GET");
  methods.push_back("POST");

  auto* write_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/writeeventlog"), GetInstanceName(), methods);
  auto* check_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/checksetting"), GetInstanceName(), methods);
  auto* detectonce_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/detectonce"), GetInstanceName(), methods);
  auto* settings_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/settings"), GetInstanceName(), methods);

  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, write_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, check_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, detectonce_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, settings_uri);
}

std::string DetectorManager::GetCurrentTimeToString() {
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);
  auto now_tm = ::gmtime(&now_time_t);

  std::stringstream ss;
  ss << std::put_time(now_tm, "%FT%T");
  return ss.str();
}

void DetectorManager::SendMetadata(int channel, const std::vector<int>& ids, const std::vector<std::vector<cv::Point2f>>& corners) {
  auto now_ms = static_cast<uint64_t> (
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

  std::string xml = MarkerMetadataFormat::BuildMarkerMetadataXml(channel, ids, corners, now_ms);

  auto metadata = StringMetadata(channel, now_ms);
  metadata.Set(xml);

  auto* req = new ("MetadataRequest") IPMetadataManager::StringMetadataRequest();
  req->SetStringMetadata(std::move(metadata));

  SendNoReplyEvent("MetadataManager", static_cast<int32_t>(IMetadataManager::EEventType::eRequestRawMetadata), 0, req);
}

void DetectorManager::ProcessMetadata(Event* event) {
  if (event == nullptr || event->IsReply()) {
    return;
  }

  auto attachment = event->GetAttachment<IPMetadataManager::MetadataOutput>();
  if (attachment) {
    std::cout << "[DetectorManager][MetadataEcho] channel=" << attachment->channel() << " output=" << attachment->output() << std::endl;
  }
}

void DetectorManager::HandleGetSettings(OpenAppSerializable* oas) {
  DetectionSettings settings = LoadDetectionSettings(kSettingsPath);
  std::string json = SerializeDetectionSettings(settings);
  oas->SetResponseBody(json.c_str(), json.size());
}

void DetectorManager::HandlePostSettings(OpenAppSerializable* oas) {
  // 기존 설정을 먼저 로드 -> body에 없는 필드 (calibration_path 등)를 보존한다.
  DetectionSettings settings = LoadDetectionSettings(kSettingsPath);

  std::string body = oas->GetRequestBody();
  if (!DeserializeDetectionSettings(body, settings)) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("request body parse error");
    return;
  }

  if (!SaveDetectionSettings(kSettingsPath, settings)) {
    oas->SetStatusCode(500);
    oas->SetResponseBody("settings save failed");
    return;
  }

  oas->SetResponseBody(std::string("{\"result\":\"ok\"}"));
}

void DetectorManager::RestartWorkers() {
  // 1) 기존 워커 전부 제거 -> 각 unique_ptr 소멸 -> ~ChannelWorker -> Stop()(notify+join)
  workers_.clear();

  // 2) 설정 로드
  DetectionSettings settings = LoadDetectionSettings(kSettingsPath);
  cv::aruco::PREDEFINED_DICTIONARY_NAME dict = StringToDict(settings.dictionary_name);
  CameraCredentials creds = LoadCameraCredentials();
  std::string calib_path_template = settings.calibration_path.empty()
                                        ? kDefaultCalibPath
                                        : settings.calibration_path;

  // 3) enabled 채널마다 워커 생성 + 시작
  for (const auto& ch : settings.channels) {
    if (!ch.enabled) continue;

    std::string calib_path = ResolveCalibPath(calib_path_template, ch.channel);

    auto worker = std::make_unique<ChannelWorker>(
        ch.channel, creds, calib_path, dict, ch.undistort, settings.poll_interval_ms,
        [this](int c, const std::vector<int>& ids,
               const std::vector<std::vector<cv::Point2f>>& corners) {
          SendMetadata(c, ids, corners);   // 워커 스레드 -> 콜백 -> SendNoReplyEvent
        });
    worker->Start();
    workers_[ch.channel] = std::move(worker);
  }
}

extern "C" {
DetectorManager* create_component(void* mem_manager) {
  Component::allocator = decltype(Component::allocator)(mem_manager);
  Event::allocator = decltype(Event::allocator)(mem_manager);
  return new ("DetectorManager") DetectorManager();
}

void destroy_component(DetectorManager* ptr) { delete ptr; }
}