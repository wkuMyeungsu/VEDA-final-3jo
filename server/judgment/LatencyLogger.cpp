// LatencyLogger.cpp
// 서버 내부 판정 지연 CSV 로거 구현
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 인터페이스와 설계 근거는 LatencyLogger.h 주석 참고.

#include "LatencyLogger.h"

#include <filesystem>
#include <iostream>
#include <utility>

namespace risk_log {

LatencyLogger::LatencyLogger(std::string csv_path, std::size_t max_queue_size)
    : csv_path_(std::move(csv_path)),
      // 큐 길이 0은 log()가 넣자마자 버리는 상태라 사실상 로거를 끄는 것과 같다.
      // 설정 실수로 로그가 통째로 사라지는 걸 막으려고 최소 1로 올린다(EventLogger와 동일).
      max_queue_size_(max_queue_size == 0 ? 1 : max_queue_size) {}

LatencyLogger::~LatencyLogger() { stop(); }

bool LatencyLogger::start() {
#if !SERVER_LATENCY_INSTRUMENTATION
    // 계측이 꺼져 있으면 파일도 워커 스레드도 만들지 않는다 (latency_stamps.h 주석 참고).
    return false;
#else
    if (running_.load()) return true;

    if (!openFile()) return false;

    running_.store(true);
    worker_ = std::thread(&LatencyLogger::run, this);
    std::cerr << "[LatencyLogger] 지연 로그 시작 - " << csv_path_ << "\n";
    return true;
#endif
}

void LatencyLogger::stop() {
    if (running_.exchange(false)) {
        cv_.notify_all();
        // 워커는 running_이 내려가도 큐를 끝까지 비우고 나서 빠져나온다
        // (종료 직전 이벤트 유실 방지 - EventLogger::run()과 같은 원칙).
        if (worker_.joinable()) worker_.join();
        closeFile();
    }
    flushDropLogSummary();
}

void LatencyLogger::log(const LatencyStamps& stamps) {
#if !SERVER_LATENCY_INSTRUMENTATION
    (void)stamps;
    return;
#else
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (queue_.size() >= max_queue_size_) {
            queue_.pop_front();
            std::size_t total = ++dropped_overflow_;
            if (total == 1) {
                std::cerr << "[LatencyLogger] queue full (max=" << max_queue_size_
                          << ") - 가장 오래된 항목 1건 드랍 (누적 드랍=" << total << ")\n";
                last_logged_drop_total_ = total;
            } else if (total - last_logged_drop_total_ >= drop_log_interval_) {
                std::cerr << "[LatencyLogger] dropped " << (total - last_logged_drop_total_)
                          << " more (total: " << total << ")\n";
                last_logged_drop_total_ = total;
            }
        }
        queue_.push_back(stamps);
    }
    cv_.notify_one();
#endif
}

bool LatencyLogger::flushWithin(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mtx_);
    return drained_cv_.wait_for(lk, timeout, [this] { return queue_.empty() && !writing_; });
}

// ============================================================
// 파일 준비 / 정리
// ============================================================

bool LatencyLogger::openFile() {
    // 기본 경로가 server/judgment/latency.csv라 저장소 루트가 아닌 곳에서 실행하면
    // 상위 디렉터리가 없을 수 있다. "없으면 자동 생성"에 디렉터리까지 포함시킨다
    // (EventLogger::openDatabase()와 동일한 처리).
    std::error_code ec;
    const auto parent = std::filesystem::path(csv_path_).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        std::filesystem::create_directories(parent, ec);
        if (ec) return false;
    }

    // 파일이 없거나 비어 있으면 헤더를 먼저 쓴다.
    const bool need_header = !std::filesystem::exists(csv_path_, ec) ||
                             std::filesystem::file_size(csv_path_, ec) == 0;

    file_.open(csv_path_, std::ios::out | std::ios::app);
    if (!file_.is_open()) return false;

    if (need_header) {
        file_ << LatencyStamps::csvHeader() << '\n';
        file_.flush();
    }
    return true;
}

void LatencyLogger::closeFile() {
    if (file_.is_open()) file_.close();
}

// ============================================================
// 워커 스레드
// ============================================================

void LatencyLogger::run() {
    for (;;) {
        std::vector<LatencyStamps> batch;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [this] { return !queue_.empty() || !running_.load(); });

            // running_이 내려가도 큐가 남아 있으면 계속 쓴다(EventLogger::run()과 동일 원칙).
            if (queue_.empty()) break;

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

void LatencyLogger::writeBatch(const std::vector<LatencyStamps>& rows) {
    if (rows.empty() || !file_.is_open()) return;

    for (const auto& s : rows) {
        file_ << s.toCsvRow() << '\n';
    }
    file_.flush();
    written_ += rows.size();
}

// ============================================================
// 보조
// ============================================================

void LatencyLogger::flushDropLogSummary() {
    std::lock_guard<std::mutex> lk(mtx_);
    std::size_t total = dropped_overflow_.load();
    if (total > last_logged_drop_total_) {
        std::cerr << "[LatencyLogger] dropped " << (total - last_logged_drop_total_)
                  << " more (total: " << total << ") - 종료 시 잔여 요약\n";
        last_logged_drop_total_ = total;
    }
}

} // namespace risk_log
