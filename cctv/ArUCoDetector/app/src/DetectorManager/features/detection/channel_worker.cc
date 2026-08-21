#include "channel_worker.h"

#include <chrono>    // 시간 측정/시각 (steady_clock, system_clock, duration_cast)
#include <ctime>     // std::time_t, std::gmtime
#include <iomanip>   // std::put_time
#include <sstream>   // std::ostringstream
#include <time.h>    // clock_gettime, CLOCK_THREAD_CPUTIME_ID (POSIX, 스레드별 CPU 시간)

#include "undistort.h" // TryUndistort
#include "detection_slot_limiter.h"

namespace {
    // 지금 시각(UTC)을 "2026-07-24T09:00:00.123Z" 같은 ISO8601(밀리초 포함) 문자열로 만든다.
    // (이 파일 안에서만 쓰는 헬퍼라 익명 namespace에 둠 → 바깥에 이름 노출 안 됨)
    // 밀리초가 꼭 필요함: 채널당 검출 주기가 1초보다 짧으면 같은 초 안에 검출이 2번 이상
    // 끝날 수 있는데, 초 단위까지만 찍으면 문자열이 똑같아져서(예: 지연 측정 UI가
    // "last_detect가 바뀌었는지"로 새 검출을 판단) 뒤 검출이 조용히 유실된다.
    std::string NowIso8601() {
        auto now = std::chrono::system_clock::now();               // 현재 벽시계 시각
        std::time_t t = std::chrono::system_clock::to_time_t(now); // C 스타일 time_t로 변환
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::tm tm = *std::gmtime(&t);                             // time_t → UTC 분해시각(년/월/일/시…)
        std::ostringstream ss;
        ss << std::put_time(&tm, "%FT%T.")                         // %F=날짜, T, %T=시:분:초
           << std::setfill('0') << std::setw(3) << ms.count() << "Z";
        return ss.str();
    }

    // 호출 스레드가 "실제로 CPU에서 실행된" 누적 시간(ms). CLOCK_THREAD_CPUTIME_ID는
    // 이 스레드가 CPU를 점유한 시간만 세고, 다른 스레드에게 밀려 대기(경합)한 시간은
    // 세지 않는다 — steady_clock(벽시계)과 대조해서 "경합으로 날아간 시간"을 가려내는 데 쓴다.
    long long ThreadCpuTimeMs() {
        struct timespec ts;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    }
} // namespace

// 생성자: 구성값을 멤버에 저장하고, 캘리브레이션 파일을 한 번 로드한다.
// (멤버는 헤더의 "선언 순서"대로 초기화됨 — 아래 초기화 리스트에 적은 순서가 아니라)
ChannelWorker::ChannelWorker(int channel, RawFrameStore* store,
                             const std::string& calib_path,
                             cv::aruco::PREDEFINED_DICTIONARY_NAME dict, bool undistort,
                             int poll_interval_ms, DetectionSlotLimiter* slot_limiter, SendFn send)
    : channel_(channel),
      undistort_(undistort),
      poll_interval_ms_(poll_interval_ms),
      raw_store_(store),                          // raw 프레임 저장소 포인터 저장
      calib_(LoadCameraCalibration(calib_path)),  // 파일을 지금 한 번 읽어 결과 저장
      detector_(dict),                            // dict로 ArUco 사전 로드
      slot_limiter_(slot_limiter),
      send_(std::move(send))                      // 콜백을 move로 가져옴(내부 클로저 복사 안 함)
{
    // 이 시점엔 아직 워커 스레드가 없다(단일 스레드) → 락 없이 status_ 써도 안전.
    status_.calibration = calib_.valid;
    status_.undistort_enabled = undistort_;  // 설정값(채널별 왜곡보정 토글) 그대로 노출
}

// 소멸자: 스레드를 반드시 정리한다. 안 하면
//  (1) 스레드가 이미 파괴된 객체 메모리(status_, source_ 등)를 접근 → crash
//  (2) 아직 joinable한 std::thread가 소멸되면 std::terminate() → 프로그램 강제종료
ChannelWorker::~ChannelWorker()
{
    Stop();
}

