#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>

// SensorUplinkReceiver - 9002 단말->서버 센서 업링크 수신기 (POSIX 전용)
//
// [배경] 9000(ResultPublisher)/9001(CameraAssignmentServer)은 둘 다 서버->단말 방향이다.
// 이 채널은 반대 방향으로, 단말(forklift-device)에 붙은 IMU/ToF 원시값을 서버가 받는다.
// 센서와 판정 엔진이 물리적으로 다른 보드(단말 RPi4 / veda3)에 있어서 프로세스 내부
// 결선(SensorCollectorReader)으로는 값을 넘길 수 없기 때문에 생긴 채널이다.
//
// 팀 표준(채널별 포트 분리 + 각 채널 1:1 연결)을 따르므로 ResultPublisher처럼 단말은
// 동시에 1대만 붙는다. 연결 중에 들어온 추가 접속은 accept하지 않고 listen 백로그에
// 남겨두며, 기존 연결이 끊기면 그때 accept된다.
//
// 프로토콜 (개행 구분 JSON = NDJSON, 한 줄 = 한 메시지):
//   단말 -> 서버 (접속 직후 1회):
//     {"type":"hello","terminal_id":"TERM_01"}
//   단말 -> 서버 (그 뒤로 주기적으로):
//     {"camera_id":"cam0","tof_ok":true,"tof_distance_mm":420,"imu_ok":true,
//      "imu_accel_x_g":0.02,"imu_accel_y_g":-0.01,"imu_accel_z_g":1.01,
//      "ts_ms":1754380800123}
//
// hello 처리는 CameraAssignmentServer와 같은 규약이다 - kHelloTimeoutSec 안에 유효한
// hello가 안 오거나 첫 줄이 hello가 아니면 연결을 끊는다. 센서 줄은 terminal_id를 싣지
// 않으므로(스키마 협의 중), "누가 보낸 값인가"는 hello로 한 번 받은 값을 쓴다.
//
// [수신 경로] 서버가 이 값을 어디로 넣을지(명수님 이벤트 큐 vs 최신값 캐시)는 아직 팀
// 협의 전이다. 이 클래스는 그중 "최신값 캐시" 쪽만 구현한다 - 수신 스레드가 최신
// 스냅샷을 갱신해두고, 판정 루프가 아무 때나 getLatest()로 꺼내 쓰는 구조.
// 큐로 결론이 나더라도 이 클래스의 소켓/파싱 부분은 그대로 재사용할 수 있다.
//
// 프로젝트에 링크되는 JSON 라이브러리가 없어(third_party/nlohmann은 server_config
// 내부 전용) CameraAssignmentServer와 같은 방식의 문자열 검색 기반 최소 파서를 쓴다.
// 다만 이쪽은 단말이 계속 흘려보내는 스트림이라 깨진 줄을 만나도 연결을 끊지 않고
// 그 줄만 버린다(hello와 정책이 다른 이유는 .cpp의 handleLine() 주석 참고).

namespace risk_transport {

// 업링크로 받은 센서값 한 건의 스냅샷.
// 단위는 문서 스키마 그대로다(ToF는 정수 mm, IMU는 3축 g). m 변환이나 magnitude 계산은
// 여기서 하지 않는다 - 그건 ISensorReader 어댑터 쪽 책임이다
// (SensorCollectorReader가 이미 같은 변환을 갖고 있어 그 로직을 재사용하면 된다).
struct SensorUplinkSample {
    std::string camera_id;              // 단말이 보고 있다고 알려준 카메라
    bool        tof_ok = false;         // ToF 응답 정상 여부
    int         tof_distance_mm = 0;    // ToF 측정 거리 (정수 mm)
    bool        imu_ok = false;         // IMU 응답 정상 여부
    double      imu_accel_x_g = 0.0;
    double      imu_accel_y_g = 0.0;
    double      imu_accel_z_g = 0.0;
    int64_t     ts_ms = 0;              // 단말이 찍은 시각(단말 clock 기준 epoch ms).
                                        // 서버 시계와 동기화돼 있다는 보장이 없어서
                                        // 신선도 판단에는 쓰지 않는다(아래 received_at 사용).

    // 서버가 이 줄을 실제로 파싱한 시각. steady_clock이라 시스템 시각이 조정돼도
    // 경과 시간 계산이 뒤로 가지 않는다 -> isStale() 판단의 유일한 기준.
    std::chrono::steady_clock::time_point received_at{};
};

class SensorUplinkReceiver {
public:
    // 업링크 채널 기본 포트. Confluence "단말->서버 센서 업링크 채널 협의" 제안값이며
    // 아직 확정 전이라 생성자 인자로 언제든 덮어쓸 수 있게 뒀다.
    static constexpr uint16_t kDefaultPort = 9002;

    // 마지막 수신 이후 이만큼 지나면 isStale() == true.
    // risk_event 채널의 하이브리드 하트비트(200ms 주기 재전송, 타임아웃 1000~1500ms 제안)를
    // 그대로 재사용할지가 아직 미협의라 그 범위 가운데값으로 잠정 고정했다.
    // 확정되면 이 상수만 바꾸면 된다(호출부는 인자를 생략하는 것을 기본으로 한다).
    static constexpr int kDefaultStaleTimeoutMs = 1200;

