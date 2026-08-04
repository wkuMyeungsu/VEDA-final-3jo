// event_log_viewer_main.cpp
// 판정 이벤트 로그(SQLite) 조회용 독립 실행파일
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 사용법:
//   ./event_log_viewer [N] [db_path]
//     N        최근 몇 건을 볼지 (기본 20)
//     db_path  DB 파일 경로 (기본 EventLogger::kDefaultDbPath = server/judgment/events.db)
//              기본 경로가 CWD 기준 상대 경로라, 빌드 디렉터리에서 실행할 때 필요하다.
//
// 시간 역순(최신 먼저)으로 출력한다. 정렬 기준은 utc_time이 아니라 id다.
// 같은 밀리초 안에 여러 이벤트가 들어오면 utc_time만으로는 순서가 갈리지 않는데,
// id(AUTOINCREMENT)는 삽입 순서를 그대로 보존한다.
//
// 출력 컬럼은 팀 확정 목록(시각/camera_id/risk_level/prev_risk/exception_state/distance_m) 그대로다.
//
// 이 파일은 읽기 전용이다. 엔진이 돌고 있는 중에 실행해도 되도록 read-only로 열고
// busy_timeout을 준다(EventLogger가 WAL 모드로 쓰기 때문에 서로 막지 않는다).
//
// 빌드: CMake 타깃 event_log_viewer
//       또는 g++ -std=c++17 event_log_viewer_main.cpp danger_judgment_engine.cpp \
//                -o event_log_viewer -lsqlite3

#include <sqlite3.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

#include "EventLogger.h"            // kDefaultDbPath / kTableName 공유
#include "danger_judgment_engine.h" // toString(RiskLevel)

namespace {

constexpr int kDefaultLimit = 20;

// NULL 컬럼 표기. 빈 칸으로 두면 "값이 없음"인지 "칸이 밀렸는지" 구분이 안 된다.
constexpr const char* kNull = "-";

void printUsage(const char* argv0) {
    std::cerr << "사용법: " << argv0 << " [N] [db_path]\n"
              << "  N        최근 조회 건수 (기본 " << kDefaultLimit << ")\n"
              << "  db_path  DB 경로 (기본 " << risk_log::EventLogger::kDefaultDbPath << ")\n";
}

// "2 (DANGER)" 형태. 숫자만 있으면 읽는 사람이 매번 매핑을 떠올려야 하고,
// 이름만 있으면 FPGA/단말 프로토콜 값(0~3)과 대조하기 어렵다.
std::string riskLabel(int level) {
    return std::to_string(level) + " (" + toString(static_cast<RiskLevel>(level)) + ")";
}

// previous_risk_level은 최초 이벤트에서 NULL이라 riskLabel()에 그대로 넘길 수 없다.
std::string riskLabelOrNull(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) return kNull;
    return riskLabel(sqlite3_column_int(stmt, col));
}

// 컬럼 폭. 가장 긴 값 기준으로 잡았다.
//   utc_time        "2026-08-03T10:15:30.123Z" = 24
//   risk_level      "3 (EMERGENCY)"            = 13
//   prev_risk       "3 (EMERGENCY)"            = 13
//   exception_state "UNCONFIRMED_PROXIMITY"    = 21
void printHeader() {
    std::cout << std::left
              << std::setw(24) << "utc_time"      << "  "
              << std::setw(12) << "camera_id"     << "  "
              << std::setw(13) << "risk_level"    << "  "
              << std::setw(13) << "prev_risk"     << "  "
              << std::setw(21) << "exception_state" << "  "
              << std::right << std::setw(10) << "distance_m" << "\n";
    std::cout << std::string(24 + 12 + 13 + 13 + 21 + 10 + 10, '-') << "\n";
}

// sqlite3_column_text()는 NULL 컬럼에 nullptr를 준다.
std::string textOrNull(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) return kNull;
    const unsigned char* v = sqlite3_column_text(stmt, col);
    return v ? reinterpret_cast<const char*>(v) : kNull;
}

int run(int limit, const std::string& db_path) {
    sqlite3* db = nullptr;
    // READONLY: 조회 도구가 실수로 DB를 만들거나(경로 오타 시 빈 파일 생성) 쓰지 않도록.
    int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "DB를 열 수 없습니다: " << db_path << "\n"
                  << "  (" << (db ? sqlite3_errmsg(db) : "unknown") << ")\n"
                  << "  엔진을 한 번도 실행하지 않았다면 파일이 아직 없을 수 있습니다.\n";
        sqlite3_close(db);
        return 1;
    }
    sqlite3_busy_timeout(db, 3000);   // 엔진이 쓰는 중이면 잠깐 기다렸다 재시도

    const std::string sql =
        std::string("SELECT utc_time, camera_id, risk_level, previous_risk_level,"
                    " exception_state, distance_m"
                    " FROM ") + risk_log::EventLogger::kTableName +
        " ORDER BY id DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "조회 실패: " << sqlite3_errmsg(db) << "\n"
                  << "  (테이블 '" << risk_log::EventLogger::kTableName
                  << "'이 아직 없을 수 있습니다)\n";
        sqlite3_close(db);
        return 1;
    }
    sqlite3_bind_int(stmt, 1, limit);

    std::cout << "=== 판정 이벤트 로그 (최근 " << limit << "건, 최신순) - " << db_path << " ===\n\n";
    printHeader();

    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ++rows;
        std::cout << std::left
                  << std::setw(24) << textOrNull(stmt, 0) << "  "
                  << std::setw(12) << textOrNull(stmt, 1) << "  "
                  << std::setw(13) << riskLabel(sqlite3_column_int(stmt, 2)) << "  "
                  << std::setw(13) << riskLabelOrNull(stmt, 3) << "  "
                  << std::setw(21) << textOrNull(stmt, 4) << "  ";

        if (sqlite3_column_type(stmt, 5) == SQLITE_NULL) {
            std::cout << std::right << std::setw(10) << kNull;
        } else {
            std::cout << std::right << std::setw(10) << std::fixed << std::setprecision(2)
                      << sqlite3_column_double(stmt, 5);
        }
        std::cout << "\n";
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    std::cout << "\n" << rows << "건 출력";
    if (rows == 0) std::cout << " (아직 기록된 상태 변화 이벤트가 없습니다)";
    std::cout << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    int limit = kDefaultLimit;
    std::string db_path = risk_log::EventLogger::kDefaultDbPath;

    if (argc > 3) { printUsage(argv[0]); return 1; }

    if (argc >= 2) {
        try {
            std::size_t consumed = 0;
            limit = std::stoi(argv[1], &consumed);
            // "20abc" 같은 입력이 조용히 20으로 통과하지 않게 전부 소비됐는지 확인한다.
            if (consumed != std::string(argv[1]).size() || limit <= 0) throw std::invalid_argument("");
        } catch (const std::exception&) {
            std::cerr << "N은 1 이상의 정수여야 합니다: " << argv[1] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }
    if (argc >= 3) db_path = argv[2];

    return run(limit, db_path);
}