void ChannelWorker::Start()
{
    {
        // lock_guard 생성자가 mtx_.lock()을 호출(다른 스레드가 잡고 있으면 그동안 대기).
        // 블록을 벗어나면 소멸자가 mtx_.unlock() → "이 블록 동안만 mtx_ 소유".
        std::lock_guard<std::mutex> lk(mtx_);
        if (running_) return;   // 이미 돌고 있으면(중복 Start) 무시
        running_ = true;
    } // 여기서 mtx_ 풀림
    {
        // 상태에도 "돌고 있음" 반영 (mtx_와는 별개인 status_mtx_가 보호하는 데이터)
        std::lock_guard<std::mutex> lk(status_mtx_);
        status_.running = true;
    }
    // OS 스레드를 새로 만든다. 그 스레드는 즉시 this->Loop()를 실행하기 시작한다.
    // [this] = this 포인터(주소)를 값으로 캡처. 이 줄 이후 실행 스레드가 2개가 된다.
    thread_ = std::thread([this] { Loop(); });
}

void ChannelWorker::Stop()
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_) return;  // 이미 꺼짐(중복 Stop / Start 안 함) → 반환
        running_ = false;       // 루프 종료 신호
    } // mtx_ 풀림 → 아래 notify가 워커의 wait_for를 깨울 수 있음

    // Loop가 wait_for에서 자고 있으면 깨운다. (대기 중인 스레드가 없으면 아무 일도 안 함)
    // running_=false 를 "먼저" 쓰고 notify 하는 순서가 중요:
    //   - 워커가 자고 있으면       → notify가 깨우고, 깬 뒤 pred(!running_)=true → 루프 탈출
    //   - 워커가 안 자고 RunOnce 중 → 다음 루프의 pred 검사에서 running_=false를 봄
    cv_.notify_all();

    // 워커 스레드의 Loop()가 끝나고 OS 스레드가 완전히 종료될 때까지 "여기서 기다린다".
    // joinable(): Start를 안 했으면 thread_가 비어 있어 join하면 예외 → 가드.
    // 데드락 없음: 워커가 부르는 send_(SendNoReplyEvent)는 논블로킹이라 워커가 안 막힘.
    if (thread_.joinable()) thread_.join();

    {
        std::lock_guard<std::mutex> lk(status_mtx_);
        status_.running = false;
    }
}

// 현재 상태를 "복사해서" 반환. 호출자는 DetectorManager 스레드(/status 처리).
// status_mtx_로 짧게 잠가서, 워커 스레드의 상태 갱신(RunOnce 5단계)과 겹치지 않게 한다.
ChannelWorker::Status ChannelWorker::GetStatus() const
{
    std::lock_guard<std::mutex> lk(status_mtx_);
    return status_; // 값 복사 (락은 이 함수 끝에서 풀림)
}

// 폴링 루프. Start가 만든 워커 스레드 위에서 계속 돈다.
void ChannelWorker::Loop()
{
    // 채널마다 첫 실행 시각을 살짝 어긋나게 한다(검출 알고리즘/정확도는 그대로,
    // 여러 채널의 무거운 검출이 같은 순간에 몰려 CPU를 다투지 않도록 "타이밍만" 분산).
    // 채널 4개 기준 poll_interval을 4등분해 0/4, 1/4, 2/4, 3/4 지점에서 시작하게 함.
    {
        std::unique_lock<std::mutex> lk(mtx_);
        int stagger_ms = ((channel_ - 1) % 4) * (poll_interval_ms_ / 4);
        if (stagger_ms > 0) {
            cv_.wait_for(lk, std::chrono::milliseconds(stagger_ms), [this] { return !running_; });
        }
        if (!running_) return;   // 대기 중 Stop되면 루프 진입 전에 바로 종료
    }

    while(true)
    {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            // 여기서 최대 poll_interval_ms 만큼 "잔다". 자는 동안:
            //   - mtx_는 자동으로 풀린다(그래서 그 사이 Stop이 running_을 바꿀 수 있음)
            //   - CPU를 안 쓴다(OS가 스레드를 재움)
            // 깨는 조건: 시간이 다 됨 / cv_.notify / spurious wakeup.
            // 세 번째 인자 pred(!running_) 덕분에 spurious wakeup은 자동으로 걸러지고,
            // 깰 때 mtx_를 다시 잡은 상태로 pred를 재확인한다.
            cv_.wait_for(lk, std::chrono::milliseconds(poll_interval_ms_),
                        [this] {return !running_; });
            if (!running_) break;   // Stop이 불려 running_=false면 루프 종료
        } // ← 여기서 mtx_ 풀림. RunOnce는 "락 없이" 실행해야
          //   그 수백 ms 동안 Stop()이 mtx_를 못 잡아 멈추는 일이 없다.
        RunOnce();
    }
}

