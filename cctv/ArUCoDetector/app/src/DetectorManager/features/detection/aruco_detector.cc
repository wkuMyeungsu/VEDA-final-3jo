// aruco_detector.cc

#include <unordered_map>
#include "aruco_detector.h"

namespace {

cv::Ptr<cv::aruco::Dictionary> MakeDictionary(ArucoDictionaryName dictionary_name) {
#if CCTV_ARUCO_MODERN_API
  return cv::makePtr<cv::aruco::Dictionary>(
      cv::aruco::getPredefinedDictionary(dictionary_name));
#else
  return cv::aruco::getPredefinedDictionary(dictionary_name);
#endif
}

}  // namespace


const std::unordered_map<std::string, ArucoDictionaryName>& Dictionaries() {
  static const std::unordered_map<std::string, ArucoDictionaryName> kMap = {
    {"DICT_4X4_50",  cv::aruco::DICT_4X4_50},
    {"DICT_4X4_100", cv::aruco::DICT_4X4_100},
    {"DICT_4X4_250", cv::aruco::DICT_4X4_250},
    {"DICT_5X5_50",  cv::aruco::DICT_5X5_50},
    {"DICT_5X5_100", cv::aruco::DICT_5X5_100},
    {"DICT_6X6_50",  cv::aruco::DICT_6X6_50},
    {"DICT_6X6_250", cv::aruco::DICT_6X6_250},
  };
  return kMap;
}

bool IsSupportedArucoDictionary(const std::string& name) {
  return Dictionaries().find(name) != Dictionaries().end();
}

ArucoDictionaryName StringToDict(const std::string& name) {
  const auto& dictionaries = Dictionaries();
  const auto it = dictionaries.find(name);
  return it != dictionaries.end() ? it->second : cv::aruco::DICT_4X4_50;
}

ArucoDetector::ArucoDetector(
    ArucoDictionaryName dictionary_name,
    const cv::Ptr<cv::aruco::DetectorParameters>& parameters)
    : dictionary_(MakeDictionary(dictionary_name)),
      parameters_(parameters)
#if CCTV_ARUCO_MODERN_API
      , aruco_detector_(cv::makePtr<cv::aruco::ArucoDetector>(*dictionary_, *parameters_))
#endif
      {}

cv::Ptr<ArucoDetector> ArucoDetector::Create(ArucoDictionaryName dictionary_name) {
#if CCTV_ARUCO_MODERN_API
  auto parameters = cv::makePtr<cv::aruco::DetectorParameters>();
#else
  auto parameters = cv::aruco::DetectorParameters::create();
#endif
  // 사전이 허용하는 오류 정정 한도까지 사용한다. 전처리/프로파일은 운영 경로에 없다.
  parameters->errorCorrectionRate = 1.0;
  return cv::Ptr<ArucoDetector>(new ArucoDetector(dictionary_name, parameters));
}

// 흑백 (gray) 이미지 한 장을 받아서 그 안의 ArUCo 마커를 검출하고 결과를 반환한다.
// 이 함수는 dictionary_/parameters_ 를 읽기만 하고 바꾸지 않는다. --> const
DetectionResult ArucoDetector::Detect(const cv::Mat& gray) const {
    
    // 반환할 결과를 담을 변수. ids/corners는 처음엔 비어있고 detectMarkers()가 채워준다.
    DetectionResult result;

    // 사각형처럼 보이긴 했는데 비트 패턴이 사전 어떤 마커와도 안 맞아서
    // 탈락한 후보들의 코너 좌표. ID로 확정되지 않았으므로 DetectionResult에는 안 담고 여기서 개수만 세고 버린다.
    std::vector<std::vector<cv::Point2f>> rejected;

    // 실제 검출 수행
    // 1. gray              : 입력 이미지
    // 2. dictionary_       : 어떤 마커 집합을 찾을지
    // 3. result.corners    : [출력] 검출된 각 마커의 4개 꼭짓점 (마커당 하나씩, ids와 인덱스로 짝지어짐.)
    // 4. result.ids        : [출력] 검출된 각 마커의 ID
    // 5. parameters_       : 검출 알고리즘 튜닝값
    // 6. rejected          : [출력] ID 판별에 실패한 후보들의 코너
#if CCTV_ARUCO_MODERN_API
    aruco_detector_->detectMarkers(gray, result.corners, result.ids, rejected);
#else
    cv::aruco::detectMarkers(gray, dictionary_, result.corners, result.ids, parameters_, rejected);
#endif
    
    // 탈락한 후보 개수만 기록 (좌표 자체는 버림) **필요해지면 DetectionResult에 필드 추가하면 됨**
    result.rejected_count = static_cast<int>(rejected.size());

    return result;
}
