// test_event_logger.cpp
// EventLogger(SQLite 이벤트 로그) + ResultDispatcher 연동 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 확인 목적:
//   [테스트 1] DB 파일/테이블 자동 생성 (없으면 만들고, 스키마가 확정 스펙과 일치)
//   [테스트 2] log() 단독 - 값이 컬럼에 그대로 들어가는지 + NULL 규칙
//              (previous_risk_level 음수 -> NULL, camera_id "" -> NULL, distance_m -1 -> NULL)
//   [테스트 3] ResultDispatcher 연동 - 같은 값을 반복 submit해도 "변화가 있을 때만" 행이 쌓이고
//              previous_risk_level 체인이 정확한지  <- 이 테스트가 핵심
//   [테스트 4] 하트비트 재전송은 로그를 남기지 않는지 (무변화 재전송이 로그를 오염시키지 않음)
//   [테스트 5] 백프레셔 - 큐 초과 시 가장 오래된 것부터 드랍 + 드랍 카운터/로그
//   [테스트 6] 재실행 - 기존 DB에 이어붙이기 (CREATE TABLE IF NOT EXISTS)
//
// 모든 테스트는 임시 디렉터리의 전용 DB 파일을 쓰고 시작·종료 시 지운다.
// (운영 경로 server/judgment/events.db는 건드리지 않는다.)
//
// 테스트 1~3, 5~6은 시간에 의존하지 않는다(flushWithin으로 워커 완료를 기다린 뒤 검사).
// 테스트 4만 하트비트를 실제로 돌려야 해서 시간 기반이고, 여유 있는 범위로 검사한다.
//
// 빌드: CMake 타깃 test_event_logger
//       또는 g++ -std=c++17 test_event_logger.cpp EventLogger.cpp danger_judgment_engine.cpp \
//                -o test_event_logger -lsqlite3 -pthread
// 실행: ./test_event_logger   (종료코드 0=성공, 1=실패)

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "logging/event_logger.hpp"
#include "network/result_dispatcher.hpp"
#include "logic/judgment/danger_judgment_engine.h"

namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

// ── 임시 DB 경로 ────────────────────────────────────────────
// WAL 모드라 -wal/-shm 부산물이 같이 생긴다. 이전 실행 잔재가 남아 있으면
// 행 수 검사가 통째로 어긋나므로 셋 다 지운다.
std::string tempDbPath(const std::string& tag) {
    auto p = std::filesystem::temp_directory_path() /
             ("veda_event_log_test_" + tag + ".db");
    return p.string();
}

void removeDb(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path + "-wal", ec);
    std::filesystem::remove(path + "-shm", ec);
}

// ── DB 읽기 헬퍼 (검증용, 프로덕션 코드 경로와 독립) ─────────
struct EventRow {
    std::string utc_time;
    bool        camera_id_null = false;
    std::string camera_id;
    int         risk_level = -1;
    bool        prev_null = false;
    int         previous_risk_level = -1;
    std::string exception_state;
    bool        distance_null = false;
    double      distance_m = 0.0;
};

// id 오름차순(= 삽입 순서)으로 전부 읽는다.
std::vector<EventRow> readAll(const std::string& db_path) {
    std::vector<EventRow> rows;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return rows;
    }
    sqlite3_busy_timeout(db, 3000);

    const char* sql =
        "SELECT utc_time, camera_id, risk_level, previous_risk_level, exception_state, distance_m"
        " FROM events ORDER BY id ASC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return rows;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EventRow r;
        const unsigned char* t = sqlite3_column_text(stmt, 0);
        r.utc_time = t ? reinterpret_cast<const char*>(t) : "";

        r.camera_id_null = sqlite3_column_type(stmt, 1) == SQLITE_NULL;
        if (!r.camera_id_null) {
            r.camera_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        }
        r.risk_level = sqlite3_column_int(stmt, 2);

        r.prev_null = sqlite3_column_type(stmt, 3) == SQLITE_NULL;
        if (!r.prev_null) r.previous_risk_level = sqlite3_column_int(stmt, 3);

        const unsigned char* e = sqlite3_column_text(stmt, 4);
        r.exception_state = e ? reinterpret_cast<const char*>(e) : "";

        r.distance_null = sqlite3_column_type(stmt, 5) == SQLITE_NULL;
        if (!r.distance_null) r.distance_m = sqlite3_column_double(stmt, 5);

        rows.push_back(std::move(r));
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rows;
}

