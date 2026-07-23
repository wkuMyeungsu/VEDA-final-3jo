#include "detector_manager.h"

#include <unistd.h>
#include <chrono>

#include "debug_detect_handler.h"  // 테스트 전용 (나중에 통째로 삭제)
#include "dispatcher_serialize.h"
#include "i_app_dispatcher.h"
#include "i_log_manager.h"
#include "i_metadata_manager.h"
#include "i_p_metadata_manager.h"
#include "i_p_open_platform_manager.h"

namespace {
auto eventToArgumentBuffer = [](Event* event) {
  auto blob = event->GetBlobArgument();
  std::pair<std::variant<BaseObject*, char*>, uint64_t> ret((char*)blob.GetRawData(),  // variant
                                                            blob.GetSize());           // size
  return ret;
};
}

DetectorManager::DetectorManager() : DetectorManager(_DetectorManager_Id, "DetectorManager") {}

DetectorManager::DetectorManager(ClassID id, const char* name) : Component(id, name) {}

DetectorManager::~DetectorManager() {}

bool DetectorManager::Initialize() {
  RegisterURI();
  return Component::Initialize();
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
          oas, [this](const std::vector<int>& ids, const std::vector<std::vector<cv::Point2f>>& corners) {
            SendMetadata(ids, corners);
          });
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

  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, write_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, check_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, detectonce_uri);
}

std::string DetectorManager::GetCurrentTimeToString() {
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);
  auto now_tm = ::gmtime(&now_time_t);

  std::stringstream ss;
  ss << std::put_time(now_tm, "%FT%T");
  return ss.str();
}

void DetectorManager::SendMetadata(const std::vector<int>& ids, const std::vector<std::vector<cv::Point2f>>& corners) {
  auto now_ms = static_cast<uint64_t> (
    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

  std::string xml = MarkerMetadataFormat::BuildMarkerMetadataXml(GetChannel(), ids, corners, now_ms);

  auto metadata = StringMetadata(GetChannel(), now_ms);
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

extern "C" {
DetectorManager* create_component(void* mem_manager) {
  Component::allocator = decltype(Component::allocator)(mem_manager);
  Event::allocator = decltype(Event::allocator)(mem_manager);
  return new ("DetectorManager") DetectorManager();
}

void destroy_component(DetectorManager* ptr) { delete ptr; }
}