#include "frame_source.h"

#include <vector>

#include <opencv2/imgcodecs.hpp>    // cv::imdecode

#include "camera_snapshot_client.h"

FrameSource::FrameSource(const CameraCredentials& credentials) 
    : credentials_(credentials) {}

cv::Mat FrameSource::Acquire(int channel, std::string& out_error) const {
    std::vector<unsigned char> jpeg;
    if (!FetchSnapshot(channel, credentials_, jpeg, out_error)) {
        return cv::Mat();   // out_error는 FetchSnapshot이 채움.
    }

    cv::Mat img = cv::imdecode(jpeg, cv::IMREAD_COLOR);
    if (img.empty()) {
        out_error = "JPEG 디코딩 실패";
        return cv::Mat();
    }
    return img;
}