// 테이블 스키마를 컬럼명/타입/NOT NULL까지 확인한다 (확정 스펙과의 계약 검사).
std::string readSchema(const std::string& db_path) {
    std::string out;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return out;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(events)", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* name = sqlite3_column_text(stmt, 1);
            const unsigned char* type = sqlite3_column_text(stmt, 2);
            const int notnull = sqlite3_column_int(stmt, 3);
            out += std::string(reinterpret_cast<const char*>(name)) + ":" +
                   reinterpret_cast<const char*>(type) + (notnull ? ":NOT_NULL" : "") + ";";
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return out;
}

// ── 테스트용 판정 결과 ──────────────────────────────────────
// ResultDispatcher::sameState()의 비교 대상은 final_risk / exception / camera_id / zone이다.
// distance_m은 일부러 제외돼 있으므로(거리만 흔들려도 매 프레임 전송되는 걸 막기 위함)
// 거리만 다른 결과는 "변화 없음"으로 취급돼야 한다 - 테스트 3에서 확인한다.
JudgmentResult makeResult(RiskLevel risk, ExceptionState exc = ExceptionState::NONE,
                          double distance_m = 2.0, const std::string& camera_id = "") {
    JudgmentResult r{};
    r.camera_risk = risk;
    r.tof_risk    = risk;
    r.final_risk  = risk;
    r.exception   = exc;
    r.distance_m  = distance_m;
    r.camera_id   = camera_id;
    return r;
}

// ============================================================
// [테스트 1] DB/테이블 자동 생성
// ============================================================
void testCreatesDbAndSchema() {
    std::cout << "[테스트 1] DB 파일/테이블 자동 생성 + 스키마 일치\n";

    const std::string db = tempDbPath("schema");
    removeDb(db);
    check(!std::filesystem::exists(db), "시작 시 DB 파일 없음");

    {
        risk_log::EventLogger logger(db);
        check(logger.start(), "start() 성공 (실패 시: " + logger.lastError() + ")");
        logger.stop();
    }

    check(std::filesystem::exists(db), "start() 후 DB 파일이 생성됨");

    const std::string expected =
        // id는 INTEGER PRIMARY KEY(= rowid 별칭)라 실제로는 NULL이 될 수 없지만,
        // PRAGMA table_info는 notnull=0으로 보고한다(선언에 NOT NULL이 없으므로).
        // 스펙과 다른 게 아니라 SQLite의 보고 방식이라서 기대값을 여기에 맞춘다.
        "id:INTEGER;"
        "utc_time:TEXT:NOT_NULL;"
        "camera_id:TEXT;"
        "risk_level:INTEGER:NOT_NULL;"
        "previous_risk_level:INTEGER;"
        "exception_state:TEXT:NOT_NULL;"
        "distance_m:REAL;";
    const std::string actual = readSchema(db);
    check(actual == expected, "스키마가 확정 스펙과 일치");
    if (actual != expected) {
        std::cout << "    기대: " << expected << "\n    실제: " << actual << "\n";
    }

    removeDb(db);
}

