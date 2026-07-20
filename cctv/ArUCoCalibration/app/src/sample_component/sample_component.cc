#include "sample_component.h"

#include <curl/curl.h>

#include <sstream>

#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>

#include "camera_credentials.h"
#include "dispatcher_serialize.h"
#include "i_app_dispatcher.h"

namespace {
size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* response = reinterpret_cast<std::string*>(userdata);
  response->append(ptr, size * nmemb);
  return size * nmemb;
}

// 기본 파라미터는 인쇄물의 실내 조명/블러 조건에서 너무 엄격해서 마커를 놓치는 경우가 많아
// adaptiveThresh 범위를 넓히고, 서브픽셀 코너 보정을 켬 (캘리브레이션 정확도는 코너 정밀도에 직결됨).
cv::Ptr<cv::aruco::DetectorParameters> MakeDetectorParams() {
  auto params = cv::aruco::DetectorParameters::create();
  params->adaptiveThreshWinSizeMin = 3;
  params->adaptiveThreshWinSizeMax = 53;
  params->adaptiveThreshWinSizeStep = 4;
  params->minMarkerPerimeterRate = 0.02;
  params->maxMarkerPerimeterRate = 6.0;
  params->polygonalApproxAccuracyRate = 0.06;
  params->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
  return params;
}
}  // namespace

SampleComponent::SampleComponent() : SampleComponent(_SampleComponent_Id, "SampleComponent") {}

SampleComponent::SampleComponent(ClassID id, const char* name) : Component(id, name) {}

SampleComponent::~SampleComponent() {}

bool SampleComponent::Initialize() {
  RegisterURI();
  dictionary_ = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
  camera_credentials_ = LoadCameraCredentials();

  return Component::Initialize();
}

bool SampleComponent::ProcessAEvent(Event* event) {
  switch (event->GetType()) {
    case (int32_t)IAppDispatcher::EEventType::eHttpRequest: {
      HandleHttpRequest(event);
      break;
    }
    default:
      Component::ProcessAEvent(event);
      break;
  }
  return true;
}

bool SampleComponent::HandleHttpRequest(Event* event) {
  if (event->IsReply()) {
    return true;
  }

  auto* oas = reinterpret_cast<OpenAppSerializable*>(event->GetBaseObjectArgument());
  auto path_info = oas->GetFCGXParam("PATH_INFO");

  if (path_info == "/board") {
    HandleSetBoard(oas);
  } else if (path_info == "/detect") {
    HandleDetect(oas);
  } else if (path_info == "/capture") {
    HandleCapture(oas);
  } else if (path_info == "/status") {
    HandleStatus(oas);
  } else if (path_info == "/discard") {
    HandleDiscard(oas);
  } else if (path_info == "/reset") {
    HandleReset(oas);
  } else {
    oas->SetStatusCode(404);
    oas->SetResponseBody("unsupported path");
  }
  return true;
}

void SampleComponent::RegisterURI() {
  printf("[SampleComponent] Register URI\n");

  Vector<String> get_methods;
  get_methods.push_back("GET");

  Vector<String> post_methods;
  post_methods.push_back("POST");

  auto* board_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/board"), GetInstanceName(), post_methods);
  auto* detect_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/detect"), GetInstanceName(), get_methods);
  auto* capture_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/capture"), GetInstanceName(), post_methods);
  auto* status_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/status"), GetInstanceName(), get_methods);
  auto* discard_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/discard"), GetInstanceName(), post_methods);
  auto* reset_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/reset"), GetInstanceName(), post_methods);

  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, board_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, detect_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, capture_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, status_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, discard_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, reset_uri);
}

// ---- 보드 설정 ----

