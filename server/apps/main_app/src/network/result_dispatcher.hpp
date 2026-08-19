#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "logic/judgment/danger_judgment_engine.h"
#include "common/latency_stamps.hpp"

// ResultDispatcher - 판정 결과 송신 정책 (헤더 온리, 소켓 의존성 없음)
//
// [정책 확정 2026-08-03] 하이브리드 전송
//   - 판정 상태가 이전과 다르면: 즉시 전송하고 하트비트 타이머를 리셋
//   - 200ms 동안 상태 변화가 없으면: 마지막 판정 결과를 재전송 (하트비트 겸용)
//   "무조건 200ms 주기 전송"을 쓰지 않는 이유: 위험 상태 전이가 최대 200ms 밀려
//   판정 지연 100ms 목표와 정면으로 충돌한다. 변화는 즉시 내보내고, 링크 생존 확인과
//   값 refresh만 주기 전송이 맡는다.
//
// 재전송은 백그라운드 스레드가 담당하므로 판정 루프는 submit()에서 블로킹되지 않는다
// (submit()은 짧은 락 구간 + 문자열 직렬화만 하고 I/O는 sink 쪽 큐로 넘긴다).
//
// 전송 자체(소켓)는 여기서 다루지 않는다. sink로 ResultPublisher::publish를 넘기면 되고,
// 테스트에서는 람다를 넣어 소켓 없이 정책만 검증할 수 있다.
//
// [추가 2026-08-03] 이벤트 로그 훅(EventSink)
//   여기가 "상태 변화"를 아는 유일한 지점이라 SQLite 이벤트 로그도 이 판단을 재사용한다.
//   하트비트 재전송 경로(run())에서는 절대 부르지 않는다 - 무변화 재전송까지 로그로 남기면
//   같은 상태가 수천 건씩 중복 저장돼 로그가 쓸모없어진다.
//   로거 구현(sqlite3 의존)은 여기 들어오지 않는다. 콜백 시그니처만 두어
//   이 헤더는 계속 표준 헤더만으로 컴파일된다.
//
// [추가] 지연 계측 훅(LatencySink)
//   서버 내부 지연(t0_ingest~t2_send) 중 t2_send를 채우는 지점도 여기다(submit()에서
//   sink_() 호출 직전). EventSink와 같은 이유로 상태 변화 시에만 호출하고, 로거 구현
//   (파일 I/O 의존)은 이 헤더에 들이지 않는다 - 자세한 내용은 latency_stamps.h 참고.
//
// [추가 2026-08-10] 기동 시 idle 프라이밍(primeIdle())
//   하트비트 루프는 has_last_(= submit()이 최소 1회 불렸는지)가 참일 때만 돈다. 그런데
//   submit()은 ObjectDetection 메타데이터가 들어오는 경로에서만 불리므로, 카메라가 객체를
//   한 번도 못 올리면(사람 미검출/메타데이터 미수신/ArUco만 수신) risk_event 채널이
//   영원히 조용하다 - 단말은 "서버가 죽었는지 아직 안전한지"를 구분할 수 없다.
//   그래서 기동 시 idle 판정(SAFE/NONE/값 없음)으로 last_를 한 번 채워 하트비트가
//   첫 프레임 전부터 돌게 한다. 자세한 규약은 primeIdle() 주석 참고.

namespace risk_transport {

class ResultDispatcher {
public:
    using Sink  = std::function<void(const std::string&)>;
    using Clock = std::chrono::steady_clock;

    // 상태 변화가 실제로 일어났을 때만 호출되는 콜백.
    // prev_risk_level: 직전 상태의 risk_level. 최초 이벤트에는 kNoPreviousRisk(-1)가 들어온다
    //                  (EventLogger::kNoPreviousRisk와 같은 값이며 DB에는 NULL로 저장된다).
    //                  0(SAFE)이 유효한 위험도라서 "직전 없음"을 0으로 표현할 수 없다.
    using EventSink = std::function<void(const JudgmentResult&, int prev_risk_level)>;

