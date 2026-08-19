// EventLogger.cpp
// 판정 이벤트 SQLite 로거 구현
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 인터페이스와 설계 근거는 EventLogger.h 주석 참고.
//
// 빌드 의존성: SQLite3 (C API). 설치 방법은 CMakeLists.txt 상단 주석 참고.
//   CMake: find_package(SQLite3 REQUIRED) -> SQLite::SQLite3
//   직접:  g++ -std=c++17 EventLogger.cpp danger_judgment_engine.cpp ... -lsqlite3 -pthread

#include "logging/event_logger.hpp"
#include "logging/logger.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <iostream>
#include <utility>

namespace risk_log {

namespace {

// 스키마. 컬럼 구성은 팀 확정 스펙 그대로다.
// - previous_risk_level만 NULL 허용: 최초 이벤트에는 직전 상태가 존재하지 않는다.
// - camera_id / terminal_id / distance_mm도 NULL 허용: 상류 미연결(camera_id/terminal_id) 및
//   거리 판정 불가(폐색/사람 미검출 -> distance_mm = -1) 상황을 "값 없음"으로 구분해서 남기기
//   위함. toJson()이 같은 상황을 JSON null로 내보내는 것과 규칙을 맞췄다.
// - terminal_id는 CREATE TABLE IF NOT EXISTS라서 기존 events.db 파일에는 자동으로 붙지
//   않는다 -> start()가 ensureTerminalIdColumn()으로 별도 보강한다.
constexpr const char* kCreateTableSql =
    "CREATE TABLE IF NOT EXISTS events ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  utc_time TEXT NOT NULL,"
    "  camera_id TEXT,"
    "  stream_id TEXT,"
    "  channel INTEGER,"
    "  terminal_id TEXT,"
    "  risk_level INTEGER NOT NULL,"
    "  previous_risk_level INTEGER,"
    "  exception_state TEXT NOT NULL,"
    "  distance_mm REAL"
    ")";

constexpr const char* kInsertSql =
    "INSERT INTO events"
    " (utc_time, camera_id, stream_id, channel, terminal_id, risk_level, previous_risk_level, exception_state, distance_mm)"
    " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";

} // namespace

EventLogger::EventLogger(std::string db_path, std::size_t max_queue_size)
    : db_path_(std::move(db_path)),
      // 큐 길이 0은 log()가 넣자마자 버리는 상태라 사실상 로거를 끄는 것과 같다.
      // 설정 실수로 로그가 통째로 사라지는 걸 막으려고 최소 1로 올린다.
      max_queue_size_(max_queue_size == 0 ? 1 : max_queue_size) {}

EventLogger::~EventLogger() { stop(); }

bool EventLogger::start() {
    if (running_.load()) return true;

    if (!openDatabase()) return false;
    if (!exec(kCreateTableSql))       { closeDatabase(); return false; }
    if (!ensureTerminalIdColumn())    { closeDatabase(); return false; }
    if (!ensureDistanceMmColumn())    { closeDatabase(); return false; }
    if (!ensureSourceColumns())       { closeDatabase(); return false; }
    if (!prepareStatement())          { closeDatabase(); return false; }

    running_.store(true);
    worker_ = std::thread(&EventLogger::run, this);
    LOG_INFO("STORAGE", "위험 이벤트 DB 연결 완료 (" + db_path_ + ")");
    return true;
}

void EventLogger::stop() {
    if (running_.exchange(false)) {
        cv_.notify_all();
        // 워커는 running_이 내려가도 큐를 끝까지 비우고 나서 빠져나온다
        // (종료 직전 이벤트 유실 방지 - run() 주석 참고).
        if (worker_.joinable()) worker_.join();
        closeDatabase();
    }
    flushDropLogSummary();
}

void EventLogger::log(const JudgmentResult& r, int previous_risk_level) {
    Row row;
    row.utc_time            = nowIso8601Ms();   // 쓰기 시점이 아니라 발생 시점
    row.camera_id           = r.source_camera_id.empty() ? r.camera_id : r.source_camera_id;
    row.stream_id           = r.stream_id;
    row.channel             = r.channel;
    row.terminal_id         = r.terminal_id;
    row.risk_level          = static_cast<int>(r.final_risk);
    row.previous_risk_level = previous_risk_level;
    row.exception_state     = toString(r.exception);
    row.distance_mm          = r.distance_mm;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (queue_.size() >= max_queue_size_) {
            queue_.pop_front();
            // 카운터는 매 드랍마다 증가시키고(droppedCount()는 항상 정확), stderr만 rate limit한다.
            std::size_t total = ++dropped_overflow_;
            if (total == 1) {
                std::cerr << "[EventLogger] queue full (max=" << max_queue_size_
                          << ") - 가장 오래된 이벤트 1건 드랍 (누적 드랍=" << total << ")\n";
                last_logged_drop_total_ = total;
            } else if (total - last_logged_drop_total_ >= drop_log_interval_) {
                std::cerr << "[EventLogger] dropped " << (total - last_logged_drop_total_)
                          << " more (total: " << total << ")\n";
                last_logged_drop_total_ = total;
            }
        }
        queue_.push_back(std::move(row));
    }
    cv_.notify_one();
}

