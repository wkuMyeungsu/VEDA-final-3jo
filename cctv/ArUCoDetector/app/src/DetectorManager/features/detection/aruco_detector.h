#pragma once

#include <vector>
#include <string>

#include <opencv2/aruco.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <opencv2/core/version.hpp>

#if (CV_VERSION_MAJOR > 4) || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 10)
#define CCTV_ARUCO_MODERN_API 1
#else
#define CCTV_ARUCO_MODERN_API 0
#endif

#if CCTV_ARUCO_MODERN_API
using ArucoDictionaryName = cv::aruco::PredefinedDictionaryType;
#else
using ArucoDictionaryName = cv::aruco::PREDEFINED_DICTIONARY_NAME;
#endif

// 검출 한 번의 결과 값을 담을 일회성 구조체
struct DetectionResult {
    std::vector<int> ids;
    std::vector<std::vector<cv::Point2f>> corners;
    int rejected_count = 0;
};

// 한 번 호출 후 여러번 재사용하는 서비스 객체
class ArucoDetector {
    public:
        static cv::Ptr<ArucoDetector> Create(ArucoDictionaryName dictionary_name);

        DetectionResult Detect(const cv::Mat& gray) const;

    private:
        ArucoDetector(ArucoDictionaryName dictionary_name,
                      const cv::Ptr<cv::aruco::DetectorParameters>& parameters);

        cv::Ptr<cv::aruco::Dictionary> dictionary_;
        cv::Ptr<cv::aruco::DetectorParameters> parameters_;
#if CCTV_ARUCO_MODERN_API
        cv::Ptr<cv::aruco::ArucoDetector> aruco_detector_;
#endif
};

bool IsSupportedArucoDictionary(const std::string& name);
ArucoDictionaryName StringToDict(const std::string& name);