// ============================================================
// [테스트 2] log() 단독 - 값/NULL 규칙
// ============================================================
void testLogWritesValuesAndNulls() {
    std::cout << "\n[테스트 2] log() 단독 - 컬럼 값 + NULL 규칙\n";

    const std::string db = tempDbPath("values");
    removeDb(db);

    risk_log::EventLogger logger(db);
    if (!logger.start()) { check(false, "start() 성공"); return; }

    // (1) 전부 값이 있는 경우
    logger.log(makeResult(RiskLevel::CAUTION, ExceptionState::NONE, 2.75, "cam_01"), 0);
    // (2) 최초 이벤트 - previous_risk_level 없음
    logger.log(makeResult(RiskLevel::DANGER, ExceptionState::SENSOR_FAULT, 1.25, "cam_02"),
               risk_log::EventLogger::kNoPreviousRisk);
    // (3) camera_id 미연결(빈 문자열) + 거리 판정 불가(-1)
    logger.log(makeResult(RiskLevel::EMERGENCY, ExceptionState::UNCONFIRMED_PROXIMITY, -1.0, ""), 2);

    check(logger.flushWithin(std::chrono::seconds(3)), "워커가 3초 안에 큐를 비움");
    logger.stop();

    auto rows = readAll(db);
    check(rows.size() == 3, "3행 저장 (실제: " + std::to_string(rows.size()) + ")");
    check(logger.writtenCount() == 3,
          "writtenCount() == 3 (실제: " + std::to_string(logger.writtenCount()) + ")");
    check(logger.writeFailureCount() == 0 && logger.droppedCount() == 0, "유실·드랍 없음");

    if (rows.size() == 3) {
        check(!rows[0].utc_time.empty() && rows[0].utc_time.back() == 'Z',
              "utc_time이 ISO8601 UTC 형식으로 기록됨 (" + rows[0].utc_time + ")");

        check(rows[0].risk_level == 1 && !rows[0].prev_null && rows[0].previous_risk_level == 0,
              "1행: risk_level=1, previous_risk_level=0");
        check(!rows[0].camera_id_null && rows[0].camera_id == "cam_01", "1행: camera_id=cam_01");
        check(rows[0].exception_state == "NONE", "1행: exception_state=NONE");
        check(!rows[0].distance_null && rows[0].distance_m > 2.74 && rows[0].distance_m < 2.76,
              "1행: distance_m=2.75");

        check(rows[1].risk_level == 2 && rows[1].prev_null,
              "2행: 최초 이벤트라 previous_risk_level이 NULL");
        check(rows[1].exception_state == "SENSOR_FAULT", "2행: exception_state=SENSOR_FAULT");

        check(rows[2].risk_level == 3 && !rows[2].prev_null && rows[2].previous_risk_level == 2,
              "3행: risk_level=3(EMERGENCY), previous_risk_level=2");
        check(rows[2].camera_id_null, "3행: camera_id 빈 문자열 -> NULL");
        check(rows[2].distance_null, "3행: distance_m -1(판정 불가) -> NULL");
        check(rows[2].exception_state == "UNCONFIRMED_PROXIMITY",
              "3행: exception_state=UNCONFIRMED_PROXIMITY");
    }

    removeDb(db);
}

// ============================================================
// [테스트 3] ResultDispatcher 연동 - 변화가 있을 때만 행이 쌓이는지  <- 핵심
// ============================================================
void testOnlyLogsOnStateChange() {
    std::cout << "\n[테스트 3] Dispatcher 연동 - 변화가 있을 때만 기록 + previous_risk_level 체인\n";

    const std::string db = tempDbPath("change");
    removeDb(db);

    risk_log::EventLogger logger(db);
    if (!logger.start()) { check(false, "start() 성공"); return; }

    // 네트워크 전송 쪽은 소켓 없이 건수만 센다 (전송 로직은 이 테스트의 대상이 아니다).
    std::size_t sent = 0;
    std::mutex sent_mtx;
    risk_transport::ResultDispatcher dispatcher(
        [&](const std::string&) { std::lock_guard<std::mutex> lk(sent_mtx); ++sent; },
        std::chrono::milliseconds(200));
    dispatcher.onStateChangeEvent(
        [&logger](const JudgmentResult& r, int prev) { logger.log(r, prev); });
    // start() 하지 않는다 -> 하트비트 스레드 없이 submit() 경로만 검증(결정적).

    // 같은 값 반복 + 거리만 다른 값 섞기.
    dispatcher.submit(makeResult(RiskLevel::SAFE));                                  // 1) 최초 -> 기록
    dispatcher.submit(makeResult(RiskLevel::SAFE));                                  // 2) 동일 -> 기록 X
    dispatcher.submit(makeResult(RiskLevel::SAFE));                                  // 3) 동일 -> 기록 X
    dispatcher.submit(makeResult(RiskLevel::SAFE, ExceptionState::NONE, 2.9));       // 4) 거리만 변함 -> 기록 X
    dispatcher.submit(makeResult(RiskLevel::CAUTION, ExceptionState::NONE, 2.9));    // 5) 위험도 변화 -> 기록
    dispatcher.submit(makeResult(RiskLevel::CAUTION));                               // 6) 동일 -> 기록 X
    dispatcher.submit(makeResult(RiskLevel::DANGER, ExceptionState::NONE, 1.2));     // 7) 위험도 변화 -> 기록
    dispatcher.submit(makeResult(RiskLevel::DANGER, ExceptionState::NONE, 1.1));     // 8) 거리만 변함 -> 기록 X
    dispatcher.submit(makeResult(RiskLevel::DANGER, ExceptionState::EMERGENCY_IMPACT, 1.1)); // 9) 예외 변화 -> 기록
    dispatcher.submit(makeResult(RiskLevel::SAFE, ExceptionState::EMERGENCY_IMPACT, 8.0));   // 10) 위험도 변화 -> 기록

    check(logger.flushWithin(std::chrono::seconds(3)), "워커가 3초 안에 큐를 비움");
    logger.stop();

    auto rows = readAll(db);
    check(rows.size() == 5,
          "submit 10회 중 상태 변화 5회만 기록 (실제: " + std::to_string(rows.size()) + "행)");
    check(sent == 5, "네트워크 전송도 같은 5회 (실제: " + std::to_string(sent) + ")");
    check(dispatcher.changeSendCount() == 5,
          "changeSendCount() == 5 (실제: " + std::to_string(dispatcher.changeSendCount()) + ")");

    // previous_risk_level 체인: NULL -> 0 -> 1 -> 2 -> 2
    //   4행(예외만 변한 이벤트)의 직전 위험도는 그대로 2여야 한다. "이벤트가 있었다 = 위험도가
    //   바뀌었다"가 아니라는 걸 확인하는 지점이다.
    if (rows.size() == 5) {
        check(rows[0].risk_level == 0 && rows[0].prev_null,
              "1행: SAFE, previous_risk_level NULL (최초)");
        check(rows[1].risk_level == 1 && !rows[1].prev_null && rows[1].previous_risk_level == 0,
              "2행: CAUTION, previous=0(SAFE)");
        check(rows[2].risk_level == 2 && !rows[2].prev_null && rows[2].previous_risk_level == 1,
              "3행: DANGER, previous=1(CAUTION)");
        check(rows[3].risk_level == 2 && !rows[3].prev_null && rows[3].previous_risk_level == 2 &&
                  rows[3].exception_state == "EMERGENCY_IMPACT",
              "4행: 예외만 변한 이벤트 - risk_level=2 유지, previous=2");
        check(rows[4].risk_level == 0 && !rows[4].prev_null && rows[4].previous_risk_level == 2,
              "5행: SAFE로 복귀, previous=2(DANGER)");

        std::cout << "  --- 저장된 이벤트 ---\n";
        for (const auto& r : rows) {
            std::cout << "  | " << r.utc_time << "  risk=" << r.risk_level
                      << "  prev=" << (r.prev_null ? std::string("NULL")
                                                   : std::to_string(r.previous_risk_level))
                      << "  exc=" << r.exception_state << "\n";
        }
    }

    removeDb(db);
}

