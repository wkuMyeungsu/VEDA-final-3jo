#pragma once

#include <string>

#include <opencv2/core.hpp>

#include "camera_credentials.h"

// 채널 스냅샷을 받아 디코딩된 컬러(BGR) 프레임으로 돌려준다.
// (FetchSnapshot + imdecode 캡슐화). 실패 시 빈 Mat 반환 + out_error 설정.
// 추후, 필요하면 RTSP 프레임 소스로 교체할 수도 있음.
class FrameSource {
    public:
        explicit FrameSource(const CameraCredentials& credentials);
        cv::Mat Acquire(int channel, std::string& out_error) const;

    private:
        CameraCredentials credentials_;
};