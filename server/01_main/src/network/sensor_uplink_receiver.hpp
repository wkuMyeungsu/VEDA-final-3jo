#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <map>

#include "network/mqtt_tls_options.hpp"

struct mosquitto;
struct mosquitto_message;

// SensorUplinkReceiver - 단말->서버 센서 업링크 수신기 (MQTT 구독)
//
// [배경] 9000(ResultPublisher)/9001(CameraAssignmentServer)은 둘 다 서버->단말 방향의
// TCP 채널이다. 이 채널은 반대 방향으로, 단말(forklift-device)에 붙은 IMU/ToF 원시값을
// 서버가 받는다. 센서와 판정 엔진이 물리적으로 다른 보드(단말 RPi4 / veda3)에 있어서
// 프로세스 내부 결선(SensorCollectorReader)으로는 값을 넘길 수 없기 때문에 생긴 채널이다.
//
// [전송 방식] 원래 9002 TCP + NDJSON + hello 핸드셰이크였으나, 서버 측 수신 경로가
// "최신값 캐시" 방식으로 팀 확정되면서 MQTT 구독으로 교체했다(박명수 확인 완료).
// 브로커(mosquitto)가 재접속/유지 로직을 대신 해주므로 이 클래스에는 더 이상
// listen/accept/프레이밍/hello 코드가 없다 - "누가 보낸 값인가"는 hello 대신
// MQTT 토픽에서 바로 얻는다.
//
// 프로토콜:
//   토픽: forklift/sensor/<terminal_id>  (구독은 forklift/sensor/+ 와일드카드 1개)
//   payload(JSON, 단말 -> 서버, 주기적):
//     {"camera_id":"cam0","tof_ok":true,"tof_distance_mm":420,"imu_ok":true,
//      "imu_accel_x_g":0.02,"imu_accel_y_g":-0.01,"imu_accel_z_g":1.01,
//      "ts_ms":1754380800123}
//   QoS 0, retain 없음 - 어차피 "최신값 캐시"라 유실된 한 건은 다음 주기 값이 곧바로
//   덮어쓴다. QoS 1/2의 재전송/중복제거 비용을 들일 이유가 없다.
//
// terminal_id는 이제 payload가 아니라 토픽에서 온다(payload 스키마 자체는 기존 TCP
// 버전과 동일 - 파싱 로직을 그대로 재사용하기 위해 일부러 안 건드렸다).
//
// [수신 경로] 서버가 이 값을 어디로 넣을지(명수님 이벤트 큐 vs 최신값 캐시)는 "최신값
// 캐시" 쪽으로 확정됐다. 수신 콜백이 단말별 최신 스냅샷을 갱신해두고, 판정 루프가
// 아무 때나 getLatest(terminal_id)로 꺼내 쓰는 구조. 여러 단말이 같은 브로커에 붙을 수
// 있어 캐시는 terminal_id별로 따로 둔다(단일 지게차 데모에서는 사실상 항상 1건).
//
// 프로젝트에 링크되는 JSON 라이브러리가 없어(third_party/nlohmann은 server_config
// 내부 전용) CameraAssignmentServer와 같은 방식의 문자열 검색 기반 최소 파서를 쓴다.
// 깨진 payload를 만나도 연결을 끊지 않고(끊을 "연결"이라는 개념 자체가 이제 없다)
// 그 메시지만 버린다.

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

    // 서버가 이 메시지를 실제로 파싱한 시각. steady_clock이라 시스템 시각이 조정돼도
    // 경과 시간 계산이 뒤로 가지 않는다 -> isStale() 판단의 유일한 기준.
    std::chrono::steady_clock::time_point received_at{};
};

class SensorUplinkReceiver {
public:
    // 마지막 수신 이후 이만큼 지나면 isStale() == true.
    // risk_event 채널의 하이브리드 하트비트(200ms 주기 재전송, 타임아웃 1000~1500ms 제안)를
    // 그대로 재사용할지가 아직 미협의라 그 범위 가운데값으로 잠정 고정했다.
    // 확정되면 이 상수만 바꾸면 된다(호출부는 인자를 생략하는 것을 기본으로 한다).
    static constexpr int kDefaultStaleTimeoutMs = 1200;

    // 로컬 mosquitto 브로커 기본 주소/포트. 배포 환경의 실제 브로커 주소가 확정되면
    // 생성자 인자로 덮어쓴다.
    static constexpr const char* kDefaultBrokerHost = "127.0.0.1";
    static constexpr int kDefaultBrokerPort = 1883;

    // 구독 토픽. 단말별 토픽(forklift/sensor/<terminal_id>)을 와일드카드 1개로 전부 받는다.
    static constexpr const char* kSubscribeTopic = "forklift/sensor/+";

