#include "sample_component.h"

#include <curl/curl.h>

#include <fstream>
#include <map>
#include <sstream>

#include <opencv2/aruco.hpp>
#include <opencv2/aruco/charuco.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "camera_credentials.h"
#include "dispatcher_serialize.h"
#include "i_app_dispatcher.h"

namespace {
size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* response = reinterpret_cast<std::string*>(userdata);
  response->append(ptr, size * nmemb);
  return size * nmemb;
}

// /detect 응답에 미리보기 이미지를 바로 심어서(base64 data URI), 프론트가 /preview/image를
// 따로 또 호출하는 왕복을 없애기 위한 인코더.
std::string Base64Encode(const std::vector<uchar>& data) {
  static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);

  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(kTable[(n >> 18) & 0x3F]);
    out.push_back(kTable[(n >> 12) & 0x3F]);
    out.push_back(kTable[(n >> 6) & 0x3F]);
    out.push_back(kTable[n & 0x3F]);
  }
  size_t remain = data.size() - i;
  if (remain == 1) {
    uint32_t n = data[i] << 16;
    out.push_back(kTable[(n >> 18) & 0x3F]);
    out.push_back(kTable[(n >> 12) & 0x3F]);
    out.append("==");
  } else if (remain == 2) {
    uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
    out.push_back(kTable[(n >> 18) & 0x3F]);
    out.push_back(kTable[(n >> 12) & 0x3F]);
    out.push_back(kTable[(n >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

// 기본 파라미터는 인쇄물의 실내 조명/블러 조건에서 너무 엄격해서 마커를 놓치는 경우가 많아
// adaptiveThresh 범위를 넓히고, 서브픽셀 코너 보정을 켬 (캘리브레이션 정확도는 코너 정밀도에 직결됨).
cv::Ptr<cv::aruco::DetectorParameters> MakeDetectorParams() {
  auto params = cv::aruco::DetectorParameters::create();
  params->adaptiveThreshWinSizeMin = 3;
  params->adaptiveThreshWinSizeMax = 53;
  params->adaptiveThreshWinSizeStep = 8;  // 4->8: 반복 횟수 절반(13->7)으로 줄여서 임베디드 CPU 부담 완화
  params->minMarkerPerimeterRate = 0.01;   // 더 작은/흐릿한 후보도 걸러내지 않도록 완화 (기본 0.03)
  params->maxMarkerPerimeterRate = 6.0;
  params->polygonalApproxAccuracyRate = 0.05;  // 기본값(0.03)에서 살짝만 완화. 0.08은 너무 관대해서
                                                // 마커 아닌 후보까지 받아들일 위험이 있어 되돌림.
  params->cornerRefinementMethod = cv::aruco::CORNER_REFINE_SUBPIX;
  // errorCorrectionRate: 기본값(0.6)으로 원복. 0.8은 너무 관대해서 다른 마커를 8/9번으로
  // 오인식했을 가능성이 있음 (인식 실패가 아니라 오인식일 수 있어서).
  params->perspectiveRemovePixelPerCell = 8;  // 마커 내부 비트 샘플링 해상도 상향 (기본 4)
  return params;
}

template <typename Allocator>
JsonUtility::ValueType MatToJsonArray(const cv::Mat& mat, Allocator& alloc) {
  auto arr = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (int i = 0; i < (int)mat.total(); ++i) {
    arr.PushBack(mat.at<double>(i), alloc);
  }
  return arr;
}

template <typename Allocator>
JsonUtility::ValueType VectorToJsonArray(const std::vector<double>& values, Allocator& alloc) {
  auto arr = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (double v : values) {
    arr.PushBack(v, alloc);
  }
  return arr;
}
}  // namespace

SampleComponent::SampleComponent() : SampleComponent(_SampleComponent_Id, "SampleComponent") {}

SampleComponent::SampleComponent(ClassID id, const char* name) : Component(id, name) {}

SampleComponent::~SampleComponent() {}

bool SampleComponent::Initialize() {
  RegisterURI();
  dictionary_ = cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);
  camera_credentials_ = LoadCameraCredentials();

  // 이전에 계산해둔 채널별 결과가 파일로 남아있으면 재시작 후에도 이어서 쓸 수 있게 불러옴.
  for (int ch = 1; ch <= kChannelCount; ++ch) {
    LoadResultFromFile(ch);
  }

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
    if (oas->GetFCGXParam("REQUEST_METHOD") == "GET") {
      HandleGetBoard(oas);
    } else {
      HandleSetBoard(oas);
    }
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
  } else if (path_info == "/calibrate") {
    HandleCalibrate(oas);
  } else if (path_info == "/result") {
    HandleResult(oas);
  } else if (path_info == "/undistort") {
    HandleUndistort(oas);
  } else if (path_info == "/captures/image") {
    HandleCaptureImage(oas);
  } else if (path_info == "/preview/image") {
    HandlePreviewImage(oas);
  } else if (path_info == "/debug/detect_dict") {
    HandleDebugDetectDict(oas);
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

  Vector<String> board_methods;
  board_methods.push_back("GET");
  board_methods.push_back("POST");

  auto* board_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/board"), GetInstanceName(), board_methods);
  auto* detect_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/detect"), GetInstanceName(), get_methods);
  auto* capture_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/capture"), GetInstanceName(), post_methods);
  auto* status_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/status"), GetInstanceName(), get_methods);
  auto* discard_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/discard"), GetInstanceName(), post_methods);
  auto* reset_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/reset"), GetInstanceName(), post_methods);
  auto* calibrate_uri =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/calibrate"), GetInstanceName(), post_methods);
  auto* capture_image_uri =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/captures/image"), GetInstanceName(), get_methods);
  auto* preview_image_uri =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/preview/image"), GetInstanceName(), get_methods);
  auto* result_uri = new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/result"), GetInstanceName(), get_methods);
  auto* undistort_uri =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/undistort"), GetInstanceName(), get_methods);
  auto* debug_detect_dict_uri =
      new ("OpenAPI") IAppDispatcher::OpenAPIRegistrar(String("/debug/detect_dict"), GetInstanceName(), get_methods);

  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, board_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, detect_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, capture_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, status_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, discard_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, reset_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, calibrate_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0,
                    capture_image_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0,
                    preview_image_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0, result_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0,
                    undistort_uri);
  SendNoReplyEvent("AppDispatcher", static_cast<int32_t>(IAppDispatcher::EEventType::eRegisterCommand), 0,
                    debug_detect_dict_uri);
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

void SampleComponent::HandleGetBoard(OpenAppSerializable* oas) {
  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("configured", board_config_.configured, alloc);
  if (board_config_.configured) {
    res.AddMember("squares_x", board_config_.squares_x, alloc);
    res.AddMember("squares_y", board_config_.squares_y, alloc);
    res.AddMember("square_length_mm", (double)(board_config_.square_length_m * 1000.0), alloc);
    res.AddMember("marker_length_mm", (double)(board_config_.marker_length_m * 1000.0), alloc);
  }

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

  // rejectedCandidates: 사각형처럼 생겨서 후보로는 잡혔지만 비트 디코딩(ID 매칭)에 실패해서
  // 버려진 것들. 이 수가 많으면 "후보는 찾는데 못 읽음"(파라미터/블러 문제), 적으면
  // "애초에 사각형 후보로도 안 잡힘"(왜곡/해상도 문제) 쪽으로 원인을 좁힐 수 있음.
  std::vector<std::vector<cv::Point2f>> rejected_candidates;
  cv::aruco::detectMarkers(gray, dictionary_, out.marker_corners, out.marker_ids, params, rejected_candidates);
  out.rejected_count = (int)rejected_candidates.size();

  if (!out.marker_ids.empty()) {
    cv::aruco::interpolateCornersCharuco(out.marker_corners, out.marker_ids, gray, board, out.charuco_corners,
                                          out.charuco_ids);
  }

  // 히스토리/미리보기용 축소 썸네일에 검출 결과(마커 테두리+ID, ChArUco 코너)를 그려서
  // 실제로 뭘 어디서 인식했는지 눈으로 바로 확인할 수 있게 함.
  cv::Mat annotated;
  cv::cvtColor(gray, annotated, cv::COLOR_GRAY2BGR);
  if (!out.marker_ids.empty()) {
    cv::aruco::drawDetectedMarkers(annotated, out.marker_corners, out.marker_ids, cv::Scalar(0, 255, 0));
  }
  if (!out.charuco_ids.empty()) {
    cv::aruco::drawDetectedCornersCharuco(annotated, out.charuco_corners, out.charuco_ids, cv::Scalar(0, 0, 255));
  }

  cv::Mat thumb;
  double scale = annotated.cols > 320 ? 320.0 / annotated.cols : 1.0;
  cv::resize(annotated, thumb, cv::Size(), scale, scale, cv::INTER_AREA);
  cv::imencode(".jpg", thumb, out.thumbnail_jpeg);

  out.ok = true;
  return true;
}

void SampleComponent::WriteDetectionJson(JsonUtility::JsonDocument& doc, const DetectionOutcome& outcome) {
  auto& alloc = doc.GetAllocator();

  doc.AddMember("image_width", outcome.image_size.width, alloc);
  doc.AddMember("image_height", outcome.image_size.height, alloc);
  doc.AddMember("marker_count", (int)outcome.marker_ids.size(), alloc);
  doc.AddMember("rejected_count", outcome.rejected_count, alloc);
  doc.AddMember("opencv_version", std::string(CV_VERSION), alloc);

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
  int channel = std::atoi(GetQueryParam(query, "ch").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
    return;
  }

  DetectionOutcome outcome;
  if (!RunDetection(channel, outcome)) {
    oas->SetStatusCode(400);
    oas->SetResponseBody(outcome.error);
    return;
  }

  // 저장은 안 하지만, 방금 본 화면은 /preview/image로 바로 확인할 수 있게 캐시해둠.
  sessions_[idx].last_preview_jpeg = outcome.thumbnail_jpeg;

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("channel", channel, alloc);
  std::string preview_data_uri = "data:image/jpeg;base64," + Base64Encode(outcome.thumbnail_jpeg);
  res.AddMember("preview_data_uri", preview_data_uri, alloc);
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
  if (doc.HasParseError() || !doc.HasMember("ch")) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 파라미터 필요");
    return;
  }

  int channel = doc["ch"].GetInt();
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
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
    session.thumbnails.push_back(outcome.thumbnail_jpeg);
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
  int channel = std::atoi(GetQueryParam(query, "ch").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
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
  if (doc.HasParseError() || !doc.HasMember("ch") || !doc.HasMember("index")) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch, index 파라미터 필요");
    return;
  }

  int channel = doc["ch"].GetInt();
  int index = doc["index"].GetInt();
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
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
  session.thumbnails.erase(session.thumbnails.begin() + index);
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
  int channel = doc.HasMember("ch") ? doc["ch"].GetInt() : -1;
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
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

// ---- 캘리브레이션 ----

void SampleComponent::HandleCalibrate(OpenAppSerializable* oas) {
  auto body = oas->GetRequestBody();
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(body);
  if (doc.HasParseError() || !doc.HasMember("ch")) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 파라미터 필요");
    return;
  }

  int channel = doc["ch"].GetInt();
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
    return;
  }

  bool rational_model = doc.HasMember("rational_model") && doc["rational_model"].GetBool();

  auto board = GetBoard();
  if (!board) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("먼저 /board 로 보드 사양을 설정해야 합니다");
    return;
  }

  auto& session = sessions_[idx];
  if (session.charuco_corners.size() < 4) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("캘리브레이션에는 최소 4장 이상의 캡처가 필요합니다 (권장 " +
                          std::to_string(kMinCapturesForCalibration) + "장 이상, 다양한 각도/거리로)");
    return;
  }

  cv::Mat camera_matrix, dist_coeffs, std_dev_intrinsics, std_dev_extrinsics, per_view_errors_mat;
  std::vector<cv::Mat> rvecs, tvecs;
  int flags = rational_model ? cv::CALIB_RATIONAL_MODEL : 0;

  double rms = 0.0;
  try {
    // stdDeviations/perViewErrors까지 받는 확장 오버로드를 사용 — 캡처별 오차를 직접
    // 재투영해서 계산하지 않고 OpenCV가 계산한 값을 그대로 신뢰함.
    rms = cv::aruco::calibrateCameraCharuco(session.charuco_corners, session.charuco_ids, board, session.image_size,
                                             camera_matrix, dist_coeffs, rvecs, tvecs, std_dev_intrinsics,
                                             std_dev_extrinsics, per_view_errors_mat, flags);
  } catch (const cv::Exception& e) {
    oas->SetStatusCode(500);
    oas->SetResponseBody(std::string("캘리브레이션 실패: ") + e.what());
    return;
  }

  std::vector<double> per_view_errors;
  per_view_errors.reserve(per_view_errors_mat.rows);
  for (int i = 0; i < per_view_errors_mat.rows; ++i) {
    per_view_errors.push_back(per_view_errors_mat.at<double>(i, 0));
  }

  session.last_result.valid = true;
  session.last_result.rms = rms;
  session.last_result.image_size = session.image_size;
  session.last_result.camera_matrix = camera_matrix;
  session.last_result.dist_coeffs = dist_coeffs;
  session.last_result.per_view_errors_px = per_view_errors;

  SaveResultToFile(channel, session.last_result);

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("channel", channel, alloc);
  res.AddMember("rms", rms, alloc);
  res.AddMember("image_width", session.image_size.width, alloc);
  res.AddMember("image_height", session.image_size.height, alloc);
  res.AddMember("camera_matrix", MatToJsonArray(camera_matrix, alloc), alloc);
  res.AddMember("dist_coeffs", MatToJsonArray(dist_coeffs, alloc), alloc);
  res.AddMember("per_view_errors_px", VectorToJsonArray(per_view_errors, alloc), alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

void SampleComponent::HandleResult(OpenAppSerializable* oas) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  int channel = std::atoi(GetQueryParam(query, "ch").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
    return;
  }

  if (!sessions_[idx].last_result.valid) {
    LoadResultFromFile(channel);
  }

  auto& result = sessions_[idx].last_result;
  if (!result.valid) {
    oas->SetStatusCode(404);
    oas->SetResponseBody("이 채널은 아직 캘리브레이션 결과가 없습니다");
    return;
  }

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("channel", channel, alloc);
  res.AddMember("rms", result.rms, alloc);
  res.AddMember("image_width", result.image_size.width, alloc);
  res.AddMember("image_height", result.image_size.height, alloc);
  res.AddMember("camera_matrix", MatToJsonArray(result.camera_matrix, alloc), alloc);
  res.AddMember("dist_coeffs", MatToJsonArray(result.dist_coeffs, alloc), alloc);
  res.AddMember("per_view_errors_px", VectorToJsonArray(result.per_view_errors_px, alloc), alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  res.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}

void SampleComponent::HandleUndistort(OpenAppSerializable* oas) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  int channel = std::atoi(GetQueryParam(query, "ch").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
    return;
  }

  if (!sessions_[idx].last_result.valid) {
    LoadResultFromFile(channel);
  }
  auto& result = sessions_[idx].last_result;
  if (!result.valid) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("이 채널은 아직 캘리브레이션 결과가 없습니다. 먼저 /calibrate 필요");
    return;
  }

  std::vector<unsigned char> jpeg;
  std::string error;
  if (!FetchSnapshot(channel, jpeg, error)) {
    oas->SetStatusCode(502);
    oas->SetResponseBody("스냅샷 요청 실패: " + error);
    return;
  }

  cv::Mat img = cv::imdecode(jpeg, cv::IMREAD_COLOR);
  if (img.empty()) {
    oas->SetStatusCode(502);
    oas->SetResponseBody("JPEG 디코딩 실패");
    return;
  }

  cv::Mat undistorted;
  cv::undistort(img, undistorted, result.camera_matrix, result.dist_coeffs);

  std::vector<unsigned char> out_jpeg;
  cv::imencode(".jpg", undistorted, out_jpeg);

  std::string out_body(reinterpret_cast<const char*>(out_jpeg.data()), out_jpeg.size());
  oas->AddResponseHeader("Content-Type", "image/jpeg");
  oas->SetResponseBody(out_body, OpenAppResponseType::FILE);
}

