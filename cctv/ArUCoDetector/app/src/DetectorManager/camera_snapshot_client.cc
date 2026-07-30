#include "camera_snapshot_client.h"

#include <sstream>

#include <curl/curl.h>

namespace {
    size_t CurlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* response = reinterpret_cast<std::string*>(userdata);
        response->append(ptr, size * nmemb);
        return size * nmemb;
    }
}

bool FetchSnapshot(int channel, const CameraCredentials& credentials, std::vector<unsigned char>& out_jpeg, std::string& out_error) {
    if(credentials.admin_pass.empty()) {
        out_error = "카메라 인증정보가 없습니다 (config.local.json 확인)";
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        out_error = "curl_easy_init 실패";
        return false;
    }

    // stw-cgi = 한화 카메라 펌웨어가 제공하는 자체 HTTP API(SUNAPI)의 경로.
    // 127.0.0.1로 보내는 이유: 이 앱 카메라 자신에게 얹혀 도는 APP이라, 자기 자신의 웹서버(카메라 펌웨어)에 루프백으로 스냅샷을 요청함.
    // 이 앱의 채널 번호(1~4)는 사용자용 표기이고, stw-cgi의 Channel 파라미터는 0부터 시작함(0~3)

    std::ostringstream url;

    // Profile=0: 고화질(메인 스트림), Profile=1: 저화질(서브 스트림) ㅡ 캘리브레이션 정확도를 위해 고화질 사용.
    url << "http://127.0.0.1/stw-cgi/video.cgi?msubmenu=snapshot&action=view&Channel=" << (channel - 1) << "&Profile=0";

    std::string response;
    std::string userpwd = credentials.admin_user + ":" + credentials.admin_pass;

    curl_easy_setopt(curl, CURLOPT_URL, url.str().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());

    CURLcode res = curl_easy_perform(curl);
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        out_error = std::string("curl 오류: ") + curl_easy_strerror(res);
        return false;
    }
    if (status_code != 200) {
        out_error = "HTTP 상태 코드: " + std::to_string(status_code);
        return false;
    }
    if (response.empty()) {
        out_error = "응답 바디가 비어있음";
        return false;
    }

    out_jpeg.assign(response.begin(), response.end());
    return true;
}