    // [추가] 지연 계측 훅(LatencySink).
    // event_sink_와 같은 이유로 여기(상태 변화가 실제로 일어난 지점)에서만 부른다 -
    // 하트비트 재전송까지 남기면 t0/t1이 없는(직전 프레임 값 재사용) 스냅숏이 매 200ms
    // 섞여 들어가 지연 로그를 왜곡한다. r.latency에 t0_ingest/t1_judge_in이 이미 실려
    // 들어온다는 전제이고(비어 있으면(=시각 0) 상류가 아직 안 채운 것), t2_send는
    // submit() 안에서 채운 뒤 이 콜백으로 넘긴다.
    using LatencySink = std::function<void(const LatencyStamps&)>;

    // "직전 상태 없음"(= 최초 이벤트)을 뜻하는 prev_risk_level 값.
    static constexpr int kNoPreviousRisk = -1;

    explicit ResultDispatcher(Sink sink,
                              std::chrono::milliseconds heartbeat_period = std::chrono::milliseconds(200))
        : sink_(std::move(sink)), period_(heartbeat_period) {}

    ~ResultDispatcher() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread(&ResultDispatcher::run, this);
    }

    void stop() {
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    // 판정 루프에서 프레임마다 호출한다.
    // 상태가 바뀐 경우에만 즉시 전송하고, 그렇지 않으면 마지막 결과만 갱신해 둔다
    // (다음 하트비트가 최신 값을 싣고 나간다).
    void submit(const JudgmentResult& r) {
        bool changed = false;
        int  prev_risk = kNoPreviousRisk;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            // last_is_idle_(기동 시 프라이밍 값)이면 내용이 같더라도 "변화"로 친다.
            // idle은 판정이 아니라 자리표시용이라, 이걸 직전 상태로 인정하면 첫 진짜 판정이
            // SAFE일 때 이벤트 로그/지연 계측이 통째로 누락된다(프라이밍 도입 전에는
            // 첫 판정이 항상 기록됐다) - 하류 로그 동작을 그대로 유지하기 위한 예외 처리.
            changed  = !has_last_ || last_is_idle_ || !sameState(last_, r);
            // last_를 덮어쓰기 전에 직전 위험도를 떠 둔다 (이벤트 로그의 previous_risk_level).
            // has_last_가 false면 직전 상태가 없으므로 sentinel을 그대로 둔다 -> DB에 NULL.
            // idle 프라이밍 값도 같은 취급이다("SAFE에서 내려왔다"가 아니라 "직전 판정 없음").
            if (changed && has_last_ && !last_is_idle_) prev_risk = static_cast<int>(last_.final_risk);
            last_    = r;
            has_last_ = true;
            last_is_idle_ = false;
            if (changed) next_heartbeat_ = Clock::now() + period_;   // 변화 시 타이머 리셋
        }
        if (!changed) return;

        ++change_sends_;

        // t2_send: 실제 전송(sink_) 직전. 하트비트 재전송 경로(run())는 이 계측 대상이
        // 아니다(위 latency_sink_ 주석 참고) - 여기 submit()의 "변화 시 즉시 전송" 경로에서만 찍는다.
        LatencyStamps stamps = r.latency;
        stamps.t2_send = LatencyStamps::Clock::now();

        // 전송이 먼저다. 이벤트 로그가 느려지더라도(디스크 등) 단말로 나가는 시각이
        // 밀리지 않게 순서를 고정한다. event_sink_/latency_sink_ 자체도 큐잉만 하고 즉시 반환한다.
        logSend("변화", r);
        sink_(toJson(r));
        if (event_sink_) event_sink_(r, prev_risk);
        if (latency_sink_) latency_sink_(stamps);
        cv_.notify_one();   // 하트비트 스레드가 리셋된 시각 기준으로 다시 자도록 깨운다
    }

    // 상태 변화 이벤트 훅을 등록한다 (예: EventLogger::log).
    // start()/submit() 전에 한 번만 설정하는 것을 전제로 한다 - 동작 중 교체는 지원하지 않는다
    // (ResultPublisher::onStateChange와 같은 규약).
    void onStateChangeEvent(EventSink cb) { event_sink_ = std::move(cb); }