    // hello 없이 접속만 유지되는 소켓을 끊기까지의 대기 시간(초).
    // CameraAssignmentServer::kHelloTimeoutSec과 같은 값으로 맞췄다.
    static constexpr int kHelloTimeoutSec = 5;

    // bind_host: 대기할 로컬 주소. ""/"0.0.0.0"이면 모든 인터페이스(INADDR_ANY).
    explicit SensorUplinkReceiver(std::string bind_host = "0.0.0.0",
                                  uint16_t port = kDefaultPort);
    ~SensorUplinkReceiver();

    SensorUplinkReceiver(const SensorUplinkReceiver&) = delete;
    SensorUplinkReceiver& operator=(const SensorUplinkReceiver&) = delete;

    void start();
    void stop();

    // 가장 최근에 정상 파싱된 스냅샷을 복사해 준다.
    // 한 번도 못 받았으면 false를 돌려주고 out은 건드리지 않는다
    // (기본값 0을 "0mm 밀착"으로 오독하는 사고를 구조적으로 막기 위해 out param 방식).
    bool getLatest(SensorUplinkSample& out) const;

    // 유효한 스냅샷을 한 번이라도 받았는지.
    // isStale()만으로는 "아직 한 번도 못 받음"과 "받았는데 끊긴 지 오래됨"을 구분할 수 없어
    // 로깅/UI용으로 따로 노출한다 (판정 정책은 둘을 똑같이 취급해야 한다 - isStale() 주석 참고).
    bool hasSample() const;

    // 마지막 수신 이후 timeout_ms를 넘었으면 true.
    //
    // [설계 결정] 아직 한 번도 못 받은 상태(연결 전 / hello만 받고 데이터 전)는 항상 true다.
    //   판정 엔진은 센서값을 못 받을 때 SAFE로 낙관하지 않고 SENSOR_FAULT -> 최소 CAUTION을
    //   유지하는 fail-safe 정책을 쓴다. 여기서 "아직 데이터 없음"을 fresh로 돌려주면 호출부가
    //   기본값 스냅샷(tof_distance_mm=0)을 진짜 측정값으로 오해해 근거 없는 DANGER를 내거나,
    //   반대로 조용히 SAFE로 흘려보낼 수 있다. 둘 다 안전 측면에서 나쁘므로
    //   "받은 게 없으면 stale"로 통일했다.
    //   (ResultDispatcher의 idle 프라이밍이 distance_m을 0.0이 아니라 null로 내보내는 것과
    //    같은 원칙이다.)
    bool isStale(int timeout_ms = kDefaultStaleTimeoutMs) const;

    // 단말이 현재 붙어 있는지 (hello 완료 여부와 무관한 소켓 레벨 상태).
    bool isConnected() const;

    // hello로 받은 단말 식별자. 연결이 끊겨도 마지막 값을 유지한다
    // (캐시에 남은 스냅샷이 어느 단말 것인지 알 수 있어야 하므로).
    std::string terminalId() const;

    // 정상 파싱해 캐시에 반영한 누적 건수.
    std::size_t receivedCount() const { return received_.load(); }

    // 파싱 실패로 버린 누적 줄 수 (깨진 JSON / 필드 누락).
    std::size_t parseFailureCount() const { return parse_failures_.load(); }

    // 단말이 접속한 누적 횟수 (재접속 동작 확인용).
    std::size_t acceptedCount() const { return accepted_.load(); }

private:
    bool ensureListening();
    void logListenFailure(const std::string& what);
    void acceptOne();
    void handleReadable();
    void handleLine(const std::string& line);
    void logParseFailure(const std::string& why, const std::string& line);
    void closeConnection();
    void closeListener();
    void run();

    std::string bind_host_;
    uint16_t    port_;

    int listen_fd_ = -1;          // 단말 접속을 받는 대기 소켓 (연결이 끊겨도 유지)
    int conn_fd_   = -1;          // 현재 접속한 단말 소켓
    int backlog_ = 4;
    int select_poll_ms_  = 200;   // select 대기 슬라이스 (stop() 반응 지연 상한)
    int listen_retry_ms_ = 1000;  // bind/listen 실패 시 재시도 간격

    // 한 줄이 이 크기를 넘도록 개행이 안 오면 스트림이 어긋난 것으로 보고 다음 개행까지 버린다.
    static constexpr std::size_t kMaxLineBytes = 4096;

    std::atomic<bool> running_{false};
    std::thread worker_;

    // ── run() 스레드 전용 (락 불필요) ─────────────────────
    std::string buf_;                  // 수신 바이트 누적 버퍼 (개행 단위로 잘라 씀)
    bool discarding_ = false;          // 과대 줄 만난 뒤 다음 개행까지 버리는 중
    bool hello_done_ = false;          // 이 연결에서 hello를 받았는지
    bool listen_error_logged_ = false; // listen 실패 로그 rate limit
    std::chrono::steady_clock::time_point connected_at_{};
    std::size_t last_logged_parse_failure_total_ = 0;
    std::size_t parse_log_interval_ = 100;

    // ── 공유 상태 (run() + 외부 조회 스레드) ───────────────
    mutable std::mutex mtx_;
    SensorUplinkSample latest_{};
    bool               has_sample_ = false;
    std::string        terminal_id_;
    bool               connected_ = false;

    std::atomic<std::size_t> received_{0};
    std::atomic<std::size_t> parse_failures_{0};
    std::atomic<std::size_t> accepted_{0};
};

} // namespace risk_transport
