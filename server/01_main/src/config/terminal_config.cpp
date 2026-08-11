// terminal_config.cpp
// 단말 설정 JSON 로더 구현
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 스키마·계약 설명은 terminal_config.hpp에 있다. 여기는 읽기/검증만 한다.
// nlohmann/json은 이 .cpp 안에서만 쓴다(헤더로 새어나가지 않게).

#include "config/terminal_config.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace forklift::config {
namespace {

using nlohmann::json;

[[noreturn]] void throwSchema(const std::string& path, const std::string& detail) {
    throw TerminalConfigError(TerminalConfigError::Code::SchemaInvalid, path, detail);
}

// 객체 안에서 지정한 키를 꺼낸다. 없거나 타입이 다르면 어떤 키가 문제인지 밝히고 던진다.
// nlohmann의 at()/get<>()도 예외를 던지지만, 메시지가 "type must be number, but is string"
// 식이라 어느 필드인지 안 나온다 - 설정 파일 오타를 잡는 게 목적이라 직접 검사한다.
// 참조가 아니라 포인터를 돌려주는 이유: 반환값은 parent가 소유한 노드라 수명 문제가
// 없는데, 참조로 돌려주면 GCC 13의 -Wdangling-reference가 오탐한다(인자 중 임시 문자열이
// 있어서 그걸 가리키는 참조로 오해한다). 호출부에서 한 번 역참조하면 경고가 사라진다.
const json* requireObject(const json& parent, const std::string& key, const std::string& path) {
    auto it = parent.find(key);
    if (it == parent.end()) {
        throwSchema(path, "필수 객체 \"" + key + "\" 가 없음");
    }
    if (!it->is_object()) {
        throwSchema(path, "\"" + key + "\" 는 객체여야 함");
    }
    return &*it;
}

int requireInt(const json& parent, const std::string& section, const std::string& key,
               const std::string& path) {
    auto it = parent.find(key);
    if (it == parent.end()) {
        throwSchema(path, "필수 필드 \"" + section + "." + key + "\" 가 없음");
    }
    // is_number_integer()는 3.0 같은 실수 표기를 거른다. 프레임 수/ms는 정수만 허용한다.
    if (!it->is_number_integer()) {
        throwSchema(path, "\"" + section + "." + key + "\" 는 정수여야 함");
    }
    return it->get<int>();
}

std::string requireString(const json& parent, const std::string& section, const std::string& key,
                          const std::string& path) {
    auto it = parent.find(key);
    if (it == parent.end()) {
        throwSchema(path, "필수 필드 \"" + section + "." + key + "\" 가 없음");
    }
    if (!it->is_string()) {
        throwSchema(path, "\"" + section + "." + key + "\" 는 문자열이어야 함");
    }
    std::string value = it->get<std::string>();
    if (value.empty()) {
        // 빈 문자열은 하류 JSON에서 null과 같은 뜻이 되는데(toJsonOrNull), 설정 파일에
        // 일부러 빈 값을 넣는 상황은 없으므로 오타로 보고 거른다.
        throwSchema(path, "\"" + section + "." + key + "\" 가 빈 문자열");
    }
    return value;
}

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

}  // namespace

TerminalConfigError::TerminalConfigError(Code code, std::string path, const std::string& detail)
    : std::runtime_error("[terminal_config] " + toString(code) + " (" + path + "): " + detail),
      code_(code),
      path_(std::move(path)) {}

std::string toString(TerminalConfigError::Code code) {
    switch (code) {
        case TerminalConfigError::Code::FileNotFound:  return "FILE_NOT_FOUND";
        case TerminalConfigError::Code::ParseFailed:   return "PARSE_FAILED";
        case TerminalConfigError::Code::SchemaInvalid: return "SCHEMA_INVALID";
    }
    return "UNKNOWN";
}

TerminalConfig loadTerminalConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw TerminalConfigError(TerminalConfigError::Code::FileNotFound, path,
                                  "파일을 열 수 없음");
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    json root;
    try {
        // allow_exceptions=true(기본), ignore_comments=false.
        // 설정 파일에 주석을 허용하면 편하지만, 이 파일은 배포 스크립트가 기계적으로
        // 만들 수도 있어서 표준 JSON만 받는 쪽으로 잠가 둔다.
        root = json::parse(buffer.str());
    } catch (const json::parse_error& e) {
        throw TerminalConfigError(TerminalConfigError::Code::ParseFailed, path, e.what());
    }

    if (!root.is_object()) {
        throwSchema(path, "최상위가 객체가 아님");
    }

    const json& forklift = *requireObject(root, "forklift", path);
    const json& handover = *requireObject(root, "handover", path);

    TerminalConfig cfg;
    cfg.forklift.marker_id   = requireInt(forklift, "forklift", "marker_id", path);
    cfg.forklift.forklift_id = requireString(forklift, "forklift", "forklift_id", path);
    cfg.forklift.terminal_id = requireString(forklift, "forklift", "terminal_id", path);
    cfg.handover.confirm_frames = requireInt(handover, "handover", "confirm_frames", path);
    cfg.handover.lost_grace_ms  = requireInt(handover, "handover", "lost_grace_ms", path);

    // ── 값 범위 검증 ────────────────────────────────────────────
    // 여기서 거르지 않으면 MarkerChannelTracker가 조용히 이상하게 동작한다:
    // confirm_frames <= 0이면 첫 프레임에 바로 전환(오검출 한 방에 화면이 튐),
    // 음수 marker_id/lost_grace_ms는 영영 매칭되지 않거나 유예가 없는 것과 같다.
    if (cfg.forklift.marker_id < 0) {
        throwSchema(path, "\"forklift.marker_id\" 는 0 이상이어야 함 (실제: " +
                              std::to_string(cfg.forklift.marker_id) + ")");
    }
    if (cfg.handover.confirm_frames < 1) {
        throwSchema(path, "\"handover.confirm_frames\" 는 1 이상이어야 함 (실제: " +
                              std::to_string(cfg.handover.confirm_frames) + ")");
    }
    if (cfg.handover.lost_grace_ms < 0) {
        throwSchema(path, "\"handover.lost_grace_ms\" 는 0 이상이어야 함 (실제: " +
                              std::to_string(cfg.handover.lost_grace_ms) + ")");
    }

    return cfg;
}

std::string terminalConfigFileName(const std::string& terminal_id) {
    return "terminal_" + terminal_id + ".json";
}

std::string resolveTerminalConfigPath(const std::string& terminal_id) {
    const std::string name = terminalConfigFileName(terminal_id);
    // server/01_main, server, build 디렉터리에서 실행하는 경우를 지원한다.
    const std::string candidates[] = {
        "config/" + name,
        "../config/" + name,
        "01_main/config/" + name,
        "server/01_main/config/" + name,
        "../01_main/config/" + name,
    };
    for (const auto& candidate : candidates) {
        if (fileExists(candidate)) return candidate;
    }
    return candidates[0];
}

}  // namespace forklift::config
