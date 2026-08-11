// terminal_config.hpp
// 단말 1대분 설정 파일(JSON) 로더 - 공개 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 배경: 지게차 마커 ID / 단말 ID / 핸드오버 판정 파라미터가 지금까지 main.cpp의
//       상수(kActiveCameraId, kTerminalId)로 박혀 있었다. 단말이 여러 대가 되면
//       실행파일을 다시 빌드해야 값이 바뀌므로 설정 파일로 뺀다.
//
// 파일 배치: 단말 1대당 파일 1개. server/01_main/config/terminal_<TERMINAL_ID>.json
//   예) server/01_main/config/terminal_TERM_01.json
//
// 스키마 (2026-08-07 확정):
//   {
//     "forklift": {"marker_id": 0, "forklift_id": "FL_01", "terminal_id": "TERM_01"},
//     "handover": {"confirm_frames": 3, "lost_grace_ms": 500}
//   }
//
// 파싱은 third_party/nlohmann/json.hpp(single-header)를 쓴다. 이 헤더는 그 사실을
// 밖으로 드러내지 않는다 - nlohmann 타입이 공개 API에 하나도 안 나오므로, 호출부는
// json.hpp(약 900KB)를 include할 필요가 없고 나중에 파서를 갈아끼워도 계약이 안 깨진다.
//
// 구현은 terminal_config.cpp에 있다. 이 헤더는 표준 헤더만 쓴다.

#pragma once

#include <chrono>
#include <stdexcept>
#include <string>

namespace forklift::config {

// 이 단말이 추적할 지게차 정보.
struct ForkliftConfig {
    // 지게차에 붙은 ArUco 마커 ID. 이 ID가 보이는 카메라 = 지게차가 있는 카메라.
    int marker_id = -1;

    // 지게차 자체의 식별자. 지금은 로그/디버깅용이고 하류 JSON 계약에는 없다
    // (판정 결과 스키마는 terminal_id/camera_id/zone까지만 확정돼 있음).
    std::string forklift_id;

    // 이 지게차 운전석 단말의 식별자. 9000 판정 결과와 9001 camera_assignment가
    // 둘 다 이 값으로 단말을 지목한다.
    std::string terminal_id;
};

// 카메라 자동 전환(핸드오버) 판정 파라미터. MarkerChannelTracker가 그대로 쓴다.
struct HandoverConfig {
    // 후보 카메라를 "확정"으로 인정하기까지 필요한 연속 검출 프레임 수.
    // 낮추면 전환이 빨라지는 대신 한두 프레임짜리 오검출에도 화면이 튄다.
    int confirm_frames = 3;

    // 현재 액티브 카메라에서 마커가 안 보여도 액티브를 유지해주는 유예 시간(ms).
    // 마커가 잠깐 가려지거나 프레임이 하나 드롭됐다고 화면이 바뀌면 안 되므로 둔다.
    int lost_grace_ms = 500;

    std::chrono::milliseconds lostGrace() const {
        return std::chrono::milliseconds(lost_grace_ms);
    }
};

struct TerminalConfig {
    ForkliftConfig forklift;
    HandoverConfig handover;
};

// 설정 로드 실패를 알리는 예외.
//
// 호출부가 "파일이 없다(배포 실수)"와 "내용이 잘못됐다(오타/스키마 위반)"를 구분해서
// 대응할 수 있도록 code()로 사유를 노출한다. what()에는 사람이 읽을 메시지가 들어간다.
class TerminalConfigError : public std::runtime_error {
public:
    enum class Code {
        FileNotFound,   // 경로에 파일이 없거나 열 수 없음
        ParseFailed,    // JSON 문법 자체가 깨짐
        SchemaInvalid,  // JSON은 유효하지만 필수 키 누락 / 타입 불일치 / 값 범위 위반
    };

    TerminalConfigError(Code code, std::string path, const std::string& detail);

    Code code() const noexcept { return code_; }
    const std::string& path() const noexcept { return path_; }

private:
    Code code_;
    std::string path_;
};

// 사유 코드를 로그용 문자열로 (예: "FILE_NOT_FOUND").
std::string toString(TerminalConfigError::Code code);

// path의 JSON을 읽어 TerminalConfig로 만든다.
// 실패하면 반드시 TerminalConfigError를 던진다(부분적으로 채워진 설정을 돌려주지 않는다) -
// 값이 반쯤 비어 있는 채로 판정이 돌면 "마커가 안 잡히는" 것처럼 보여서 원인 추적이 어렵다.
TerminalConfig loadTerminalConfig(const std::string& path);

// terminal_id -> 설정 파일 이름 ("TERM_01" -> "terminal_TERM_01.json").
std::string terminalConfigFileName(const std::string& terminal_id);

// terminal_id에 해당하는 설정 파일을 실행 위치 기준 후보 경로들에서 찾아 첫 번째로
// 존재하는 경로를 돌려준다. 어디에도 없으면 기본 후보("config/...")를 그대로 돌려준다
// (그래야 로드 실패 메시지가 "찾아봤어야 할 경로"를 가리킨다).
//
// 서버 경로가 전부 상대경로라 server/에서 실행할 때와 build/에서 실행할 때 CWD가
// 달라지는 기존 문제(README "런타임 데이터" 절)와 같은 성격이라 여기서도 완화만 한다.
std::string resolveTerminalConfigPath(const std::string& terminal_id);

}  // namespace forklift::config
