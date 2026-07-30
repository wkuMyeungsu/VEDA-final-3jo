#include "frame_source.h"

#include "raw_frame_store.h"

FrameSource::FrameSource(RawFrameStore* store)
    : store_(store) {}

cv::Mat FrameSource::Acquire(int channel, std::string& out_error) const {
    cv::Mat gray = store_->Get(channel);   // 채널별 최신 raw 프레임(grayscale)
    if (gray.empty()) {
        out_error = "아직 raw 프레임 수신 전 (ch " + std::to_string(channel) + ")";
        return cv::Mat();
    }
    return gray;   // BGR로 안 부풀림 — 이후 파이프라인(undistort)이 gray를 그대로 다룬다.
}