bool EventLogger::flushWithin(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mtx_);
    return drained_cv_.wait_for(lk, timeout, [this] { return queue_.empty() && !writing_; });
}

std::string EventLogger::lastError() const {
    std::lock_guard<std::mutex> lk(err_mtx_);
    return last_error_;
}

// ============================================================
// DB 준비 / 정리
// ============================================================

bool EventLogger::openDatabase() {
    // 기본 경로가 storage/events.db라 저장소 루트에서 실행하면
    // 상위 디렉터리가 없을 수 있다. "없으면 자동 생성"에 디렉터리까지 포함시킨다.
    std::error_code ec;
    const auto parent = std::filesystem::path(db_path_).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            setError("DB 디렉터리 생성 실패: " + parent.string() + " (" + ec.message() + ")");
            return false;
        }
    }

    int rc = sqlite3_open_v2(db_path_.c_str(), &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        // open 실패여도 db_ 핸들이 잡혀 있을 수 있어(문서상 보장) 메시지를 읽은 뒤 닫는다.
        setError(std::string("DB 열기 실패: ") + (db_ ? sqlite3_errmsg(db_) : "unknown"));
        closeDatabase();
        return false;
    }

    // WAL: 엔진이 쓰는 동안 event_log_viewer가 같은 파일을 읽어도 서로 막지 않게 한다
    //      (기본 rollback journal이면 조회 중 INSERT가 SQLITE_BUSY로 튄다).
    // synchronous=NORMAL: WAL에서는 커밋마다 fsync하지 않아 쓰기 지연이 크게 줄어든다.
    //      전원이 갑자기 끊기면 마지막 몇 건을 잃을 수 있는데, 이 로그는 사후 분석용이라
    //      판정 루프 지연을 늘리면서까지 FULL로 둘 이유가 없다고 판단했다.
    // busy_timeout: 뷰어와 겹칠 때 즉시 실패하지 않고 잠깐 기다렸다 재시도한다.
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA busy_timeout=3000");
    return true;
}

bool EventLogger::ensureTerminalIdColumn() {
    // PRAGMA table_info는 결과 집합을 돌려주는 쿼리라 sqlite3_exec의 콜백으로는 다루기
    // 번거로워서 prepare/step으로 직접 훑는다. 컬럼명은 1번 인덱스("name")에 있다.
    sqlite3_stmt* pragma_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(events)", -1, &pragma_stmt, nullptr) != SQLITE_OK) {
        setError(std::string("스키마 조회 실패: ") + sqlite3_errmsg(db_));
        return false;
    }

    bool has_terminal_id = false;
    while (sqlite3_step(pragma_stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(pragma_stmt, 1);
        if (name && std::string(reinterpret_cast<const char*>(name)) == "terminal_id") {
            has_terminal_id = true;
            break;
        }
    }
    sqlite3_finalize(pragma_stmt);

    if (has_terminal_id) return true;

    // 기존 events.db 파일(CREATE TABLE IF NOT EXISTS 시점에 이미 존재)에는 컬럼이
    // 자동으로 붙지 않으므로 여기서 한 번 보강한다. 새로 만든 DB는 CREATE TABLE에
    // 이미 terminal_id가 포함돼 있어 이 분기를 타지 않는다.
    std::cerr << "[EventLogger] 기존 events.db에 terminal_id 컬럼이 없어 ALTER TABLE로 추가합니다\n";
    if (!exec("ALTER TABLE events ADD COLUMN terminal_id TEXT")) return false;
    return true;
}

