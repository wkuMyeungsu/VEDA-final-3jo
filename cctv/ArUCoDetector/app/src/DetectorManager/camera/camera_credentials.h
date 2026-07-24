#pragma once

#include <string>

// 카메라 stw-cgi 요청용 admin 계정.
struct CameraCredentials {
    std::string admin_user;
    std::string admin_pass;
};

CameraCredentials LoadCameraCredentials();