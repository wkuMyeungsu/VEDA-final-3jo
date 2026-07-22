#pragma once

#include <string>
#include <vector>

#include "camera_credentials.h"

// 카메라 자신의 stw-cgi(SUNAPI)에 스냅샷을 요청해 JPEG 바이트를 받아온다.
// Channel은 1부터 시작하는 사용자용 채널 번호.
bool FetchSnapshot(
    int channel,
    const CameraCredentials& credentials,
    std::vector<unsigned char>& out_jpeg, std::string& out_error
);