bool EventLogger::ensureDistanceMmColumn() {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(events)", -1, &statement, nullptr) != SQLITE_OK) {
        setError(std::string("스키마 조회 실패: ") + sqlite3_errmsg(db_));
        return false;
    }
    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(statement, 1);
        if (name && std::string(reinterpret_cast<const char*>(name)) == "distance_mm") {
            found = true;
            break;
        }
    }
    sqlite3_finalize(statement);
    if (found) return true;
    std::cerr << "[EventLogger] 기존 events.db에 distance_mm 컬럼을 추가합니다\n";
    return exec("ALTER TABLE events ADD COLUMN distance_mm REAL");
}

bool EventLogger::ensureSourceColumns() {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(events)", -1, &statement, nullptr) != SQLITE_OK) {
        setError(std::string("스키마 조회 실패: ") + sqlite3_errmsg(db_));
        return false;
    }
    bool has_stream = false;
    bool has_channel = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(statement, 1);
        if (!name) continue;
        const std::string column(reinterpret_cast<const char*>(name));
        has_stream = has_stream || column == "stream_id";
        has_channel = has_channel || column == "channel";
    }
    sqlite3_finalize(statement);
    if (!has_stream && !exec("ALTER TABLE events ADD COLUMN stream_id TEXT")) return false;
    if (!has_channel && !exec("ALTER TABLE events ADD COLUMN channel INTEGER")) return false;
    return true;
}

bool EventLogger::prepareStatement() {
    // 매 INSERT마다 SQL을 파싱하지 않도록 한 번만 준비해서 재사용한다.
    if (sqlite3_prepare_v2(db_, kInsertSql, -1, &stmt_, nullptr) != SQLITE_OK) {
        setError(std::string("INSERT 문 준비 실패: ") + sqlite3_errmsg(db_));
        stmt_ = nullptr;
        return false;
    }
    return true;
}

bool EventLogger::exec(const char* sql) {
    if (!db_) return false;
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        setError(std::string("SQL 실패 [") + sql + "]: " + (err ? err : "unknown"));
        sqlite3_free(err);
        return false;
    }
    return true;
}

void EventLogger::closeDatabase() {
    if (stmt_) { sqlite3_finalize(stmt_); stmt_ = nullptr; }
    if (db_)   { sqlite3_close(db_);      db_   = nullptr; }
}

// ============================================================
// 워커 스레드
// ============================================================

void EventLogger::run() {
    for (;;) {
        std::vector<Row> batch;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return !queue_.empty() || !running_.load(); });

            // running_이 내려가도 큐가 남아 있으면 계속 쓴다. 여기서 바로 빠져나오면
            // stop() 직전에 들어온 이벤트(대개 마지막 위험 상태)가 통째로 사라진다.
            if (queue_.empty()) break;

            // 밀려 있던 건 한 번에 꺼내 트랜잭션 하나로 쓴다. 건별 커밋이면 건마다
            // WAL 동기화 비용이 붙어서, 백로그가 생겼을 때 회복이 느려진다.
            batch.assign(std::make_move_iterator(queue_.begin()),
                         std::make_move_iterator(queue_.end()));
            queue_.clear();
            writing_ = true;
        }

        writeBatch(batch);

        {
            std::lock_guard<std::mutex> lk(mtx_);
            writing_ = false;
        }
        drained_cv_.notify_all();
    }
}

