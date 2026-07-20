#pragma once

#include <string>

// 카메라 stw-cgi 요청용 admin 계정. config.local.json(레포에 안 올라감, config.example.json 참고)에서
// 읽어옴. 파일이 없으면 빈 값을 돌려주므로, 호출부에서 인증이 필요한 요청 전에 값 존재를 확인해야 함.
struct CameraCredentials {
  std::string admin_user;
  std::string admin_pass;
};

CameraCredentials LoadCameraCredentials();