    // tls: 기본값 MqttTlsOptions{}(enabled=false) - 평문 연결 그대로 유지. enabled=true면
    //      start()가 mosquitto_connect_async() 전에 mosquitto_tls_set()을 건다
    //      (ResultPublisher와 같은 규약).
    explicit SensorUplinkReceiver(std::string broker_host = kDefaultBrokerHost,
                                  int broker_port = kDefaultBrokerPort,
                                  MqttTlsOptions tls = {});
    ~SensorUplinkReceiver();

    SensorUplinkReceiver(const SensorUplinkReceiver&) = delete;
    SensorUplinkReceiver& operator=(const SensorUplinkReceiver&) = delete;

    // 브로커에 비동기 접속하고 mosquitto 내부 네트워크 스레드를 띄운다.
    void start();
    void stop();

    // terminal_id가 보낸 가장 최근 정상 파싱된 스냅샷을 복사해 준다.
    // 그 단말에서 한 번도 못 받았으면 false를 돌려주고 out은 건드리지 않는다
    // (기본값 0을 "0mm 밀착"으로 오독하는 사고를 구조적으로 막기 위해 out param 방식).
    bool getLatest(const std::string& terminal_id, SensorUplinkSample& out) const;

    // 그 terminal_id로 유효한 스냅샷을 한 번이라도 받았는지.
    // isStale()만으로는 "아직 한 번도 못 받음"과 "받았는데 끊긴 지 오래됨"을 구분할 수 없어
    // 로깅/UI용으로 따로 노출한다 (판정 정책은 둘을 똑같이 취급해야 한다 - isStale() 주석 참고).
    bool hasSample(const std::string& terminal_id) const;

    // 그 terminal_id의 마지막 수신 이후 timeout_ms를 넘었으면 true.
    //
    // [설계 결정] 아직 한 번도 못 받은 상태는 항상 true다 (기존 TCP 버전과 동일 정책).
    //   판정 엔진은 센서값을 못 받을 때 SAFE로 낙관하지 않고 SENSOR_FAULT -> 최소 CAUTION을
    //   유지하는 fail-safe 정책을 쓴다. 여기서 "아직 데이터 없음"을 fresh로 돌려주면 호출부가
    //   기본값 스냅샷(tof_distance_mm=0)을 진짜 측정값으로 오해해 근거 없는 DANGER를 내거나,
    //   반대로 조용히 SAFE로 흘려보낼 수 있다. 둘 다 안전 측면에서 나쁘므로
    //   "받은 게 없으면 stale"로 통일했다.
    //   (ResultDispatcher의 idle 프라이밍이 distance_m을 0.0이 아니라 null로 내보내는 것과
    //    같은 원칙이다.)
    bool isStale(const std::string& terminal_id, int timeout_ms = kDefaultStaleTimeoutMs) const;

    // MQTT 브로커에 현재 연결돼 있는지(구독 세션 전체 단위 - TCP 버전의 "단말 접속"과
    // 달리 특정 단말과는 무관하다. 어느 단말이 값을 보내는지는 이제 토픽으로 구분된다).
    bool isConnected() const { return connected_.load(); }

    // 정상 파싱해 캐시에 반영한 누적 건수(전체 단말 합산).
    std::size_t receivedCount() const { return received_.load(); }

    // 파싱 실패로 버린 누적 메시지 수(깨진 JSON / 필드 누락 / 토픽 형식 오류).
    std::size_t parseFailureCount() const { return parse_failures_.load(); }

private:
    static void onConnectTrampoline(mosquitto* mosq, void* userdata, int rc);
    static void onDisconnectTrampoline(mosquitto* mosq, void* userdata, int rc);
    static void onMessageTrampoline(mosquitto* mosq, void* userdata, const mosquitto_message* msg);

    void onConnect(int rc);
    void onDisconnect(int rc);
    void onMessage(const mosquitto_message* msg);
    void logParseFailure(const std::string& why, const std::string& topic, const std::string& payload);

    std::string broker_host_;
    int         broker_port_;
    MqttTlsOptions tls_;

    mosquitto* mosq_ = nullptr;

    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    // ── 공유 상태 (mosquitto 내부 네트워크 스레드 + 외부 조회 스레드) ───────────
    mutable std::mutex mtx_;
    std::map<std::string, SensorUplinkSample> latest_by_terminal_;

    std::atomic<std::size_t> received_{0};
    std::atomic<std::size_t> parse_failures_{0};

    // 파싱 실패 stderr rate limit (ResultPublisher/기존 TCP 버전과 동일 정책).
    std::size_t last_logged_parse_failure_total_ = 0;
    static constexpr std::size_t kParseLogInterval = 100;
};

} // namespace risk_transport
