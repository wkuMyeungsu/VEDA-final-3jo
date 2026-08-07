// test_terminal_config.cpp
// 단말 설정 JSON 로더(terminal_config) 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   [테스트 1] 정상 파일을 읽으면 모든 필드가 스키마대로 채워지는지
//   [테스트 2] 파일이 없으면 FILE_NOT_FOUND로 실패하는지
//   [테스트 3] JSON 문법이 깨지면 PARSE_FAILED로 실패하는지
//   [테스트 4] 필수 키 누락 / 타입 불일치 / 값 범위 위반이 SCHEMA_INVALID로 걸리는지
//   [테스트 5] terminal_id -> 파일명 규칙
//
// 로더가 실패를 "조용히 기본값으로 때우지 않는지"가 핵심이다. 반쯤 채워진 설정으로
// 서버가 뜨면 지게차가 어느 카메라에도 안 잡히는 것처럼 보여서 원인 추적이 어렵다.
//
// 임시 파일은 실행 디렉터리(보통 build/tests)에 만들고 테스트 끝에 지운다.
//
// 빌드/실행: ctest -R terminal_config_test --output-on-failure

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#include "config/terminal_config.hpp"

namespace {

using forklift::config::TerminalConfigError;

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

// 내용이 content인 임시 파일을 만들고, 소멸 시 지운다.
class TempFile {
public:
    TempFile(std::string name, const std::string& content) : path_(std::move(name)) {
        std::ofstream out(path_, std::ios::trunc);
        out << content;
    }
    ~TempFile() { std::remove(path_.c_str()); }

    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// 로드가 주어진 사유 코드로 실패하는지 확인한다.
void expectFailure(const std::string& path, TerminalConfigError::Code expected,
                   const std::string& what) {
    try {
        forklift::config::loadTerminalConfig(path);
        check(false, what + " (실제: 예외 없이 성공함)");
    } catch (const TerminalConfigError& e) {
        check(e.code() == expected,
              what + " (실제: " + forklift::config::toString(e.code()) + ")");
    } catch (const std::exception& e) {
        check(false, what + " (실제: 다른 예외 - " + e.what() + ")");
    }
}

const char* kValidJson = R"({
  "forklift": {"marker_id": 0, "forklift_id": "FL_01", "terminal_id": "TERM_01"},
  "handover": {"confirm_frames": 3, "lost_grace_ms": 500}
})";

void testValidConfigLoads() {
    std::cout << "[테스트 1] 정상 설정 파일 로드\n";

    TempFile file("test_terminal_valid.json", kValidJson);
    try {
        const auto cfg = forklift::config::loadTerminalConfig(file.path());
        check(cfg.forklift.marker_id == 0, "forklift.marker_id = 0");
        check(cfg.forklift.forklift_id == "FL_01", "forklift.forklift_id = FL_01");
        check(cfg.forklift.terminal_id == "TERM_01", "forklift.terminal_id = TERM_01");
        check(cfg.handover.confirm_frames == 3, "handover.confirm_frames = 3");
        check(cfg.handover.lost_grace_ms == 500, "handover.lost_grace_ms = 500");
        check(cfg.handover.lostGrace() == std::chrono::milliseconds(500),
              "lostGrace()가 ms 타입으로 변환됨");
    } catch (const std::exception& e) {
        check(false, std::string("정상 파일인데 예외 발생: ") + e.what());
    }
}

void testMissingFile() {
    std::cout << "\n[테스트 2] 파일 없음 -> FILE_NOT_FOUND\n";
    expectFailure("test_terminal_does_not_exist.json", TerminalConfigError::Code::FileNotFound,
                  "없는 경로 -> FILE_NOT_FOUND");
}