    // 지연 계측 훅을 등록한다 (예: LatencyLogger::log). 위 onStateChangeEvent()와 같은 규약 -
    // start()/submit() 전에 한 번만 설정한다.
    void onLatencyEvent(LatencySink cb) { latency_sink_ = std::move(cb); }

    // "아직 판정할 프레임이 한 장도 안 들어왔음"을 나타내는 자리표시 판정 결과.
    //
    // 값 선택 근거:
    //   risk_level = SAFE      : 아직 위험 근거가 없다. 모르는 상태를 CAUTION 이상으로
    //                            올리면 단말이 기동 직후 상시 경보를 울린다.
    //   exception  = NONE      : 센서 고장/폐색이 확인된 게 아니다(그냥 입력이 없을 뿐).
    //   distance_mm = -1        : 엔진이 "거리 판정 불가"에 쓰는 것과 같은 sentinel.
    //                            toJson()이 음수를 null로 내보낸다(0.0으로 두면 하류가
    //                            "0mm 밀착"으로 읽는다 - 기본값 {}를 그대로 쓰면 안 되는 이유).
    //   camera_id/zone/terminal_id = "" : 빈 문자열 -> JSON null (기존 미확정 규약 그대로).
    //                            terminal_id는 기동 시점에 이미 아는 값이라 호출부가
    //                            채워 넣을 수 있다(primeIdle 인자).
    //   latency    = 기본값     : t0/t1이 없는 스냅숏이라 지연 로그에 올리면 안 된다.
    //                            primeIdle()이 latency_sink_를 부르지 않는 이유이기도 하다.
    static JudgmentResult idleResult() {
        JudgmentResult r{};
        r.camera_risk = RiskLevel::SAFE;
        r.tof_risk    = RiskLevel::SAFE;
        r.final_risk  = RiskLevel::SAFE;
        r.exception   = ExceptionState::NONE;
        r.distance_mm  = -1.0;
        return r;
    }

    // 기동 시 idle 상태로 하트비트를 깨운다. start() 전후 아무 때나 한 번 부르면 된다.
    //
    // 왜 submit(idleResult())가 아닌 별도 진입점인가:
    //   submit()은 "변화 시 즉시 전송" 경로라 event_sink_(SQLite 이벤트 로그)와
    //   latency_sink_(지연 CSV)를 함께 부른다. idle은 판정이 아니므로 둘 다 부르면 안 된다.
    //   - 이벤트 로그: 서버가 뜰 때마다 판정 근거 없는 SAFE 행이 한 줄씩 쌓인다.
    //   - 지연 로그  : t0/t1이 비어 있어(에폭 0) judge_ms/server_total_ms에 프로세스
    //                  가동시간만큼의 거대한 값이 섞여 통계가 통째로 망가진다.
    //   그래서 여기서는 last_만 채우고(= 하트비트 루프 기동), 실제 송신은 첫 하트비트에
    //   맡긴다. 훅 등록 순서(onStateChangeEvent/onLatencyEvent)와도 무관해진다.
    //
    // 이미 실제 판정이 한 건이라도 들어와 있으면 아무 일도 하지 않는다(덮어쓰기 금지).
    void primeIdle(const JudgmentResult& idle = idleResult()) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (has_last_) return;
            last_         = idle;
            has_last_     = true;
            last_is_idle_ = true;
            next_heartbeat_ = Clock::now() + period_;
        }
        cv_.notify_one();   // start()가 먼저 불렸다면 대기 중인 워커를 바로 깨운다
    }

    // 마지막으로 전송한 판정 결과가 있는지 (없으면 하트비트도 나가지 않는다).
    // primeIdle() 이후에는 실제 판정이 없어도 true다.
    bool hasResult() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return has_last_;
    }

    // 상태 변화로 즉시 나간 누적 건수
    std::size_t changeSendCount() const { return change_sends_.load(); }

    // 무변화 구간에서 주기 재전송으로 나간 누적 건수
    std::size_t heartbeatSendCount() const { return heartbeat_sends_.load(); }

    std::chrono::milliseconds heartbeatPeriod() const { return period_; }

    // "판정 상태가 같은가" 판단 기준.
    //
    // 비교 대상: risk_level / exception_state / camera_id / zone
    //   = 단말이 경보 동작을 결정할 때 쓰는 필드들.
    // distance_mm을 일부러 제외한 이유: 사람이 조금만 움직여도 매 프레임 값이 흔들려서
    // 포함하면 사실상 "프레임마다 전송"이 되어 하이브리드 방식의 의미가 없어진다.
    // 거리가 임계값을 넘어가면 risk_level이 바뀌므로 위험 전이는 그대로 즉시 전송되고,
    // 표시용 거리 값은 200ms 하트비트에 최신 값이 실려 나간다.
    static bool sameState(const JudgmentResult& a, const JudgmentResult& b) {
        return a.final_risk == b.final_risk
            && a.exception  == b.exception
            && a.camera_id  == b.camera_id
            && a.zone       == b.zone;
    }

    // 송신 디버그 로그를 켜는 환경변수 이름. 기본은 꺼짐이고, 값이 설정돼 있으면 켜진다
    // ("0"/""/"false"/"off"는 명시적 꺼짐으로 취급 - 스크립트에서 =0으로 끄는 흔한 관례).
    static constexpr const char* kSendLogEnvVar = "FORKLIFT_DEBUG_SEND_LOG";

    // 현재 송신 로그가 켜져 있는지 (테스트/기동 로그에서 상태를 확인할 용도).
    static bool sendLogEnabled() { return send_log_enabled_; }

