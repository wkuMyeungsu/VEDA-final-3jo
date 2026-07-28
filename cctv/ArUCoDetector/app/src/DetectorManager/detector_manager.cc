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
#include "i_p_stream_provider_manager_video_raw.h"  // raw 비디오 구독/이벤트
#include "i_pl_video_frame_raw.h"                   // IPLVideoFrameRaw
#include "i_p_video_frame_raw.h"                    // IPVideoFrameRaw / RawImage

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
  // RestartWorkers();   // [raw 테스트] HTTP 폴링 중지 — raw 비디오(eVideoRawData) 경로만 사용
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
    case static_cast<int32_t>(IPStreamProviderManagerVideoRaw::EEventType::eVideoRawData): {
      ProcessRawVideo(event);
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
    } else if (path_info == "/settings/apply") {
      auto method = oas->GetFCGXParam("REQUEST_METHOD");
      if (method == "POST") {
        RestartWorkers();   // 저장된 settings.json대로 워커 전부 재구성
        oas->SetResponseBody(std::string("{\"result\":\"ok\"}"));
      } else {
        oas->SetStatusCode(405);
        oas->SetResponseBody("method not allowed");
      }
    } else if (path_info == "/status") {
      HandleGetStatus(oas);
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
  auto* settings_apply_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/settings/apply"), GetInstanceName(), methods);
  auto* status_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/status"), GetInstanceName(), methods);

  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, write_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, check_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, detectonce_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, settings_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, settings_apply_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, status_uri);
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

// SPMgrVideoRaw가 밀어주는 raw 비디오 프레임(eVideoRawData)을 받아 ArUco를 검출한다.
// HTTP 스냅샷 폴링(ChannelWorker)과 달리 push 방식이라 별도 폴링 루프가 없다.
// 프레임 버퍼 수명관리 순서를 반드시 지켜야 한다(더블프리/OOM/use-after-free 방지).
void DetectorManager::ProcessRawVideo(Event* event) {
  if (event == nullptr || event->IsReply()) {
    return;
  }

  // 스로틀: N프레임마다 1회만 검출한다 (cv5 SoC 부하 방지).
  bool do_detect = (raw_frame_count_++ % raw_detect_every_ == 0);

  // 1) 이벤트에서 프레임 blob을 떼어낸다(detach). 안 하면 이벤트 소멸 시 이중 해제된다.
  auto blob = event->GetBlobArgument();
  event->ClearBaseObjectArgument();

  std::pair<std::variant<BaseObject*, char*>, uint64_t> ret((char*)blob.GetRawData(), blob.GetSize());

  IPVideoFrameRaw* raw_frame = new ("GetImage") IPLVideoFrameRaw(this, GetChannel());
  raw_frame->DeserializeBaseObject(raw_frame, ret);

  std::shared_ptr<RawImage> img(raw_frame->GetRawImage());
  if (img && do_detect) {
    // 2) YUV의 Y평면 = grayscale. cvtColor 없이 참조로 cv::Mat을 감싼다(stride=pitch).
    cv::Mat gray(static_cast<int>(img->height), static_cast<int>(img->width), CV_8UC1,
                 reinterpret_cast<void*>(img->plane[0].vir_addr), static_cast<size_t>(img->pitch));

    // 3) 검출은 반드시 blob.ClearResource() '이전'에 끝낸다.
    //    gray가 SDK 버퍼를 복사 없이 참조하므로, 해제 후 사용하면 use-after-free.
    DetectionResult r = raw_detector_.Detect(gray);
    std::cout << "[RawVideo] frame " << img->width << "x" << img->height
              << " pitch=" << img->pitch << " markers=" << r.ids.size() << std::endl;
    SendMetadata(GetChannel() + 1, r.ids, r.corners);  // 앱 채널표기는 1-based
  }

  blob.ClearResource();   // 4) 프레임 버퍼 해제 (안 하면 고FPS라 금방 OOM)
  delete raw_frame;       // 5) 프레임 객체 해제
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

  // 2) 설정 로드. settings.json이 없거나 채널이 비어 있으면(=갓 설치) 기본값(4채널 ON)으로
  //    시작하고 파일도 생성한다(best-effort). UI가 all-off 저장을 막으므로 빈 channels = 미설정.
  DetectionSettings settings = LoadDetectionSettings(kSettingsPath);
  if (settings.channels.empty()) {
    settings = DefaultDetectionSettings();
    SaveDetectionSettings(kSettingsPath, settings);
  }
  cv::aruco::PREDEFINED_DICTIONARY_NAME dict = StringToDict(settings.dictionary_name);
  CameraCredentials creds = LoadCameraCredentials();
  std::string calib_path_template = settings.calibration_path.empty()
                                        ? kDefaultCalibPath
                                        : settings.calibration_path;

  // 3) enabled 채널마다 워커 생성 + 시작
  workers_start_time_ = std::chrono::steady_clock::now();   // uptime 기준 시각
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

void DetectorManager::HandleGetStatus(OpenAppSerializable* oas) {
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  auto& alloc = doc.GetAllocator();

  bool running = !workers_.empty();
  doc.AddMember("running", running, alloc);

  uint64_t uptime_ms = 0;
  if (running) {
    uptime_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - workers_start_time_).count());
  }
  doc.AddMember("uptime_ms", uptime_ms, alloc);

  std::string last_sent;  // 채널들의 last_detect 중 최신 (ISO8601 문자열 비교로 최대값)

  JsonUtility::ValueType channels_arr(JsonUtility::Type::kArrayType);
  for (const auto& [ch, worker] : workers_) {
    ChannelWorker::Status st = worker->GetStatus();

    std::string state = !st.running ? "정지"
                        : (!st.last_error.empty() ? "오류" : "검출중");
    if (st.last_detect > last_sent) last_sent = st.last_detect;

    JsonUtility::ValueType obj(JsonUtility::Type::kObjectType);
    obj.AddMember("channel", ch, alloc);
    obj.AddMember("state", state, alloc);
    obj.AddMember("marker_count", st.marker_count, alloc);
    obj.AddMember("latency_ms", st.latency_ms, alloc);
    obj.AddMember("last_detect", st.last_detect, alloc);
    obj.AddMember("last_error", st.last_error, alloc);
    obj.AddMember("calibration", st.calibration, alloc);
    channels_arr.PushBack(obj, alloc);
  }
  doc.AddMember("channels", channels_arr, alloc);
  doc.AddMember("last_sent", last_sent, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  doc.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

extern "C" {
DetectorManager* create_component(void* mem_manager) {
  Component::allocator = decltype(Component::allocator)(mem_manager);
  Event::allocator = decltype(Event::allocator)(mem_manager);
  return new ("DetectorManager") DetectorManager();
}

void destroy_component(DetectorManager* ptr) { delete ptr; }
}