// ============================================================
// [테스트 4] 하트비트 재전송은 로그를 남기지 않는다
// ============================================================
void testHeartbeatDoesNotLog() {
    std::cout << "\n[테스트 4] 하트비트 재전송은 이벤트 로그를 남기지 않음\n";

    const std::string db = tempDbPath("heartbeat");
    removeDb(db);

    risk_log::EventLogger logger(db);
    if (!logger.start()) { check(false, "start() 성공"); return; }

    std::size_t sent = 0;
    std::mutex sent_mtx;
    risk_transport::ResultDispatcher dispatcher(
        [&](const std::string&) { std::lock_guard<std::mutex> lk(sent_mtx); ++sent; },
        std::chrono::milliseconds(50));   // 짧은 주기로 하트비트를 여러 번 돌린다
    dispatcher.onStateChangeEvent(
        [&logger](const JudgmentResult& r, int prev) { logger.log(r, prev); });
    dispatcher.start();

    dispatcher.submit(makeResult(RiskLevel::DANGER, ExceptionState::NONE, 1.2));
    std::this_thread::sleep_for(std::chrono::milliseconds(400));   // 50ms 주기 -> 하트비트 여러 회
    dispatcher.stop();

    check(logger.flushWithin(std::chrono::seconds(3)), "워커가 3초 안에 큐를 비움");
    logger.stop();

    const std::size_t hb = dispatcher.heartbeatSendCount();
    auto rows = readAll(db);

    check(hb >= 3, "하트비트가 실제로 여러 번 나감 (실제: " + std::to_string(hb) + "회)");
    check(sent == 1 + hb,
          "전송은 즉시 1건 + 하트비트 " + std::to_string(hb) + "건 (실제: " +
          std::to_string(sent) + "건)");
    check(rows.size() == 1,
          "이벤트 로그는 변화 1건만 (실제: " + std::to_string(rows.size()) +
          "행 / 하트비트까지 남았다면 " + std::to_string(1 + hb) + "행이 됐을 것)");

    removeDb(db);
}