// 파이프라인 1회. 무거운 일은 락 없이 하고, 마지막에 status_ 갱신할 때만 짧게 잠근다.
void ChannelWorker::RunOnce()
{
    // 지연 측정 시작. steady_clock(벽시계, 경합 포함) + 스레드 CPU 시간(경합 미포함)을
    // 나란히 잰다 — 둘의 차이가 "다른 채널에게 밀려 대기한 시간(경합)"이다.
    auto t0 = std::chrono::steady_clock::now();
    long long cpu0 = ThreadCpuTimeMs();

    // 1) raw 저장소에서 이 채널의 최신 프레임(grayscale)을 가져온다. (frame_get)
    auto t_get0 = std::chrono::steady_clock::now();
    cv::Mat gray = raw_store_->Get(channel_);
    auto t_get1 = std::chrono::steady_clock::now();
    if (gray.empty())
    {
        // 이번 폴은 스킵(전송 안 함). 상태에 에러만 남긴다.
        std::lock_guard<std::mutex> lk(status_mtx_);
        status_.last_error = "아직 raw 프레임 수신 전 (ch " + std::to_string(channel_) + ")";
        return;
    }

    bool undistorted = false; // 실제 보정 적용 여부 (calib 유효 + 토글 on + 해상도 일치를 모두 만족해야 true) -> status_.undistort_applied로 노출
    cv::Mat corrected;
    DetectionResult result;

    // 2) 슬롯 대기 (slot_wait: 다른 채널이 검출 중이면 여기서 대기)
    auto t_slot0 = std::chrono::steady_clock::now();
    slot_limiter_->Acquire();
    auto t_slot1 = std::chrono::steady_clock::now();

    // 3) 왜곡보정 (undistort)
    auto t_undist0 = std::chrono::steady_clock::now();
    corrected = TryUndistort(gray, calib_, undistort_, undistorted);    // undistort_가 false이거나 캘리브레이션 무효/해상도 불일치면 원본 그대로.
    auto t_undist1 = std::chrono::steady_clock::now();

    // 4) ArUco 검출 (detect)
    auto t_det0 = std::chrono::steady_clock::now();
    result = detector_.Detect(corrected);                               // detector_는 자기 상태를 안 바꾸는 const 연산
    auto t_det1 = std::chrono::steady_clock::now();

    slot_limiter_->Release();

    // 5) 결과 전송 (send: XML 빌드 및 논블로킹 송신)
    auto t_send0 = std::chrono::steady_clock::now();
    send_(channel_, result.ids, result.corners);
    auto t_send1 = std::chrono::steady_clock::now();

    auto t1 = std::chrono::steady_clock::now();
    long long cpu1 = ThreadCpuTimeMs();

    // t1-t0 = 시계 틱 단위 duration → ms 및 μs로 변환
    int latency = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    int cpu_latency = static_cast<int>(cpu1 - cpu0);

    int get_us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(t_get1 - t_get0).count());
    int slot_us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(t_slot1 - t_slot0).count());
    int undist_us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(t_undist1 - t_undist0).count());
    int det_us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(t_det1 - t_det0).count());
    int send_us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(t_send1 - t_send0).count());
    int tot_us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    // 6) 상태 갱신. GET /status가 읽는 데이터라 status_mtx_로 보호.
    {
        std::lock_guard<std::mutex> lk(status_mtx_);
        status_.marker_count = static_cast<int>(result.ids.size());
        status_.marker_ids = result.ids;
        status_.rejected_count = result.rejected_count;
        status_.latency_ms = latency;
        status_.cpu_latency_ms = cpu_latency;
        status_.frame_get_us = get_us;
        status_.slot_wait_us = slot_us;
        status_.undistort_us = undist_us;
        status_.detect_us = det_us;
        status_.send_us = send_us;
        status_.total_us = tot_us;
        status_.last_detect = NowIso8601(); // 이번 검출 완료 시각 기록
        status_.last_error.clear();         // 성공했으니 이전 에러 기록 제거
        status_.undistort_applied = undistorted; // 이번 폴링에서 실제로 보정이 적용됐는지
    }
}