void testBrokenJson() {
    std::cout << "\n[테스트 3] JSON 문법 오류 -> PARSE_FAILED\n";

    TempFile truncated("test_terminal_broken.json", R"({"forklift": {"marker_id": 0,)");
    expectFailure(truncated.path(), TerminalConfigError::Code::ParseFailed,
                  "중간에 잘린 JSON -> PARSE_FAILED");

    TempFile empty("test_terminal_empty.json", "");
    expectFailure(empty.path(), TerminalConfigError::Code::ParseFailed,
                  "빈 파일 -> PARSE_FAILED");
}

void testSchemaViolations() {
    std::cout << "\n[테스트 4] 스키마 위반 -> SCHEMA_INVALID\n";

    TempFile noSection("test_terminal_no_handover.json",
                       R"({"forklift": {"marker_id": 0, "forklift_id": "FL_01",
                           "terminal_id": "TERM_01"}})");
    expectFailure(noSection.path(), TerminalConfigError::Code::SchemaInvalid,
                  "handover 절 누락 -> SCHEMA_INVALID");

    TempFile noKey("test_terminal_no_marker_id.json",
                   R"({"forklift": {"forklift_id": "FL_01", "terminal_id": "TERM_01"},
                       "handover": {"confirm_frames": 3, "lost_grace_ms": 500}})");
    expectFailure(noKey.path(), TerminalConfigError::Code::SchemaInvalid,
                  "forklift.marker_id 누락 -> SCHEMA_INVALID");

    TempFile wrongType("test_terminal_wrong_type.json",
                       R"({"forklift": {"marker_id": "0", "forklift_id": "FL_01",
                           "terminal_id": "TERM_01"},
                           "handover": {"confirm_frames": 3, "lost_grace_ms": 500}})");
    expectFailure(wrongType.path(), TerminalConfigError::Code::SchemaInvalid,
                  "marker_id가 문자열 -> SCHEMA_INVALID");

    TempFile emptyId("test_terminal_empty_id.json",
                     R"({"forklift": {"marker_id": 0, "forklift_id": "FL_01",
                         "terminal_id": ""},
                         "handover": {"confirm_frames": 3, "lost_grace_ms": 500}})");
    expectFailure(emptyId.path(), TerminalConfigError::Code::SchemaInvalid,
                  "terminal_id가 빈 문자열 -> SCHEMA_INVALID");

    // confirm_frames <= 0이면 오검출 한 프레임에 바로 전환돼버리므로 반드시 걸러야 한다.
    TempFile zeroFrames("test_terminal_zero_frames.json",
                        R"({"forklift": {"marker_id": 0, "forklift_id": "FL_01",
                            "terminal_id": "TERM_01"},
                            "handover": {"confirm_frames": 0, "lost_grace_ms": 500}})");
    expectFailure(zeroFrames.path(), TerminalConfigError::Code::SchemaInvalid,
                  "confirm_frames=0 -> SCHEMA_INVALID");

    TempFile negativeGrace("test_terminal_negative_grace.json",
                           R"({"forklift": {"marker_id": 0, "forklift_id": "FL_01",
                               "terminal_id": "TERM_01"},
                               "handover": {"confirm_frames": 3, "lost_grace_ms": -1}})");
    expectFailure(negativeGrace.path(), TerminalConfigError::Code::SchemaInvalid,
                  "lost_grace_ms 음수 -> SCHEMA_INVALID");
}

void testFileNameRule() {
    std::cout << "\n[테스트 5] terminal_id -> 파일명 규칙\n";
    check(forklift::config::terminalConfigFileName("TERM_01") == "terminal_TERM_01.json",
          "TERM_01 -> terminal_TERM_01.json");
    check(forklift::config::terminalConfigFileName("TERM_02") == "terminal_TERM_02.json",
          "TERM_02 -> terminal_TERM_02.json");
}

}  // namespace

int main() {
    std::cout << "=== 단말 설정 로더(terminal_config) 테스트 ===\n\n";

    testValidConfigLoads();
    testMissingFile();
    testBrokenJson();
    testSchemaViolations();
    testFileNameRule();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
