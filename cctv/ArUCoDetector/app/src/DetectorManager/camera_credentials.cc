#include "camera_credentials.h"

#include <fstream>
#include <sstream>

#include "json_utility.h"
#include "dispatcher_serialize.h"

namespace {

    // admin_user/admin_pass 같은 실제 비밀번호가 들어가는 파일이라 git에는 올리지 않음
    // 값이 비어있는 템플릿인 config.example.json만 커밋되어 있고, 
    // 실제 값은 배포 및 실행 환경에서 이 이름으로 직접 만들어 넣어야 함.
    // 실행 CWD 기준 상대 경로.
    constexpr const char* kConfigPath = "config.local.json";
}

CameraCredentials LoadCameraCredentials() {
    CameraCredentials creds;

    // 파일이 없으면 에러가 아니라 "아직 설정 안 함"으로 판단하고 빈 값 반환.
    std::ifstream ifs(kConfigPath);
    if (!ifs.is_open()) {
        return creds;
    }

    std::stringstream ss;
    ss << ifs.rdbuf();

    // JSON 문법이 깨져있어도 크래시 대신 빈 값 반환 (호출부가 인증 실패로 처리)
    JsonUtility::JsonDocument doc(JsonUtility::Type::kObjectType);
    doc.Parse(ss.str());
    if (doc.HasParseError()) {
        return creds;
    }

    if (doc.HasMember("admin_user")) {
        creds.admin_user = doc["admin_user"].GetString();
    }
    if (doc.HasMember("admin_pass")) {
        creds.admin_pass = doc["admin_pass"].GetString();
    }
    return creds;
}