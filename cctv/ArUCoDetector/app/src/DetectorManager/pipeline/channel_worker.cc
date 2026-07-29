#include "channel_worker.h"

#include <chrono>    // 시간 측정/시각 (steady_clock, system_clock, duration_cast)
#include <ctime>     // std::time_t, std::gmtime
#include <iomanip>   // std::put_time
#include <sstream>   // std::ostringstream
#include <time.h>    // clock_gettime, CLOCK_THREAD_CPUTIME_ID (순수 스레드 CPU 시간)

#include "frame_preprocessor.h" // TryUndistort, ConvertToGrayscale

namespace {
    // 이 스레드가 실제로 CPU 코어에서 수행된 시간만 측정한다 (ms 단위).
    // 다른 스레드/프로세스에 밀려 CPU를 뺏기고 대기한 시간(경합)은 포함되지 않는다.
    // latency_ms(벽시계) - ThreadCpuTimeMs()(CPU시간) = 경합으로 소모된 대기시간.
    int64_t ThreadCpuTimeMs() {
        struct timespec ts;
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
            return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
        }
        return 0;
    }

    // 지금 시각(UTC)을 "2026-07-24T09:00:00Z" 같은 ISO8601 문자열로 만든다.
    // (이 파일 안에서만 쓰는 헬퍼라 익명 namespace에 둠 → 바깥에 이름 노출 안 됨)
    std::string NowIso8601() {
        auto now = std::chrono::system_clock::now();               // 현재 벽시계 시각
        std::time_t t = std::chrono::system_clock::to_time_t(now); // C 스타일 time_t로 변환
        std::tm tm = *std::gmtime(&t);                             // time_t → UTC 분해시각(년/월/일/시…)
        std::ostringstream ss;
        ss << std::put_time(&tm, "%FT%TZ");                        // %F=날짜, T, %T=시간, Z=UTC 표기
        return ss.str();
    }
} // namespace

// 생성자: 구성값을 멤버에 저장하고, 캘리브레이션 파일을 한 번 로드한다.
// (멤버는 헤더의 "선언 순서"대로 초기화됨 — 아래 초기화 리스트에 적은 순서가 아니라)
ChannelWorker::ChannelWorker(int channel, const CameraCredentials& credentials,
                             const std::string& calib_path,
                             cv::aruco::PREDEFINED_DICTIONARY_NAME dict, bool undistort,
                             int poll_interval_ms, SendFn send)
    : channel_(channel),
      undistort_(undistort),
      poll_interval_ms_(poll_interval_ms),
      source_(credentials),                       // credentials를 FrameSource 안에 값복사
      calib_(LoadCameraCalibration(calib_path)),  // 파일을 지금 한 번 읽어 결과 저장
      detector_(dict),                            // dict로 ArUco 사전 로드
      send_(std::move(send))                      // 콜백을 move로 가져옴(내부 클로저 복사 안 함)
{
    // 이 시점엔 아직 워커 스레드가 없다(단일 스레드) → 락 없이 status_ 써도 안전.
    status_.calibration = calib_.valid;
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
    // 지연 측정 시작. steady_clock = 단조 증가 시계(구간 측정 전용, NTP 등에 안 튐).
    // cpu0 = 순수 CPU 시간(CLOCK_THREAD_CPUTIME_ID). 코어 경합 분리용.
    auto t0 = std::chrono::steady_clock::now();
    int64_t cpu0 = ThreadCpuTimeMs();

    // 1) 스냅샷 요청 + JPEG 디코딩. 실패하면 빈 Mat + error 문자열.
    std::string error;
    cv::Mat color = source_.Acquire(channel_, error);
    if (color.empty())
    {
        // 이번 폴은 스킵(전송 안 함). 상태에 에러만 남긴다.
        std::lock_guard<std::mutex> lk(status_mtx_);
        status_.last_error = error;
        return;
    }

    // 2) (옵션) 왜곡보정 → grayscale.
    //    undistort_가 false이거나 캘리브레이션 무효/해상도 불일치면 원본 그대로 gray.
    bool undistorted; // 실제 보정 적용 여부(현재 상태엔 안 씀. 필요하면 /status에 추가 가능)
    cv::Mat corrected = TryUndistort(color, calib_, undistort_, undistorted);
    cv::Mat gray = ConvertToGrayscale(corrected);

    // 3) ArUco 검출 (detector_는 자기 상태를 안 바꾸는 const 연산)
    DetectionResult result = detector_.Detect(gray);

    // 4) 결과 전송. 검출 0개여도 매 폴링마다 보낸다(서버가 "마커 사라짐"도 알아야 하므로).
    //    send_는 DetectorManager::SendMetadata를 감싼 콜백 → 내부는 SendNoReplyEvent(논블로킹).
    send_(channel_, result.ids, result.corners);

    auto t1 = std::chrono::steady_clock::now();
    int64_t cpu1 = ThreadCpuTimeMs();
    // t1-t0 = 시계 틱 단위 duration → ms로 변환 → .count()로 정수 추출
    int latency = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    int cpu_latency = static_cast<int>(cpu1 - cpu0);

    // 5) 상태 갱신. GET /status가 읽는 데이터라 status_mtx_로 보호.
    {
        std::lock_guard<std::mutex> lk(status_mtx_);
        status_.marker_count = static_cast<int>(result.ids.size());
        status_.latency_ms = latency;
        status_.cpu_latency_ms = cpu_latency;
        status_.undistort_enabled = undistort_;
        status_.undistort_applied = undistorted;
        status_.last_detect = NowIso8601(); // 이번 검출 완료 시각 기록
        status_.last_error.clear();         // 성공했으니 이전 에러 기록 제거
    }
}