void SampleComponent::HandleSetBoard(OpenAppSerializable* oas) {
  auto body = oas->GetRequestBody();
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(body);

  if (doc.HasParseError() || !doc.HasMember("squares_x") || !doc.HasMember("squares_y") ||
      !doc.HasMember("square_length_mm") || !doc.HasMember("marker_length_mm")) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("squares_x, squares_y, square_length_mm, marker_length_mm 필요");
    return;
  }

  board_config_.squares_x = doc["squares_x"].GetInt();
  board_config_.squares_y = doc["squares_y"].GetInt();
  board_config_.square_length_m = doc["square_length_mm"].GetFloat() / 1000.f;
  board_config_.marker_length_m = doc["marker_length_mm"].GetFloat() / 1000.f;
  board_config_.configured = true;
  board_ = cv::Ptr<cv::aruco::CharucoBoard>();  // 다음 GetBoard() 호출에서 새 사양으로 재생성

  // 보드 사양이 바뀌면 이전에 모아둔 코너 데이터는 새 보드 좌표계와 안 맞으므로 전부 버림.
  for (auto& session : sessions_) {
    session = ChannelSession{};
  }

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("squares_x", board_config_.squares_x, alloc);
  res.AddMember("squares_y", board_config_.squares_y, alloc);
  res.AddMember("square_length_mm", (double)(board_config_.square_length_m * 1000.0), alloc);
  res.AddMember("marker_length_mm", (double)(board_config_.marker_length_m * 1000.0), alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

cv::Ptr<cv::aruco::CharucoBoard> SampleComponent::GetBoard() {
  if (!board_config_.configured) {
    return cv::Ptr<cv::aruco::CharucoBoard>();
  }
  if (!board_) {
    board_ = cv::aruco::CharucoBoard::create(board_config_.squares_x, board_config_.squares_y,
                                              board_config_.square_length_m, board_config_.marker_length_m,
                                              dictionary_);
  }
  return board_;
}

// ---- 검출 (저장 없이 확인만) ----

bool SampleComponent::RunDetection(int channel, DetectionOutcome& out) {
  static const cv::Ptr<cv::aruco::DetectorParameters> params = MakeDetectorParams();

  auto board = GetBoard();
  if (!board) {
    out.error = "먼저 /board 로 보드 사양을 설정해야 합니다";
    return false;
  }

  std::vector<unsigned char> jpeg;
  std::string fetch_error;
  if (!FetchSnapshot(channel, jpeg, fetch_error)) {
    out.error = "스냅샷 요청 실패: " + fetch_error;
    return false;
  }

  cv::Mat gray = cv::imdecode(jpeg, cv::IMREAD_GRAYSCALE);
  if (gray.empty()) {
    out.error = "JPEG 디코딩 실패";
    return false;
  }
  out.image_size = gray.size();

  cv::aruco::detectMarkers(gray, dictionary_, out.marker_corners, out.marker_ids, params);

  if (!out.marker_ids.empty()) {
    cv::aruco::interpolateCornersCharuco(out.marker_corners, out.marker_ids, gray, board, out.charuco_corners,
                                          out.charuco_ids);
  }

  out.ok = true;
  return true;
}

void SampleComponent::WriteDetectionJson(JsonUtility::JsonDocument& doc, const DetectionOutcome& outcome) {
  auto& alloc = doc.GetAllocator();

  doc.AddMember("image_width", outcome.image_size.width, alloc);
  doc.AddMember("image_height", outcome.image_size.height, alloc);
  doc.AddMember("marker_count", (int)outcome.marker_ids.size(), alloc);

  auto markers = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (size_t i = 0; i < outcome.marker_ids.size(); ++i) {
    auto marker = JsonUtility::ValueType(JsonUtility::Type::kObjectType);
    marker.AddMember("id", outcome.marker_ids[i], alloc);
    auto corners = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
    for (const auto& pt : outcome.marker_corners[i]) {
      auto corner = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
      corner.PushBack(pt.x, alloc);
      corner.PushBack(pt.y, alloc);
      corners.PushBack(corner, alloc);
    }
    marker.AddMember("corners", corners, alloc);
    markers.PushBack(marker, alloc);
  }
  doc.AddMember("markers", markers, alloc);

  int charuco_count = outcome.charuco_ids.empty() ? 0 : outcome.charuco_ids.rows;
  doc.AddMember("charuco_corner_count", charuco_count, alloc);

  auto charuco = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (int r = 0; r < charuco_count; ++r) {
    auto c = JsonUtility::ValueType(JsonUtility::Type::kObjectType);
    c.AddMember("id", outcome.charuco_ids.at<int>(r, 0), alloc);
    cv::Point2f pt = outcome.charuco_corners.at<cv::Point2f>(r, 0);
    c.AddMember("x", (double)pt.x, alloc);
    c.AddMember("y", (double)pt.y, alloc);
    charuco.PushBack(c, alloc);
  }
  doc.AddMember("charuco_corners", charuco, alloc);
}

void SampleComponent::HandleDetect(OpenAppSerializable* oas) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  int channel = std::atoi(GetQueryParam(query, "channel").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("channel 값은 1~4 이어야 함");
    return;
  }

  DetectionOutcome outcome;
  if (!RunDetection(channel, outcome)) {
    oas->SetStatusCode(400);
    oas->SetResponseBody(outcome.error);
    return;
  }

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("channel", channel, alloc);
  WriteDetectionJson(res, outcome);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

// ---- 캡처 (저장) ----

void SampleComponent::HandleCapture(OpenAppSerializable* oas) {
  auto body = oas->GetRequestBody();
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(body);
  if (doc.HasParseError() || !doc.HasMember("channel")) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("channel 파라미터 필요");
    return;
  }

  int channel = doc["channel"].GetInt();
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("channel 값은 1~4 이어야 함");
    return;
  }

  DetectionOutcome outcome;
  if (!RunDetection(channel, outcome)) {
    oas->SetStatusCode(400);
    oas->SetResponseBody(outcome.error);
    return;
  }

  auto& session = sessions_[idx];
  int charuco_count = outcome.charuco_ids.empty() ? 0 : outcome.charuco_ids.rows;
  bool accepted = charuco_count >= kMinCharucoCornersPerFrame;
  std::string reject_reason;

  if (!accepted) {
    reject_reason = "ChArUco 코너가 부족합니다 (검출 " + std::to_string(charuco_count) + "개, 최소 " +
                     std::to_string(kMinCharucoCornersPerFrame) + "개 필요). 보드를 화면에 더 크게/정면으로 놓고 다시 시도하세요.";
  } else if (!session.charuco_corners.empty() && session.image_size != outcome.image_size) {
    // 캡처 도중 해상도/줌이 바뀌면 캘리브레이션 전체가 깨지므로 거부.
    accepted = false;
    reject_reason = "이전 캡처와 해상도가 다릅니다 (해상도/줌 변경 금지). 필요하면 /reset 후 다시 시작하세요.";
  }

  if (accepted) {
    session.image_size = outcome.image_size;
    session.charuco_corners.push_back(outcome.charuco_corners.clone());
    session.charuco_ids.push_back(outcome.charuco_ids.clone());
    session.last_result = CalibrationResult{};  // 새 데이터가 들어왔으니 이전 계산 결과는 무효
  }

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("channel", channel, alloc);
  res.AddMember("accepted", accepted, alloc);
  if (!reject_reason.empty()) {
    res.AddMember("reject_reason", reject_reason, alloc);
  }
  res.AddMember("total_captured", (int)session.charuco_corners.size(), alloc);
  res.AddMember("min_recommended", kMinCapturesForCalibration, alloc);
  WriteDetectionJson(res, outcome);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

