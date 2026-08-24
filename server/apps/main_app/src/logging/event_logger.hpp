// EventLogger.h
// 판정 이벤트 SQLite 로거 - 공개 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 무엇을 남기는가:
//   "판정 상태가 실제로 바뀐 순간"만 한 행씩 남긴다. 프레임마다 남기지 않는 이유는
//   ResultDispatcher가 이미 sameState()로 변화를 판별하고 있고, 무변화 구간의 하트비트
//   재전송(200ms 주기)까지 로그로 받으면 똑같은 상태가 시간당 수만 건씩 쌓여서
//   나중에 "언제 위험해졌나"를 찾는 데 쓸 수 없는 로그가 되기 때문이다.
//   -> 그래서 log() 호출 판단은 여기서 하지 않고 ResultDispatcher::submit()의
//      변화 감지 지점(EventSink)에서만 들어온다. 이 클래스는 "받은 건 무조건 쓴다".
//
// 왜 별도 스레드인가:
//   SQLite INSERT는 커밋 시 디스크 fsync가 걸려 수 ms~수십 ms까지 튄다. 판정 루프는
//   100ms 목표를 지켜야 하므로 log()는 큐에 넣고 즉시 반환하고, 실제 DB 쓰기는
//   워커 스레드가 담당한다 (ResultPublisher::publish()와 같은 구조).
//
// 백프레셔:
//   ResultPublisher와 동일하게 "큐가 가득 차면 가장 오래된 것부터 드랍 + 드랍 로그
//   rate limit" 정책을 쓴다. 로그 관점에서는 오래된 이벤트를 버리는 게 아깝지만,
//   두 큐가 서로 다른 규칙으로 동작하면 장애 상황 해석이 어려워져서 정책을 통일했다.
//   (애초에 상태 변화 시에만 들어오므로 큐가 찰 일 자체가 정상 운용에서는 없다.
//    큐가 찼다면 디스크 쪽에 문제가 생긴 것이고, 그때는 droppedCount()가 신호가 된다.)
//
// 구현은 EventLogger.cpp, 조회용 실행파일은 event_log_viewer_main.cpp에 있다.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logic/judgment/danger_judgment_engine.h"

// sqlite3.h를 이 헤더에서 include하지 않는 이유: 이 헤더를 쓰는 쪽(엔진 main / 테스트)에
// SQLite 헤더 경로를 강제하지 않으려고 불투명 포인터만 전방 선언한다.
// sqlite3 / sqlite3_stmt는 sqlite3.h에서 struct에 대한 typedef이므로 이 선언과 호환된다.
struct sqlite3;
struct sqlite3_stmt;

namespace risk_log {

class EventLogger {
public:
    // DB 파일 기본 경로. 프로세스의 현재 작업 디렉터리 기준 상대 경로다
    // (저장소 루트에서 실행하는 것을 전제로 한 팀 합의 경로).
    // 빌드 디렉터리 등 다른 위치에서 실행할 때는 생성자에 절대 경로를 넘기면 된다.
    // 상위 디렉터리가 없으면 start()에서 만들어 준다.
    static constexpr const char* kDefaultDbPath = "storage/events.db";

    // 테이블 이름. 조회 쪽(event_log_viewer)이 같은 이름을 써야 하므로 여기서 한 번만 정의한다.
    static constexpr const char* kTableName = "events";

    // "직전 상태 없음"(= 최초 이벤트)을 뜻하는 previous_risk_level 값.
    // 이 값(정확히는 모든 음수)이 들어오면 DB에는 NULL로 저장된다.
    // 0(SAFE)은 유효한 위험도라서 sentinel로 쓸 수 없다 - "직전이 SAFE였다"와
    // "직전이 없다"를 반드시 구분해야 한다.
    static constexpr int kNoPreviousRisk = -1;

    // db_path        : SQLite 파일 경로 (없으면 생성)
    // max_queue_size : 쓰기 대기 큐 최대 길이. 초과 시 가장 오래된 항목부터 버린다.
    //                  200건 = 상태 변화 이벤트 기준으로 한참 여유 있는 버퍼.
    explicit EventLogger(std::string db_path = kDefaultDbPath,
                         std::size_t max_queue_size = 200);

    ~EventLogger();

    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;

    // DB 파일/테이블을 준비하고 워커 스레드를 띄운다.
    // 실패해도 예외를 던지지 않고 false를 돌려준다 - 로그를 못 남긴다고 판정·송신까지
    // 멈추면 안 되기 때문이다. 실패 사유는 lastError()에 남고 stderr에도 한 줄 찍힌다.
    // start()가 실패한 뒤에도 log()는 안전하게 호출할 수 있다(큐에만 쌓이다 드랍된다).
    bool start();

    // 남은 큐를 끝까지 쓰고 워커를 정리한 뒤 DB를 닫는다.
    // 종료 직전의 이벤트(대개 가장 중요한 마지막 위험 상태)를 버리지 않기 위해
    // 중간에 끊지 않고 반드시 flush한다.
    void stop();