std::string SampleComponent::ResultFilePath(int channel) const {
  return "calib_result_ch" + std::to_string(channel) + ".json";
}

void SampleComponent::SaveResultToFile(int channel, const CalibrationResult& result) {
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  auto& alloc = doc.GetAllocator();
  doc.AddMember("rms", result.rms, alloc);
  doc.AddMember("image_width", result.image_size.width, alloc);
  doc.AddMember("image_height", result.image_size.height, alloc);
  doc.AddMember("camera_matrix", MatToJsonArray(result.camera_matrix, alloc), alloc);
  doc.AddMember("dist_coeffs", MatToJsonArray(result.dist_coeffs, alloc), alloc);
  doc.AddMember("per_view_errors_px", VectorToJsonArray(result.per_view_errors_px, alloc), alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  doc.Accept(writer);

  std::ofstream ofs(ResultFilePath(channel));
  ofs << strbuf.GetString();
}

void SampleComponent::LoadResultFromFile(int channel) {
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    return;
  }

  std::ifstream ifs(ResultFilePath(channel));
  if (!ifs.is_open()) {
    return;
  }

  std::stringstream ss;
  ss << ifs.rdbuf();

  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  doc.Parse(ss.str());
  if (doc.HasParseError() || !doc.HasMember("camera_matrix") || !doc.HasMember("dist_coeffs")) {
    return;
  }

  CalibrationResult result;
  result.valid = true;
  result.rms = doc["rms"].GetDouble();
  result.image_size = cv::Size(doc["image_width"].GetInt(), doc["image_height"].GetInt());

  result.camera_matrix = cv::Mat(3, 3, CV_64F);
  int k = 0;
  for (auto& v : doc["camera_matrix"].GetArray()) {
    result.camera_matrix.at<double>(k / 3, k % 3) = v.GetDouble();
    ++k;
  }

  auto dc_array = doc["dist_coeffs"].GetArray();
  result.dist_coeffs = cv::Mat(1, (int)dc_array.Size(), CV_64F);
  int dc_i = 0;
  for (auto& v : dc_array) {
    result.dist_coeffs.at<double>(0, dc_i++) = v.GetDouble();
  }

  if (doc.HasMember("per_view_errors_px")) {
    for (auto& v : doc["per_view_errors_px"].GetArray()) {
      result.per_view_errors_px.push_back(v.GetDouble());
    }
  }

  sessions_[idx].last_result = result;
}