void EventLogger::writeBatch(const std::vector<Row>& rows) {
    if (rows.empty() || !stmt_) return;

    // BEGIN이 실패하면(예: DB 잠김) 트랜잭션 없이라도 쓴다 - 로그 유실보다 느린 쪽이 낫다.
    const bool in_txn = exec("BEGIN IMMEDIATE");

    std::size_t ok_count = 0;
    for (const auto& row : rows) {
        if (insertRow(row)) {
            ++ok_count;
        } else {
            std::size_t total = ++write_failures_;
            if (total == 1 || total - last_logged_failure_total_ >= drop_log_interval_) {
                std::cerr << "[EventLogger] INSERT 실패로 이벤트 유실 (누적 실패=" << total
                          << ") - " << lastError() << "\n";
                last_logged_failure_total_ = total;
            }
        }
    }

    if (in_txn && !exec("COMMIT")) {
        // 커밋이 깨지면 이 배치는 통째로 롤백된다 -> 성공 카운트에 넣으면 안 된다.
        exec("ROLLBACK");
        write_failures_ += ok_count;
        std::cerr << "[EventLogger] COMMIT 실패 - 배치 " << ok_count
                  << "건 롤백 (" << lastError() << ")\n";
        return;
    }
    written_ += ok_count;
}

bool EventLogger::insertRow(const Row& row) {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);

    // 문자열은 SQLITE_TRANSIENT로 복사시킨다. row가 배치 벡터에 살아 있긴 하지만,
    // 수명 가정이 깨지면 조용히 쓰레기 값이 들어가는 종류의 버그라 복사 비용을 택했다.
    sqlite3_bind_text(stmt_, 1, row.utc_time.c_str(), -1, SQLITE_TRANSIENT);

    // camera_id는 상류 배선 전까지 빈 문자열로 들어온다. ""와 "값 없음"을 조회 쪽에서
    // 구분할 수 있도록 빈 값은 NULL로 통일한다(toJsonOrNull()과 같은 규칙).
    if (row.camera_id.empty()) {
        sqlite3_bind_null(stmt_, 2);
    } else {
        sqlite3_bind_text(stmt_, 2, row.camera_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    if (row.stream_id.empty()) sqlite3_bind_null(stmt_, 3);
    else sqlite3_bind_text(stmt_, 3, row.stream_id.c_str(), -1, SQLITE_TRANSIENT);
    if (row.channel < 0) sqlite3_bind_null(stmt_, 4);
    else sqlite3_bind_int(stmt_, 4, row.channel);

    // terminal_id도 camera_id와 같은 규칙: 빈 값은 NULL.
    if (row.terminal_id.empty()) {
        sqlite3_bind_null(stmt_, 5);
    } else {
        sqlite3_bind_text(stmt_, 5, row.terminal_id.c_str(), -1, SQLITE_TRANSIENT);
    }

    sqlite3_bind_int(stmt_, 6, row.risk_level);

    // 최초 이벤트(직전 상태 없음)는 NULL. 0(SAFE)과 반드시 구분돼야 한다.
    if (row.previous_risk_level < 0) {
        sqlite3_bind_null(stmt_, 7);
    } else {
        sqlite3_bind_int(stmt_, 7, row.previous_risk_level);
    }

    sqlite3_bind_text(stmt_, 8, row.exception_state.c_str(), -1, SQLITE_TRANSIENT);

    // distance_mm의 -1은 "거리 판정 불가" sentinel이지 측정값이 아니다 -> NULL.
    if (row.distance_mm < 0) {
        sqlite3_bind_null(stmt_, 9);
    } else {
        sqlite3_bind_double(stmt_, 9, row.distance_mm);
    }

    if (sqlite3_step(stmt_) != SQLITE_DONE) {
        setError(std::string("INSERT 실패: ") + sqlite3_errmsg(db_));
        return false;
    }
    return true;
}

// ============================================================
// 보조
// ============================================================

void EventLogger::flushDropLogSummary() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::size_t total = dropped_overflow_.load();
    if (total > last_logged_drop_total_) {
        std::cerr << "[EventLogger] dropped " << (total - last_logged_drop_total_)
                  << " more (total: " << total << ") - 종료 시 잔여 요약\n";
        last_logged_drop_total_ = total;
    }
}

void EventLogger::setError(const std::string& what) {
    {
        std::lock_guard<std::mutex> lk(err_mtx_);
        last_error_ = what;
    }
    // start() 실패는 조용히 넘어가면 "로그가 왜 안 쌓이지"로 이어지므로 항상 한 줄 남긴다.
    // (반복되는 INSERT 실패 로그는 writeBatch() 쪽에서 따로 rate limit한다.)
    if (!running_.load()) std::cerr << "[EventLogger] " << what << "\n";
}

} // namespace risk_log