    // 상태 변화 이벤트 한 건을 기록 큐에 넣는다. 판정 루프에서 호출되므로 블로킹하지 않는다.
    //
    // previous_risk_level: 직전 상태의 risk_level. 최초 이벤트면 kNoPreviousRisk(음수)를
    //                      넘기고, DB에는 NULL로 저장된다.
    //
    // utc_time은 DB 쓰기 시점이 아니라 이 호출 시점으로 찍는다. 워커가 밀리거나
    // 배치로 몰아 쓰는 경우에도 "이벤트가 발생한 시각"이 보존돼야 하기 때문이다.
    void log(const JudgmentResult& r, int previous_risk_level);

    // 실제로 DB에 커밋된 누적 행 수
    std::size_t writtenCount() const { return written_.load(); }

    // 큐 넘침으로 버려진 누적 건수 (백프레셔 발생 여부 추적용)
    std::size_t droppedCount() const { return dropped_overflow_.load(); }

    // INSERT 실패(디스크 가득참, 파일 권한 등)로 유실된 누적 건수. 큐 넘침과 원인이 달라 따로 센다.
    std::size_t writeFailureCount() const { return write_failures_.load(); }

    // 큐가 비고 진행 중인 쓰기도 끝날 때까지 기다린다 (테스트/종료 확인용).
    // 시간 안에 비면 true. 판정 루프에서는 쓰지 않는다.
    bool flushWithin(std::chrono::milliseconds timeout);

    const std::string& dbPath() const { return db_path_; }

    // 마지막 SQLite 오류 메시지 (없으면 빈 문자열)
    std::string lastError() const;

private:
    // 큐에 담기는 한 행. enum -> 문자열 변환과 시각 찍기를 log() 시점에 끝내 두어
    // 워커는 바인딩만 하면 되게 한다.
    struct Row {
        std::string utc_time;
        std::string camera_id;            // 빈 문자열 -> NULL
        std::string stream_id;            // 실제 RTSP 입력 스트림
        int channel = -1;                 // CCTV 장비 내부 채널
        std::string terminal_id;          // 빈 문자열 -> NULL (camera_id와 동일 규약)
        int         risk_level = 0;
        int         previous_risk_level = kNoPreviousRisk;  // 음수 -> NULL
        std::string exception_state;
        double      distance_mm = -1.0;    // 음수 -> NULL (toJson의 sentinel 규칙과 동일)
        std::string decision_id;
        std::string sensor_message_id;
        std::string sensor_producer_run_id;
        std::int64_t sensor_sequence = -1;
        std::int64_t sensor_ts_ms = 0;
        std::int64_t sensor_age_ms = -1;
    };

    bool openDatabase();
    bool prepareStatement();
    bool exec(const char* sql);
    void closeDatabase();

    // CREATE TABLE IF NOT EXISTS는 이미 존재하는 기존 events.db 파일에는 새 컬럼을
    // 붙여주지 않는다. start() 시점에 PRAGMA table_info로 없는 컬럼을 ALTER TABLE로 보강한다.
    bool ensureSchemaColumns();

    void run();
    void writeBatch(const std::vector<Row>& rows);
    bool insertRow(const Row& row);

    // 종료 시점에 아직 로그로 안 남은 드랍 잔여분을 요약 1줄로 남긴다.
    // 잔여분이 없으면 아무것도 찍지 않으므로 여러 번 불려도 안전하다.
    void flushDropLogSummary();

    void setError(const std::string& what);

    std::string db_path_;
    std::size_t max_queue_size_;

    // db_ / stmt_는 start()/stop() 구간 밖에서는 워커 스레드만 만진다.
    sqlite3*      db_   = nullptr;
    sqlite3_stmt* stmt_ = nullptr;

    mutable std::mutex mtx_;
    std::condition_variable cv_;         // 워커 깨우기
    std::condition_variable drained_cv_; // flushWithin() 대기용
    std::deque<Row> queue_;
    bool writing_ = false;               // 워커가 배치를 쓰는 중 (mtx_ 보호)

    std::atomic<bool> running_{false};
    std::thread worker_;

    std::atomic<std::size_t> written_{0};
    std::atomic<std::size_t> dropped_overflow_{0};
    std::atomic<std::size_t> write_failures_{0};

    // 드랍 로그 rate limit 상태 (mtx_ 보호). 첫 1건은 즉시 찍고, 그 뒤로는
    // 마지막 로그 이후 drop_log_interval_건이 쌓일 때마다 요약 1줄만 찍는다.
    // - unbuffered stderr에 건건이 찍으면 판정 루프까지 느려진다(ResultPublisher와 같은 이유).
    std::size_t last_logged_drop_total_ = 0;
    std::size_t drop_log_interval_ = 100;

    // INSERT 실패 로그도 같은 방식으로 rate limit (워커 스레드에서만 접근).
    std::size_t last_logged_failure_total_ = 0;

    mutable std::mutex err_mtx_;
    std::string last_error_;
};

} // namespace risk_log