// ---- 캡처 히스토리 ----

void SampleComponent::HandleCaptureImage(OpenAppSerializable* oas) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  int channel = std::atoi(GetQueryParam(query, "ch").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
    return;
  }

  int index = std::atoi(GetQueryParam(query, "index").c_str());
  auto& session = sessions_[idx];
  if (index < 0 || index >= (int)session.thumbnails.size()) {
    oas->SetStatusCode(404);
    oas->SetResponseBody("잘못된 index");
    return;
  }

  const auto& jpeg = session.thumbnails[index];
  std::string out_body(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
  oas->AddResponseHeader("Content-Type", "image/jpeg");
  oas->SetResponseBody(out_body, OpenAppResponseType::FILE);
}

void SampleComponent::HandlePreviewImage(OpenAppSerializable* oas) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  int channel = std::atoi(GetQueryParam(query, "ch").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
    return;
  }

  const auto& jpeg = sessions_[idx].last_preview_jpeg;
  if (jpeg.empty()) {
    oas->SetStatusCode(404);
    oas->SetResponseBody("아직 /detect를 호출한 적이 없습니다");
    return;
  }

  std::string out_body(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
  oas->AddResponseHeader("Content-Type", "image/jpeg");
  oas->SetResponseBody(out_body, OpenAppResponseType::FILE);
}