private:
    // 환경변수 파싱. 아래 send_log_enabled_의 초기화식으로 딱 한 번만 불린다.
    static bool readSendLogEnv() {
        const char* v = std::getenv(kSendLogEnvVar);
        if (v == nullptr) return false;               // 미설정 -> 꺼짐(기본값)
        return !(v[0] == '\0'
                 || std::strcmp(v, "0")     == 0
                 || std::strcmp(v, "false") == 0
                 || std::strcmp(v, "off")   == 0);
    }

    // 송신 로그 on/off 플래그.
    //
    // getenv()를 매 송신마다 부르면(200ms 하트비트 + 변화 시마다) 판정 루프에 환경변수
    // 테이블 조회가 얹힌다. inline static 변수라 정적 초기화 시점(main() 진입 전)에
    // 한 번만 평가되고, 이후 logSend()의 체크는 bool 로드 + 분기뿐이다.
    // 함수 안 static 지역변수(magic static)를 쓰지 않은 이유: 호출마다 가드 변수를
    // 확인하는 코드가 붙기 때문. 초기화식이 다른 전역에 의존하지 않아(getenv만 호출)
    // 정적 초기화 순서 문제도 없다.
    //
    // 부작용: 프로세스 기동 후 환경변수를 바꿔도 반영되지 않는다(의도된 동작 -
    // 런타임 토글은 요구사항이 아니고, 로그 일련번호에 구멍이 생기지도 않는다).
    inline static const bool send_log_enabled_ = readSendLogEnv();

    // 송신 1건을 stderr에 한 줄로 남긴다.
    // 이 채널은 "변화 즉시 + 무변화 시 주기 재전송"이라 두 경로가 섞여 나가는데,
    // 나간 줄만 봐서는 어느 쪽인지 구분되지 않아 하트비트 주기 실측이 불가능했다.
    // reason으로 경로를 구분하고, mono_ms(단조 시계)를 같이 찍어 재전송 간격을
    // 벽시계 보정(NTP 등)과 무관하게 계산할 수 있게 한다.
    //
    // 기본은 꺼짐이다(kSendLogEnvVar 참고). 정상 운영 중에는 stderr I/O가 송신 경로에
    // 아예 끼어들지 않아야 하므로, 출력만 막는 게 아니라 함수 전체를 건너뛴다.
    // send_seq_까지 같이 멈추지만 이 값은 로그 줄 번호 전용이라(다른 곳에서 읽지 않는다)
    // 문제가 없다 - 실제 송신 건수는 change_sends_/heartbeat_sends_가 호출부에서
    // 따로 세고 있고, 그쪽은 이 플래그와 무관하게 항상 증가한다.
    //
    // 호출 규약: mtx_를 잡지 않은 구간에서 부를 것. 송신 경로에 stderr I/O가
    // 얹히므로, 락을 든 채로 부르면 판정 루프의 submit()까지 같이 밀린다.
    void logSend(const char* reason, const JudgmentResult& r) {
        if (!send_log_enabled_) return;

        const auto seq = ++send_seq_;
        const auto wall = std::chrono::system_clock::now();
        const auto wall_t = std::chrono::system_clock::to_time_t(wall);
        const auto wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 wall.time_since_epoch()).count() % 1000;
        const auto mono_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 Clock::now().time_since_epoch()).count();

        std::tm tm_buf{};
        ::localtime_r(&wall_t, &tm_buf);

        // 여러 스레드(판정 루프 / 하트비트 워커)가 같이 찍으므로 한 줄을 통째로
        // 만들어 한 번에 내보낸다 - 스트림에 조각으로 흘리면 줄이 서로 섞인다.
        std::ostringstream line;
        line << "[판정] risk_event seq=" << seq
             << " reason=" << reason
             << " terminal=" << (r.terminal_id.empty() ? "-" : r.terminal_id)
             << " stream=" << (r.stream_id.empty() ? "-" : r.stream_id)
             << " camera=" << (r.source_camera_id.empty() ? r.camera_id : r.source_camera_id)
             << " channel=" << r.channel
             << " distance_mm=";
        if (r.distance_mm < 0.0) line << "-";
        else line << std::fixed << std::setprecision(1) << r.distance_mm;
        line << " risk_level=" << toString(r.final_risk)
             << " exception_state=" << toString(r.exception)
             << " t=" << std::put_time(&tm_buf, "%H:%M:%S")
             << '.' << std::setfill('0') << std::setw(3) << wall_ms
             << " mono_ms=" << mono_ms
             << "\n";
        std::cerr << line.str();
    }

    void run() {
        std::unique_lock<std::mutex> lk(mtx_);
        while (running_) {
            if (!has_last_) {
                // 아직 판정 결과가 한 건도 없으면 재전송할 것도 없다.
                cv_.wait_for(lk, period_);
                continue;
            }
            const auto now = Clock::now();
            if (now < next_heartbeat_) {
                cv_.wait_until(lk, next_heartbeat_);
                continue;
            }

            // utc_time은 toJson()이 호출 시점으로 새로 찍는다. 판정 내용(위험도/예외/거리)은
            // 마지막 결과 그대로이고 시각만 갱신되므로, 단말이 "언제 기준 상태인지"를
            // 하트비트만으로도 알 수 있다.
            const JudgmentResult snapshot = last_;
            next_heartbeat_ = now + period_;

            lk.unlock();
            ++heartbeat_sends_;
            logSend("하트비트", snapshot);
            sink_(toJson(snapshot));
            lk.lock();
        }
    }

    Sink sink_;
    EventSink event_sink_;     // 상태 변화 시에만 호출. 미등록이면 로깅 없이 동작한다.
    LatencySink latency_sink_; // 상태 변화 시에만 호출. 미등록이면 지연 계측 없이 동작한다.
    std::chrono::milliseconds period_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;
    JudgmentResult last_{};
    bool has_last_ = false;
    // last_가 primeIdle()이 넣은 자리표시 값인지(= 아직 진짜 판정이 없음).
    // 첫 submit()에서 false가 되며, 그 전까지 이벤트 로그의 "직전 상태" 취급을 막는다.
    bool last_is_idle_ = false;
    Clock::time_point next_heartbeat_{};

    std::atomic<bool> running_{false};
    std::thread worker_;

    std::atomic<std::size_t> change_sends_{0};
    std::atomic<std::size_t> heartbeat_sends_{0};

    // 송신 로그 줄의 일련번호. 두 경로(변화/하트비트)가 같은 카운터를 쓰므로
    // 로그만 보고도 실제 전송 순서를 복원할 수 있다.
    // 로그가 꺼져 있으면 증가하지 않는다 - 이 값을 읽는 곳은 logSend()의 출력뿐이고,
    // 전송 건수는 change_sends_/heartbeat_sends_가 따로 센다.
    std::atomic<std::size_t> send_seq_{0};
};

} // namespace risk_transport
