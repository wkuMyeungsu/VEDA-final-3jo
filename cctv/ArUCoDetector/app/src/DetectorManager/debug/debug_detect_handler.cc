// ===== 테스트 전용 (나중에 통째로 삭제) =====
#include "debug_detect_handler.h"

#include <cstdlib>
#include <string>
#include <vector>

#include <opencv2/aruco.hpp>       // cv::aruco::drawDetectedMarkers
#include <opencv2/imgcodecs.hpp>   // cv::imencode (프리뷰)

#include "aruco_detector.h"
#include "camera_calibration.h"
#include "frame_preprocessor.h"
#include "dispatcher_serialize.h"
#include "json_utility.h"

namespace {

// JPEG 바이트를 data URI(base64)로 만들어 응답 JSON에 바로 심기 위한 인코더
// (ArUCoCalibration의 프리뷰 인코더와 동일).
std::string Base64Encode(const std::vector<unsigned char>& data) {
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

}  // namespace

namespace DebugDetectHandler {

void HandleDetectOnce(OpenAppSerializable* oas, const SendMetadataFn& send_metadata, const FrameProviderFn& get_frame) {
  std::string query = oas->GetFCGXParam("QUERY_STRING");
  // ?ch=3 파싱
  int channel = 1;
  auto pos = query.find("ch=");
  if (pos != std::string::npos) {
    channel = std::atoi(query.c_str() + pos + 3);
  }
  // ?undistort=0 이면 왜곡보정 강제 OFF (기본 1: 캘리브레이션 파일 있으면 적용).
  // 카메라 펌웨어가 이미 렌즈 보정을 하는 경우 이중 보정이 되므로 끄고 원본으로 확인용.
  bool undistort_requested = true;
  auto upos = query.find("undistort=");
  if (upos != std::string::npos) {
    undistort_requested = (std::atoi(query.c_str() + upos + 10) != 0);
  }

  // 1) 프레임 획득 — 주입된 provider(raw 비디오 최신 프레임)에서 1장 가져온다.
  std::string error;
  cv::Mat img = get_frame(channel, error);
  if (img.empty()) {
    oas->SetStatusCode(503);
    oas->SetResponseBody("프레임 없음: " + error);
    return;
  }

  // 2) 캘리브레이션 로드 + 왜곡보정(옵션) + grayscale (파이프라인 조각 사용)
  std::string calib_path = "/mnt/opensdk/apps/ArUCoCalibration/app/bin/calib_result_ch" + std::to_string(channel) + ".json";
  CameraCalibration calib = LoadCameraCalibration(calib_path);
  bool calibration_available = calib.valid;
  bool undistorted_applied = false;
  cv::Mat corrected = TryUndistort(img, calib, undistort_requested, undistorted_applied);
  cv::Mat gray = ConvertToGrayscale(corrected);

  // 3) 검출
  ArucoDetector detector;
  DetectionResult result = detector.Detect(gray);

  // 4) 메타데이터 전송 (실제 운영 경로를 그대로 태워서 검증)
  send_metadata(channel, result.ids, result.corners);

  // 5) 프리뷰: corrected(보정본 or 원본)에 마커 그려서 base64 data URI로 인코딩
  if (!result.ids.empty()) {
    cv::aruco::drawDetectedMarkers(corrected, result.corners, result.ids);
  }
  std::vector<unsigned char> preview_jpeg;
  cv::imencode(".jpg", corrected, preview_jpeg);
  std::string preview_uri = "data:image/jpeg;base64," + Base64Encode(preview_jpeg);

  // 6) 응답 JSON (검출 개수 + 프리뷰 이미지)
  JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
  auto& alloc = doc.GetAllocator();
  doc.AddMember("channel", channel, alloc);
  doc.AddMember("marker_count", static_cast<int>(result.ids.size()), alloc);
  doc.AddMember("rejected_count", result.rejected_count, alloc);
  doc.AddMember("calibration_available", calibration_available, alloc);
  doc.AddMember("undistort_requested", undistort_requested, alloc);
  doc.AddMember("undistorted", undistorted_applied, alloc);

  JsonUtility::ValueType ids_arr(JsonUtility::Type::kArrayType);
  for (int id : result.ids) {
    ids_arr.PushBack(id, alloc);
  }
  doc.AddMember("marker_ids", ids_arr, alloc);

  doc.AddMember("preview", preview_uri, alloc);

  rapidjson::StringBuffer strbuf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
  doc.Accept(writer);
  oas->SetResponseBody(strbuf.GetString(), strbuf.GetLength());
}



}  // namespace DebugDetectHandler
