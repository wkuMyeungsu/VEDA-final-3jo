#pragma once

// ── 표준 라이브러리 ──
#include <condition_variable>           // std::condition_variable : 스레드를 재우고 신호로 깨우는 도구
#include <functional>                   // std::function           : 함수/람다(콜백)를 담는 타입
#include <mutex>                        // std::mutex, lock_guard   : 공유 데이터 보호용 잠금
#include <string>
#include <thread>                       // std::thread              : OS 스레드 생성/관리
#include <vector>

#include <opencv2/core.hpp>             // cv::Mat, cv::Point2f

#include "aruco_detector.h"             // ArucoDetector, DetectionResult, PREDEFINED_DICTIONARY_NAME
#include "camera_calibration.h"         // CameraCalibration, LoadCameraCalibration
#include "raw_frame_store.h"            // RawFrameStore
#include "detection_slot_limiter.h"     // DetectionSlotLimiter

// 채널 하나를 전담하는 폴링 워커.
// 자기만의 std::thread 하나를 돌리면서 poll_interval_ms 주기로
//   스냅샷 → (옵션)왜곡보정 → grayscale → ArUco 검출 → 전송 콜백
// 을 반복한다. 채널마다 이 객체를 하나씩 만든다(채널 간 공유 상태 없음 → 채널 간 락 불필요).
class ChannelWorker {
    public:
        // 검출 결과를 바깥으로 넘기는 콜백 타입.
        // DetectorManager가 [this](ch,ids,corners){ SendMetadata(ch,ids,corners); } 를 주입한다.
        // 워커는 "어떻게/어디로 전송하는지" 모른 채 이 함수만 호출 → 검출과 전송의 분리.
        using SendFn = std::function<void(int channel, const std::vector<int>& ids,
                                          const std::vector<std::vector<cv::Point2f>>& corners)>;

        // GET /status 가 읽어갈 채널별 상태 스냅샷.
        // 워커 스레드가 쓰고, DetectorManager 스레드가 읽으므로 status_mtx_ 로 보호한다.
        struct Status {
            bool running = false;       // 이 워커가 폴링 루프를 돌고 있는가
            int marker_count = 0;       // 마지막 폴링에서 검출된 마커 개수
            int rejected_count = 0;     // 마지막 폴링에서 사각형처럼 보였지만 사전과 안 맞아 탈락한 후보 개수.
                                         // 마커 유무와 무관하게 이 값이 크면 장면(컨투어)이 복잡해 검출 비용이 늘어난다는 신호.
            int latency_ms = 0;         // 마지막 폴링 1회 소요 시간(ms) — 벽시계(체감 지연). 다른 채널에게
                                         // 밀려 CPU를 못 받고 대기한 시간(경합)도 그대로 포함된다.
            int cpu_latency_ms = 0;     // 같은 구간에서 "이 스레드가 실제로 CPU에서 실행된" 시간만(ms).
                                         // 경합으로 대기한 시간은 빠짐 → 파이프라인 자체의 순수 비용.
                                         // latency_ms - cpu_latency_ms = 경합 때문에 날아간 시간.
            std::string last_detect;    // 마지막 검출 완료 시각 (ISO8601 UTC 문자열)
            std::string last_error;     // 마지막 에러 (스냅샷/디코딩 실패 등). 성공하면 비움
            bool calibration = false;        // 이 채널의 캘리브레이션 파일이 유효한가 (파일 존재+파싱 성공 여부일 뿐, 적용 여부와 무관)
            bool undistort_enabled = false;  // 설정(채널별 왜곡보정 토글)이 켜져 있는가
            bool undistort_applied = false;  // 마지막 폴링에서 실제로 왜곡보정이 적용됐는가
                                              // (calibration && undistort_enabled && 해상도 일치 모두 만족해야 true)
        };

        // channel          : 담당 채널 번호(1~4)
        // store            : 채널별 최신 raw 프레임 저장소 (여기서 이 채널의 프레임을 읽음)
        // calib_path       : 이 채널의 calib_result_chN.json 절대경로 (생성자에서 한 번 로드)
        // dict             : 검출에 쓸 ArUco 사전 (StringToDict로 문자열→enum 변환한 값)
        // undistort        : 이 채널에서 우리 왜곡보정을 적용할지 (설정의 채널별 토글)
        // poll_interval_ms : 폴링 주기(ms)
        // send             : 검출 결과 전송 콜백
        ChannelWorker(int channel, RawFrameStore* store, const std::string& calib_path,
                      cv::aruco::PREDEFINED_DICTIONARY_NAME dict, bool undistort,
                      int poll_interval_ms, DetectionSlotLimiter* slot_limiter,SendFn send);

        ~ChannelWorker();   // 소멸 시 반드시 Stop()으로 스레드 회수 (안 하면 crash/terminate)

        // mutex/thread를 멤버로 들고 있어 복사·이동이 불가능한 타입이다.
        // 그래서 DetectorManager는 이 객체를 unique_ptr(포인터)로 소유한다.
        ChannelWorker(const ChannelWorker&) = delete;
        ChannelWorker& operator=(const ChannelWorker&) = delete;

        void Start();               // 워커 스레드를 만들어 폴링 루프 시작
        void Stop();                // 루프를 멈추고 스레드가 끝날 때까지 대기(join)
        Status GetStatus() const;   // 현재 상태를 복사해서 반환(스레드 안전)

    private:
        void Loop();        // 폴링 루프 본체 (워커 스레드에서 실행됨)
        void RunOnce();     // 파이프라인 1회 (스냅샷→검출→전송→상태갱신)

        // ── 생성 시 정해지고 이후 안 바뀌는 구성값들 ──
        int channel_;                       // 담당 채널 번호
        bool undistort_;                    // 왜곡보정 on/off
        int poll_interval_ms_;              // 폴링 주기
        RawFrameStore* raw_store_;          // 비소유 포인터. 채널별 최신 raw 프레임 저장소 (수명은 DetectorManager가 관리)
        CameraCalibration calib_;           // 로드된 캘리브레이션 값
        ArucoDetector detector_;            // 검출기 (사전 보유). 생성 후 상태 안 변함
        DetectionSlotLimiter* slot_limiter_; // 비소유 포인터. 수명은 DetectorManager가 관리
        SendFn send_;                       // 전송 콜백

        // ── 루프 제어용 (mtx_ 가 보호) ──
        std::thread thread_;          // 워커 스레드 핸들
        std::mutex mtx_;              // running_ 과 cv_ 대기를 보호
        std::condition_variable cv_;  // 주기 대기 + 즉시 깨우기(Stop)용
        bool running_ = false;        // 루프를 계속 돌지 여부 (Stop이 false로 바꿈)

        // ── 상태 공유용 (status_mtx_ 가 보호) ──
        // mutable: GetStatus()가 const 함수인데도 이 mutex는 잠가야 하므로 mutable로 둔다.
        mutable std::mutex status_mtx_;
        Status status_;               // /status가 읽어갈 최신 상태
};
