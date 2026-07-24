#pragma once

#include <vector>
#include <string>

#include <opencv2/aruco.hpp>
#include <opencv2/core.hpp>

// 검출 한 번의 결과 값을 담을 일회성 구조체
struct DetectionResult {
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners;
    int rejected_count = 0;
};

// 한 번 호출 후 여러번 재사용하는 서비스 객체
class ArucoDetector {
    public:
        explicit ArucoDetector(cv::aruco::PREDEFINED_DICTIONARY_NAME dictionary_name = cv::aruco::DICT_4X4_50);
        DetectionResult Detect(const cv::Mat& gray) const;

    private:
        cv::Ptr<cv::aruco::Dictionary> dictionary_;
        cv::Ptr<cv::aruco::DetectorParameters> parameters_;
};

// html에서 고른 사전 문자열(DICT_4X4_50 등 7종)을 `PREDEFINED_DICTIONARY_NAME enum` 으로 바꾸는 함수
// (특정 ArucoDetector 인스턴스에 의존하지 않는 순수 변환이라 자유 함수로 둠)
cv::aruco::PREDEFINED_DICTIONARY_NAME StringToDict(const std::string& name);