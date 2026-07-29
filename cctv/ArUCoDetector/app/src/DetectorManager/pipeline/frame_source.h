#pragma once

#include <string>

#include <opencv2/core.hpp>

class RawFrameStore;

// 채널별 최신 raw 프레임(grayscale)을 그대로 돌려준다.
// (BGR로 변환하지 않음 — undistort가 gray에도 그대로 동작해서 왕복 변환이 불필요함.)
// 실패(아직 프레임 없음) 시 빈 Mat 반환 + out_error 설정.
class FrameSource {
    public:
        explicit FrameSource(RawFrameStore* store);
        cv::Mat Acquire(int channel, std::string& out_error) const;

    private:
        RawFrameStore* store_;
};
