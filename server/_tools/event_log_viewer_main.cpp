#include <sqlite3.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "logic/judgment/danger_judgment_engine.h"
#include "logging/event_logger.hpp"

int main(int argc, char** argv) {
    const int limit = argc > 1 ? std::atoi(argv[1]) : 20;
    const std::string db_path = argc > 2 ? argv[2] : risk_log::EventLogger::kDefaultDbPath;
    if (limit <= 0 || argc > 3) {
        std::cerr << "usage: event_log_viewer [N] [db_path]\n";
        return 1;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "cannot open database: " << db_path << "\n";
        sqlite3_close(db);
        return 1;
    }
    const std::string sql =
        "SELECT utc_time, camera_id, risk_level, exception_state, distance_m FROM "
        + std::string(risk_log::EventLogger::kTableName) + " ORDER BY id DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "cannot query event log: " << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db);
        return 1;
    }
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::cout << reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) << " "
                  << sqlite3_column_int(stmt, 1) << " "
                  << toString(static_cast<RiskLevel>(sqlite3_column_int(stmt, 2))) << " "
                  << reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)) << " "
                  << sqlite3_column_double(stmt, 4) << "\n";
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return 0;
}
