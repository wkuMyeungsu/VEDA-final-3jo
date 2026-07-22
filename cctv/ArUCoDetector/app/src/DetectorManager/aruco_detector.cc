// aruco_detector.cc
#include "aruco_detector.h"

// 
ArucoDetector::ArucoDetector(cv::aruco::PREDEFINED_DICTIONARY_NAME dictionary_name)
    
      // dictionary_name(DICT_4X4_50)에 해당하는 마커 사전을 한 번만 로드해서 저장.
      // 이후 Detect()가 몇 번 호출되든 이 사전을 재사용한다.
    : dictionary_(cv::aruco::getPredefinedDictionary(dictionary_name)),
      
      // 검출 알고리즘 튜닝 파라미터(threshold 등)를 기본값으로 생성해서 저장. **나중에 settings.json 값을 반영하려면 이 멤버를 직접 수정하면 된다.**
      parameters_(cv::aruco::DetectorParameters::create()) {}

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
    cv::aruco::detectMarkers(gray, dictionary_, result.corners, result.ids, parameters_, rejected);
    
    // 탈락한 후보 개수만 기록 (좌표 자체는 버림) **필요해지면 DetectionResult에 필드 추가하면 됨**
    result.rejected_count = static_cast<int>(rejected.size());

    return result;
}