// ============================================================
// [테스트 5] 백프레셔 - 오래된 것부터 드랍
// ============================================================
void testBackpressureDropsOldest() {
    std::cout << "\n[테스트 5] 백프레셔 - 큐 초과 시 가장 오래된 이벤트부터 드랍\n";

    const std::string db = tempDbPath("backpressure");
    removeDb(db);

    const std::size_t kCapacity = 10;
    const int kCount = 50;

    risk_log::EventLogger logger(db, kCapacity);

    // start() 전에 몰아넣는다 -> 워커가 아직 없어 큐가 확실히 넘치므로 결과가 결정적이다
    // (ResultPublisher 테스트가 쓰는 것과 같은 방식).
    std::ostringstream captured;
    std::streambuf* saved = std::cerr.rdbuf(captured.rdbuf());
    for (int i = 0; i < kCount; ++i) {
        // risk_level 자리에 순번을 넣을 수는 없으므로(0~3만 유효) camera_id로 순번을 식별한다.
        logger.log(makeResult(RiskLevel::SAFE, ExceptionState::NONE, 2.0,
                              "seq_" + std::to_string(i)),
                   risk_transport::ResultDispatcher::kNoPreviousRisk);
    }
    std::cerr.rdbuf(saved);

    check(logger.droppedCount() == static_cast<std::size_t>(kCount) - kCapacity,
          "드랍 카운터 " + std::to_string(kCount - static_cast<int>(kCapacity)) +
          " (실제: " + std::to_string(logger.droppedCount()) + ")");
    check(captured.str().find("queue full") != std::string::npos,
          "첫 드랍 시 stderr에 백프레셔 로그가 남음");

    if (!logger.start()) { check(false, "start() 성공"); return; }
    check(logger.flushWithin(std::chrono::seconds(3)), "워커가 3초 안에 큐를 비움");
    logger.stop();

    auto rows = readAll(db);
    check(rows.size() == kCapacity,
          "남은 " + std::to_string(kCapacity) + "건만 저장 (실제: " +
          std::to_string(rows.size()) + ")");
    check(!rows.empty() && rows.front().camera_id == "seq_" + std::to_string(kCount - static_cast<int>(kCapacity)),
          "가장 오래된 것부터 버려져 seq_" + std::to_string(kCount - static_cast<int>(kCapacity)) +
          "부터 남음 (실제 첫 행: " + (rows.empty() ? std::string("없음") : rows.front().camera_id) + ")");
    check(!rows.empty() && rows.back().camera_id == "seq_" + std::to_string(kCount - 1),
          "마지막 행은 seq_" + std::to_string(kCount - 1) + " (최신 이벤트는 보존)");

    std::cout << "  --- 실제 stderr 출력 ---\n";
    std::istringstream lines(captured.str());
    std::string line;
    while (std::getline(lines, line)) std::cout << "  | " << line << "\n";

    removeDb(db);
}

// ============================================================
// [테스트 6] 재실행 시 기존 DB에 이어붙이기
// ============================================================
void testReopenAppends() {
    std::cout << "\n[테스트 6] 재실행 - 기존 DB/테이블 재사용 후 이어붙이기\n";

    const std::string db = tempDbPath("reopen");
    removeDb(db);

    {
        risk_log::EventLogger logger(db);
        if (!logger.start()) { check(false, "1차 start() 성공"); return; }
        logger.log(makeResult(RiskLevel::SAFE), risk_log::EventLogger::kNoPreviousRisk);
        logger.log(makeResult(RiskLevel::DANGER), 0);
        logger.flushWithin(std::chrono::seconds(3));
        logger.stop();
    }
    check(readAll(db).size() == 2, "1차 실행에서 2행");

    {
        // 같은 경로로 다시 연다 - CREATE TABLE IF NOT EXISTS라 기존 테이블을 그대로 쓴다.
        risk_log::EventLogger logger(db);
        check(logger.start(), "2차 start() 성공 (기존 파일 재사용)");
        logger.log(makeResult(RiskLevel::SAFE), 2);
        logger.flushWithin(std::chrono::seconds(3));
        logger.stop();
    }

    auto rows = readAll(db);
    check(rows.size() == 3,
          "2차 실행 뒤 총 3행 (기존 2행 보존 + 1행 추가) (실제: " +
          std::to_string(rows.size()) + ")");
    check(rows.size() == 3 && rows[2].risk_level == 0 && rows[2].previous_risk_level == 2,
          "이어붙인 행의 값이 정확 (risk=0, previous=2)");

    removeDb(db);
}

} // namespace

int main() {
    std::cout << "=== EventLogger(SQLite 이벤트 로그) 테스트 ===\n\n";

    testCreatesDbAndSchema();
    testLogWritesValuesAndNulls();
    testOnlyLogsOnStateChange();
    testHeartbeatDoesNotLog();
    testBackpressureDropsOldest();
    testReopenAppends();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