// 디버그 전용: 딕셔너리 불일치 의심을 재빌드 없이 확인하기 위한 임시 엔드포인트.
// ?ch=1&dict=DICT_5X5_100 형식. dict 생략 시 DICT_4X4_50(앱 기본값).
void SampleComponent::HandleDebugDetectDict(OpenAppSerializable* oas) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  int channel = std::atoi(GetQueryParam(query, "ch").c_str());
  int idx = ChannelIndex(channel);
  if (idx < 0) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("ch 값은 1~4 이어야 함");
    return;
  }

  std::string dict_name = GetQueryParam(query, "dict");
  if (dict_name.empty()) {
    dict_name = "DICT_4X4_50";
  }

  static const std::map<std::string, cv::aruco::PREDEFINED_DICTIONARY_NAME> kDictMap = {
      {"DICT_4X4_50", cv::aruco::DICT_4X4_50},     {"DICT_4X4_100", cv::aruco::DICT_4X4_100},
      {"DICT_4X4_250", cv::aruco::DICT_4X4_250},   {"DICT_4X4_1000", cv::aruco::DICT_4X4_1000},
      {"DICT_5X5_50", cv::aruco::DICT_5X5_50},     {"DICT_5X5_100", cv::aruco::DICT_5X5_100},
      {"DICT_5X5_250", cv::aruco::DICT_5X5_250},   {"DICT_5X5_1000", cv::aruco::DICT_5X5_1000},
      {"DICT_6X6_50", cv::aruco::DICT_6X6_50},     {"DICT_6X6_100", cv::aruco::DICT_6X6_100},
      {"DICT_6X6_250", cv::aruco::DICT_6X6_250},   {"DICT_6X6_1000", cv::aruco::DICT_6X6_1000},
      {"DICT_7X7_50", cv::aruco::DICT_7X7_50},     {"DICT_7X7_100", cv::aruco::DICT_7X7_100},
      {"DICT_7X7_250", cv::aruco::DICT_7X7_250},   {"DICT_7X7_1000", cv::aruco::DICT_7X7_1000},
      {"DICT_ARUCO_ORIGINAL", cv::aruco::DICT_ARUCO_ORIGINAL},
  };

  auto it = kDictMap.find(dict_name);
  if (it == kDictMap.end()) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("알 수 없는 dict 이름: " + dict_name);
    return;
  }

  std::vector<unsigned char> jpeg;
  std::string fetch_error;
  if (!FetchSnapshot(channel, jpeg, fetch_error)) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("스냅샷 요청 실패: " + fetch_error);
    return;
  }

  cv::Mat gray = cv::imdecode(jpeg, cv::IMREAD_GRAYSCALE);
  if (gray.empty()) {
    oas->SetStatusCode(400);
    oas->SetResponseBody("JPEG 디코딩 실패");
    return;
  }

  static const cv::Ptr<cv::aruco::DetectorParameters> params = MakeDetectorParams();
  auto test_dictionary = cv::aruco::getPredefinedDictionary(it->second);

  // 이 딕셔너리의 실제 비트 데이터(bytesList) 체크섬. Python(pip opencv-contrib-python, 최신 버전)
  // 쪽에서 같은 방식으로 계산한 값이랑 대조해서, "같은 이름의 딕셔너리인데 버전 간에 실제
  // 바이트가 다른가"를 직접 확인하기 위함.
  uint64_t dict_checksum = 0;
  cv::Mat bytes_list = test_dictionary->bytesList;
  cv::Mat bytes_list_c = bytes_list.isContinuous() ? bytes_list : bytes_list.clone();
  const uchar* bytes_data = bytes_list_c.ptr<uchar>(0);
  size_t bytes_total = bytes_list_c.total() * bytes_list_c.elemSize();
  for (size_t i = 0; i < bytes_total; ++i) {
    dict_checksum = dict_checksum * 31 + bytes_data[i];
  }

  std::vector<int> marker_ids;
  std::vector<std::vector<cv::Point2f>> marker_corners;
  std::vector<std::vector<cv::Point2f>> rejected_candidates;
  cv::aruco::detectMarkers(gray, test_dictionary, marker_corners, marker_ids, params, rejected_candidates);

  JsonUtility::JsonDocument res(JsonUtility::Type::kObjectType);
  auto& alloc = res.GetAllocator();
  res.AddMember("ok", true, alloc);
  res.AddMember("dict", dict_name, alloc);
  res.AddMember("dict_bytes_rows", bytes_list.rows, alloc);
  res.AddMember("dict_bytes_cols", bytes_list.cols, alloc);
  res.AddMember("dict_marker_size", test_dictionary->markerSize, alloc);
  res.AddMember("dict_max_correction_bits", test_dictionary->maxCorrectionBits, alloc);
  res.AddMember("dict_checksum", std::to_string(dict_checksum), alloc);
  res.AddMember("marker_count", (int)marker_ids.size(), alloc);
  res.AddMember("rejected_count", (int)rejected_candidates.size(), alloc);
  auto ids_arr = JsonUtility::ValueType(JsonUtility::Type::kArrayType);
  for (int id : marker_ids) {
    ids_arr.PushBack(id, alloc);
  }
  res.AddMember("ids", ids_arr, alloc);

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

  // 이 앱의 채널 번호(1~4)는 사용자용 표기이고, stw-cgi의 Channel 파라미터는 0부터 시작함(0~3).
  std::ostringstream url;
  // Profile=0: 고화질(메인 스트림), Profile=1: 저화질(서브 스트림) - 캘리브레이션 정확도를 위해 고화질 사용.
  url << "http://127.0.0.1/stw-cgi/video.cgi?msubmenu=snapshot&action=view&Channel=" << (channel - 1) << "&Profile=0";

  std::string response;
  std::string userpwd = camera_credentials_.admin_user + ":" + camera_credentials_.admin_pass;

  curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  // Basic으로 고정 시도했으나 401(카메라가 Basic이 아닌 다른 인증 방식을 요구) 발생해서 원복.
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
