// LatencyLogger.h
// 서버 내부 판정 지연(LatencyStamps) CSV 로거 - 공개 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// EventLogger(EventLogger.h)와 정책을 맞춘 큐+워커스레드 로거다:
//   - log()는 큐에 넣고 즉시 반환한다. 실제 파일 쓰기는 워커 스레드가 담당해서
//     ResultDispatcher::submit()(판정 루프에서 호출)이 파일 I/O로 블로킹되지 않는다.
//   - 큐가 가득 차면 가장 오래된 항목부터 버린다(백프레셔 정책도 EventLogger와 동일).
//
// 무엇을 남기는가: ResultDispatcher::submit()이 "상태 변화"로 판단해 실제로 전송한
// 프레임만 들어온다(하트비트 재전송 제외) - ResultDispatcher.h의 latency_sink_ 훅 참고.
//
// ON/OFF: SERVER_LATENCY_INSTRUMENTATION 매크로(latency_stamps.h)가 0이면 start()/log()가
// 아무 것도 하지 않는다 - 파일도 워커 스레드도 생기지 않는다. 이유는 latency_stamps.h 주석 참고.
//
// 구현은 LatencyLogger.cpp에 있다.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "latency_stamps.h"

namespace risk_log {

class LatencyLogger {
public:
    // CSV 파일 기본 경로. EventLogger::kDefaultDbPath와 같은 규약(CWD 기준 상대경로,
    // 저장소 루트에서 실행하는 것을 전제로 한 팀 합의 경로)을 따른다.
    static constexpr const char* kDefaultCsvPath = "server/judgment/latency.csv";

    // csv_path       : CSV 파일 경로 (없으면 생성, 상위 디렉터리도 없으면 만든다)
    // max_queue_size : 쓰기 대기 큐 최대 길이. 초과 시 가장 오래된 항목부터 버린다.
    explicit LatencyLogger(std::string csv_path = kDefaultCsvPath,
                           std::size_t max_queue_size = 200);

    ~LatencyLogger();

    LatencyLogger(const LatencyLogger&) = delete;
    LatencyLogger& operator=(const LatencyLogger&) = delete;

    // 파일을 열고(없으면 헤더를 먼저 쓴다) 워커 스레드를 띄운다.
    // 계측이 꺼져 있으면(SERVER_LATENCY_INSTRUMENTATION=0) 아무 것도 하지 않고 false를 돌려준다.
    // 실패해도 예외를 던지지 않는다 - 로그를 못 남긴다고 판정·송신까지 멈추면 안 되기 때문이다
    // (EventLogger::start()와 같은 원칙).
    bool start();

    // 남은 큐를 끝까지 쓰고 워커를 정리한 뒤 파일을 닫는다.
    void stop();

    // 지연 스탬프 한 건을 기록 큐에 넣는다. 판정 루프(ResultDispatcher::submit())에서
    // 호출되므로 블로킹하지 않는다.
    void log(const LatencyStamps& stamps);

    // 실제로 파일에 쓰인 누적 행 수
    std::size_t writtenCount() const { return written_.load(); }

    // 큐 넘침으로 버려진 누적 건수
    std::size_t droppedCount() const { return dropped_overflow_.load(); }

    // 큐가 비고 진행 중인 쓰기도 끝날 때까지 기다린다 (테스트/종료 확인용).
    bool flushWithin(std::chrono::milliseconds timeout);

    const std::string& csvPath() const { return csv_path_; }

private:
    bool openFile();
    void closeFile();

    void run();
    void writeBatch(const std::vector<LatencyStamps>& rows);

    void flushDropLogSummary();

    std::string csv_path_;
    std::size_t max_queue_size_;

    std::ofstream file_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;         // 워커 깨우기
    std::condition_variable drained_cv_; // flushWithin() 대기용
    std::deque<LatencyStamps> queue_;
    bool writing_ = false;               // 워커가 배치를 쓰는 중 (mtx_ 보호)

    std::atomic<bool> running_{false};
    std::thread worker_;

    std::atomic<std::size_t> written_{0};
    std::atomic<std::size_t> dropped_overflow_{0};

    // 드랍 로그 rate limit 상태 (EventLogger와 같은 이유 - unbuffered stderr에 건건이
    // 찍으면 판정 루프까지 느려진다).
    std::size_t last_logged_drop_total_ = 0;
    std::size_t drop_log_interval_ = 100;
};

} // namespace risk_log