void SampleComponent::HandleStatus(OpenAppSerializable* oas) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  int channel = std::atoi(GetQueryParam(query, "channel").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("channel 값은 1~4 이어야 함");
    return;
  }

  auto& session = sessions_[idx];

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("channel", channel, alloc);
  res.AddMember("board_configured", board_config_.configured, alloc);
  res.AddMember("total_captured", (int)session.charuco_corners.size(), alloc);
  res.AddMember("min_recommended", kMinCapturesForCalibration, alloc);
  res.AddMember("has_result", session.last_result.valid, alloc);
  if (session.last_result.valid) {
    res.AddMember("rms", session.last_result.rms, alloc);
  }

  auto counts = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (const auto& ids : session.charuco_ids) {
    counts.PushBack(ids.rows, alloc);
  }
  res.AddMember("corners_per_capture", counts, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

void SampleComponent::HandleDiscard(OpenAppSerializable* oas) {
  auto body = oas->GetRequestBody();
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(body);
  if (doc.HasParseError() || !doc.HasMember("channel") || !doc.HasMember("index")) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("channel, index 파라미터 필요");
    return;
  }

  int channel = doc["channel"].GetInt();
  int index = doc["index"].GetInt();
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("channel 값은 1~4 이어야 함");
    return;
  }

  auto& session = sessions_[idx];
  if (index < 0 || index >= (int)session.charuco_corners.size()) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("잘못된 index");
    return;
  }

  session.charuco_corners.erase(session.charuco_corners.begin() + index);
  session.charuco_ids.erase(session.charuco_ids.begin() + index);
  session.last_result = CalibrationResult{};  // 데이터가 바뀌었으니 재계산 전까지 이전 결과는 무효

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("remaining", (int)session.charuco_corners.size(), alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

void SampleComponent::HandleReset(OpenAppSerializable* oas) {
  auto body = oas->GetRequestBody();
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(body);
  int channel = doc.HasMember("channel") ? doc["channel"].GetInt() : -1;
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("channel 값은 1~4 이어야 함");
    return;
  }

  sessions_[idx] = ChannelSession{};

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("channel", channel, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

// ---- 스냅샷 / 유틸 ----

bool SampleComponent::FetchSnapshot(int channel, std::vector<unsigned char>& out_jpeg, std::string& out_error) {
  if (camera_credentials_.admin_pass.empty()) {
    out_error = "카메라 인증정보가 없습니다 (config.local.json 확인)";
    return false;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    out_error = "curl_easy_init 실패";
    return false;
  }

  std::ostringstream url;
  url << "http://127.0.0.1/stw-cgi/video.cgi?msubmenu=snapshot&action=view&Channel=" << channel << "&Profile=1";

  std::string response;
  std::string userpwd = camera_credentials_.admin_user + ":" + camera_credentials_.admin_pass;

  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
  curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());

  CURLcode res = curl_easy_perform(curl);
  long status_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    out_error = std::string("curl 오류: ") + curl_easy_strerror(res);
    return false;
  }
  if (status_code != 200) {
    out_error = "HTTP 상태 코드: " + std::to_string(status_code);
    return false;
  }
  if (response.empty()) {
    out_error = "응답 바디가 비어있음";
    return false;
  }

  out_jpeg.assign(response.begin(), response.end());
  return true;
}

int SampleComponent::ChannelIndex(int channel_number) const {
  if (channel_number < 1 || channel_number > kChannelCount) {
    return -1;
  }
  return channel_number - 1;
}

std::string SampleComponent::GetQueryParam(const std::string& query, const std::string& key) {
  std::string prefix = key + "=";
  size_t pos = query.find(prefix);
  if (pos == std::string::npos) {
    return "";
  }
  size_t start = pos + prefix.size();
  size_t end = query.find('&', start);
  return query.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

extern "C" {
SampleComponent* create_component(void* mem_manager) {
  Component::allocator = decltype(Component::allocator)(mem_manager);
  Event::allocator = decltype(Event::allocator)(mem_manager);
  return new ("SampleComponent") SampleComponent();
}

void destroy_component(SampleComponent* ptr) { delete ptr; }
}
