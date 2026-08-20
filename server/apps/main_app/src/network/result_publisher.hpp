#pragma once
#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unistd.h>

#include <mosquitto.h>

#include "network/mqtt_tls_options.hpp"
#include "logging/logger.hpp"

// ResultPublisher - 판정 결과 MQTT 송신기 (헤더 온리)
//
// [구조 변경 2026-08-03] client(connect) -> server(listen/accept)
// [구조 변경 2026-08-11] TCP -> MQTT
//   단말이 현장에서 이동하며 IP가 바뀌는 문제를 서버가 listen하는 구조로 풀었는데,
//   단말이 여러 대가 되면 "누가 붙어 있는지"를 서버가 직접 관리해야 하고 재연결·
//   재대기 코드가 양쪽에 중복된다. 브로커를 중간에 두면 양쪽 다 publish/subscribe만
//   하면 되므로 소켓 수명 관리가 libmosquitto 안으로 들어간다.
//   -> listen/accept/send 경로를 통째로 걷어내고 mosquitto_publish로 교체했다.
//      큐/백프레셔/드랍 로그 정책은 TCP 시절 그대로 유지한다(전송 수단과 무관한 정책).
//
// [주의] "상태 변화 시 즉시 전송 + 무변화 시 200ms 주기 재전송"(하이브리드 하트비트)은
//        이 클래스가 아니라 ResultDispatcher(result_dispatcher.hpp)에 있다. 이 클래스는
//        dispatcher가 넘겨준 JSON 한 줄을 "보내기만" 한다 - 통신 수단이 바뀌어도
//        판단 로직은 건드릴 필요가 없게 처음부터 분리돼 있었다.
//
// 토픽은 "forklift/risk/<terminal_id>"로 단말마다 갈린다. terminal_id는 서버 실행
// 인자에서 받아 생성자에 전달한다. publish()가 받는 건 이미 직렬화된 JSON
// 문자열이라 여기서 판정 결과 필드를 다시 들여다볼 수 없기 때문인데, JudgmentPipeline과
// idle 프라이밍이 둘 다 같은 설정값을 채워 넣으므로 payload 안의 terminal_id와 항상 같다.

namespace risk_transport {

enum class ResultPublisherRole {
    RiskResult,   // forklift/risk/<terminal_id>
    ServerStatus, // forklift/status/server (one publisher per server process)
};

// 브로커와의 링크 상태.
// MQTT로 바뀌면서 LISTENING(단말 접속 대기)은 의미가 없어져 CONNECTING(브로커 연결
// 시도 중 / 백오프 재연결 대기 중)으로 대체했다.
enum class LinkState {
    DISCONNECTED,  // mosquitto 인스턴스조차 없는 상태 (start() 전 / stop() 후 / 생성 실패)
    CONNECTING,    // 브로커에 붙는 중이거나, 끊긴 뒤 재연결 백오프 대기 중
    CONNECTED      // 브로커와 연결됨 (CONNACK 수신)
};

class ResultPublisher {
public:
    // 판정 결과 토픽 접두사. 뒤에 terminal_id가 붙는다.
    static constexpr const char* kRiskTopicPrefix = "forklift/risk/";

    // 서버 생존 상태 토픽. birth(online) / will(offline)이 같은 토픽을 retain으로
    // 덮어쓰며 "지금 서버가 살아 있는지" 한 값으로 유지한다.
    //
    // LWT(will): 서버 프로세스가 죽어서 keepalive가 끊기면 브로커가 대신 offline을
    //   발행한다 - 서버가 자기 죽음을 알릴 수는 없으므로 브로커에 미리 맡겨두는 용도다.
    // birth(online): 연결에 성공할 때마다 서버가 직접 발행한다. 이게 없으면 죽었다
    //   살아난 뒤에도 retain된 값이 offline으로 남아, 단말은 서버가 멀쩡한데도 계속
    //   offline으로 본다(retain은 새 publish가 오기 전까지 브로커에 그대로 남는다).
    static constexpr const char* kStatusTopic    = "forklift/status/server";
    static constexpr const char* kOfflinePayload = "{\"state\":\"offline\"}";
    static constexpr const char* kOnlinePayload  = "{\"state\":\"online\"}";

    // QoS 0 / retain true.
    // QoS 0: 판정 결과는 최소 200ms마다 갱신되므로(하트비트) 재전송으로 늦게 도착한
    //        낡은 값보다 다음 값이 더 쓸모 있다.
    // retain: 단말이 뒤늦게 구독을 시작해도 브로커가 마지막 위험도를 즉시 내려준다.
    static constexpr int  kQos    = 0;
    static constexpr bool kRetain = true;

    // 재연결 백오프: 3초에서 시작해 실패마다 2배, 30초 상한.
    // Qt 단말 RiskEventSource와 같은 값으로 맞춘 것이라 한쪽만 바꾸면 안 된다.
    static constexpr unsigned int kReconnectDelayMinSec = 3;
    static constexpr unsigned int kReconnectDelayMaxSec = 30;

    // MQTT keepalive(초). 이 시간 동안 트래픽이 없으면 PINGREQ가 나가고, 브로커가
    // 1.5배 동안 아무것도 못 받으면 죽은 것으로 보고 LWT를 발행한다.
    static constexpr int kKeepaliveSec = 60;

    // terminal_id: 토픽을 가르는 단말 식별자 (설정 파일의 forklift.terminal_id).
    // broker_host/broker_port: 브로커 주소. 기본값은 서버와 같은 머신에 브로커를 두는
    //                          현재 배치 기준이며, 다른 머신으로 옮기면 호출부가 넘긴다.
    // tls: 기본값 MqttTlsOptions{}(enabled=false) - 평문 연결 그대로 유지. enabled=true면
    //      connectBroker()가 mosquitto_connect() 전에 mosquitto_tls_set()을 건다.
    explicit ResultPublisher(std::string terminal_id,
                             std::string broker_host = "localhost",
                             uint16_t broker_port = 1883,
                             MqttTlsOptions tls = {},
                             ResultPublisherRole role = ResultPublisherRole::RiskResult)
        : terminal_id_(std::move(terminal_id)),
          broker_host_(std::move(broker_host)),
          broker_port_(broker_port),
          tls_(std::move(tls)),
          manage_server_status_(role == ResultPublisherRole::ServerStatus),
          topic_(manage_server_status_ ? std::string(kStatusTopic)
                                       : std::string(kRiskTopicPrefix) + terminal_id_) {}

    ~ResultPublisher() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        connectBroker();   // 실패해도 계속 간다 (아래 주석 참고)
        worker_ = std::thread(&ResultPublisher::run, this);
    }

    void stop() {
        // 워커 정리는 실제로 돌고 있을 때만 (중복 stop()/join() 방지).
        if (running_.exchange(false)) {
            cv_.notify_all();
            if (worker_.joinable()) worker_.join();
            // 워커가 끝난 뒤에 닫아야 mosq_ 핸들에 대한 경쟁이 없다.
            disconnectBroker();
        }
        // rate limit 때문에 아직 안 찍힌 잔여 드랍을 종료 시점에 한 줄로 요약한다.
        // start() 없이 publish만 한 경우에도(위 if를 안 타도) 잔여분은 남길 수 있게
        // early return 하지 않고 항상 호출한다.
        // 찍은 뒤 last_logged_drop_total_을 갱신하므로 소멸자에서 stop()이 한 번 더
        // 불려도 잔여분이 0이라 중복 출력되지 않는다.
        flushDropLogSummary();
    }

    // 판정 결과 한 건을 송신 큐에 넣는다.
    // 이 채널의 확정 스펙은 "값 변화 시 즉시 전송"이므로 모든 변화가 전달돼야 한다.
    // -> 예전처럼 마지막 값만 덮어쓰지(last-write-wins) 않고 FIFO 큐에 쌓는다.
    // 큐가 가득 차면 백프레셔 정책상 "가장 오래된 항목부터" 버린다
    // (최신 위험도가 더 중요하므로 신규 항목을 버리지 않는다).
    //
    // [주의] 브로커 미연결 구간에도 큐는 계속 쌓인다. 연결이 뒤늦게 붙으면 밀린 결과가
    //        한꺼번에 나가는데, 각 줄에 utc_time이 있으므로 단말이 오래된 줄을 구분할 수 있다.
    //        (여기서 큐를 비워버리면 "연결 직전에 발생한 DANGER"가 통째로 사라져서
    //         안전 측면에서 더 나쁘다고 판단해 유지했다.)
    void publish(const std::string& json) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (queue_.size() >= max_queue_size_) {
                queue_.pop_front();
                // 카운터는 매 드랍마다 증가시키고(droppedCount()는 항상 정확),
                // 로그만 rate limit한다. 수신 측이 오래 느려지면 드랍이 연속으로
                // 발생하는데, 건건이 찍으면 판정 루프까지 느려질 수 있다.
                std::size_t total = ++dropped_overflow_;
                if (total == 1) {
                    // 첫 드랍은 즉시 (백프레셔가 시작됐다는 신호)
                    LOG_WARN(logTag(), logTarget() + " 전송 대기열 초과 (이전 판정값 1건 건너뜀, 누적: " +
                                            std::to_string(total) + ")");
                    last_logged_drop_total_ = total;
                } else if (total - last_logged_drop_total_ >= drop_log_interval_) {
                    // 이후로는 drop_log_interval_건마다 요약 1줄
                    LOG_WARN(logTag(), logTarget() + " 전송 대기열 초과 누적 " +
                                            std::to_string(total - last_logged_drop_total_) +
                                            "건 (누적: " + std::to_string(total) + ")");
                    last_logged_drop_total_ = total;
                }
            }
            queue_.push_back(json);
        }
        cv_.notify_one();
    }

    LinkState state() const { return state_.load(); }

    void onStateChange(std::function<void(LinkState)> cb) { onStateChange_ = std::move(cb); }

    // 이 퍼블리셔가 결과를 내보내는 토픽 ("forklift/risk/<terminal_id>").
    const std::string& topic() const { return topic_; }

    // 큐 넘침으로 버려진 누적 건수 (백프레셔 발생 여부 추적용)
    std::size_t droppedCount() const { return dropped_overflow_.load(); }

    // publish 실패로 버려진 누적 건수. 큐 넘침과 원인이 달라 따로 센다.
    std::size_t sendFailureCount() const { return send_failures_.load(); }

    // 브로커에 연결된 누적 횟수 (재연결 동작 확인용).
    std::size_t connectedCount() const { return connected_.load(); }

private:
    // ── libmosquitto 전역 초기화 ────────────────────────────────
    // mosquitto_lib_init()/cleanup()은 프로세스 전역이고 refcount가 없어서, 인스턴스가
    // 여럿일 때 먼저 죽는 쪽이 cleanup을 불러버리면 남은 쪽이 망가진다. 여기서 직접 센다.
    static std::mutex& libMutex() { static std::mutex m; return m; }
    static int& libRefCount() { static int n = 0; return n; }

    static void libRetain() {
        std::lock_guard<std::mutex> lk(libMutex());
        if (libRefCount()++ == 0) mosquitto_lib_init();
    }
    static void libRelease() {
        std::lock_guard<std::mutex> lk(libMutex());
        if (--libRefCount() == 0) mosquitto_lib_cleanup();
    }

    // 종료 시점에 아직 로그로 안 남은 드랍 잔여분을 요약 1줄로 남긴다.
    // 잔여분이 없으면 아무것도 찍지 않으므로 여러 번 불려도 안전하다.
    void flushDropLogSummary() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::size_t total = dropped_overflow_.load();
        if (total > last_logged_drop_total_) {
            LOG_WARN(logTag(), logTarget() + " 전송 건너뜀 요약 (추가: " +
                                    std::to_string(total - last_logged_drop_total_) +
                                    "건, 누적: " + std::to_string(total) + ")");
            last_logged_drop_total_ = total;
        }
    }

    const char* logTag() const {
        return manage_server_status_ ? "SERVER" : "RISK";
    }

    std::string logTarget() const {
        return manage_server_status_ ? std::string("서버 상태")
                                     : "[" + terminal_id_ + "] 위험 경보";
    }

    // [주의] mosquitto 콜백(네트워크 스레드)에서도 불린다. onStateChange_는 start() 전에
    //        한 번만 등록하는 규약이라 교체 경쟁은 없다.
    void setState(LinkState s) {
        if (state_.exchange(s) != s && onStateChange_) onStateChange_(s);
    }

    // ── mosquitto 콜백 (네트워크 스레드에서 불린다) ─────────────
    static void onConnectCb(struct mosquitto*, void* obj, int rc) {
        auto* self = static_cast<ResultPublisher*>(obj);
        if (rc == 0) {
            ++self->connected_;
            LOG_INFO(self->logTag(), self->logTarget() + " 송신 연결 완료 (" + self->broker_host_ + ":" +
                                      std::to_string(self->broker_port_) + ", 토픽: " + self->topic_ + ")");
            if (self->manage_server_status_) self->publishStatus(kOnlinePayload);
            self->setState(LinkState::CONNECTED);
        } else {
            LOG_WARN(self->logTag(), self->logTarget() + " 송신 브로커 연결 거부 (사유: " +
                                      std::string(mosquitto_connack_string(rc)) + ")");
            self->setState(LinkState::CONNECTING);
        }
    }

    static void onDisconnectCb(struct mosquitto*, void* obj, int rc) {
        auto* self = static_cast<ResultPublisher*>(obj);
        if (rc != 0 && self->running_.load()) {
            LOG_WARN(self->logTag(), self->logTarget() + " 통신 끊김 (자동 재연결 대기)");
            self->setState(LinkState::CONNECTING);
        } else {
            self->setState(LinkState::DISCONNECTED);
        }
    }

    // 브로커 연결을 준비하고 네트워크 스레드를 띄운다.
    //
    // 초기 mosquitto_connect()가 실패해도(브로커가 아직 안 떴다 등) false를 돌려주고
    // 끝내지 않는다: loop_start 스레드가 재연결 백오프로 계속 재시도하므로, 서버는
    // 죽지 않고 브로커가 뜨는 순간 자동으로 붙는다 (TCP 시절 bind 실패 시 재시도하던
    // 동작과 같은 성격이고, 재시도 주기를 libmosquitto가 대신 관리한다).
    bool connectBroker() {
        libRetain();

        // 클라이언트 ID에 PID를 포함해 프로세스 간 ID 충돌 및 강제 연결 끊김(rc=7)을 방지한다.
        const std::string client_id = "forklift-server-" + terminal_id_ + "-" + std::to_string(::getpid());
        mosq_ = mosquitto_new(client_id.c_str(), /*clean_session=*/true, this);
        if (mosq_ == nullptr) {
            LOG_ERROR(logTag(), logTarget() + " 초기화 실패 (mosquitto_new -> 송신 비활성)");
            libRelease();
            setState(LinkState::DISCONNECTED);
            return false;
        }

        // LWT는 반드시 connect 전에 걸어야 CONNECT 패킷에 실려 브로커에 등록된다.
        if (manage_server_status_) {
            mosquitto_will_set(mosq_, kStatusTopic,
                               static_cast<int>(std::strlen(kOfflinePayload)), kOfflinePayload,
                               kQos, kRetain);
        }
        mosquitto_reconnect_delay_set(mosq_, kReconnectDelayMinSec, kReconnectDelayMaxSec,
                                      /*reconnect_exponential_backoff=*/true);

        // TLS/mTLS는 mosquitto_connect() 전에 걸어야 한다 - 이후에 부르면 다음 연결부터만
        // 적용되고 이번 시도는 평문으로 나간다. 실패 시 여기서 포기하는 이유: 인증서 경로가
        // 잘못됐는데 평문으로 폴백하면 "TLS를 켰다고 착각한 채 실제로는 안 걸린" 상태가
        // 조용히 만들어진다 - 그건 켜는 것보다 나쁘다.
        if (tls_.enabled) {
            int trc = mosquitto_tls_set(mosq_,
                                        tls_.ca_cert_path.c_str(),
                                        /*capath=*/nullptr,
                                        tls_.client_cert_path.c_str(),
                                        tls_.client_key_path.c_str(),
                                        /*pw_callback=*/nullptr);
            if (trc != MOSQ_ERR_SUCCESS) {
                LOG_ERROR(logTag(), logTarget() + " TLS 설정 실패 (사유: " +
                                        std::string(mosquitto_strerror(trc)) +
                                        " -> 송신 비활성)");
                mosquitto_destroy(mosq_);
                mosq_ = nullptr;
                libRelease();
                setState(LinkState::DISCONNECTED);
                return false;
            }
        }

        mosquitto_connect_callback_set(mosq_, &ResultPublisher::onConnectCb);
        mosquitto_disconnect_callback_set(mosq_, &ResultPublisher::onDisconnectCb);

        setState(LinkState::CONNECTING);

        int rc = mosquitto_connect(mosq_, broker_host_.c_str(), broker_port_, kKeepaliveSec);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG_WARN(logTag(), logTarget() + " 브로커 초기 연결 실패 (" +
                                  broker_host_ + ":" + std::to_string(broker_port_) + ", 사유: " +
                                  mosquitto_strerror(rc) + " -> 백오프 재연결)");
        }

        rc = mosquitto_loop_start(mosq_);
        if (rc != MOSQ_ERR_SUCCESS) {
            // 네트워크 스레드가 안 뜨면 재연결도 publish도 불가능하므로 여기서만 포기한다.
            LOG_ERROR(logTag(), logTarget() + " 네트워크 스레드 시작 실패 (" +
                                     std::string(mosquitto_strerror(rc)) + " -> 송신 비활성)");
            mosquitto_destroy(mosq_);
            mosq_ = nullptr;
            libRelease();
            setState(LinkState::DISCONNECTED);
            return false;
        }
        return true;
    }

    void disconnectBroker() {
        if (mosq_ == nullptr) return;
        // 정상 종료다. MQTT 규약상 DISCONNECT를 보내고 끊으면 브로커는 LWT를 발행하지
        // 않는다(LWT는 "예기치 않게 죽었을 때"만 나가는 게 맞다) - 그래서 계획된 종료는
        // 여기서 offline을 직접 남겨야 birth/will 쌍이 완성된다. 안 그러면 retain 값이
        // online인 채로 굳어서 단말이 꺼진 서버를 계속 살아 있는 것으로 본다.
        // DISCONNECT보다 먼저 큐에 넣어야 한다 - out 패킷은 FIFO로 나가므로 이 순서면
        // PUBLISH가 먼저 전송되고, 아래 loop_stop(force=false)이 네트워크 스레드가
        // 다 비우고 끝날 때까지 기다린다.
        if (manage_server_status_) publishStatus(kOfflinePayload);
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, /*force=*/true);   // 네트워크 스레드 즉시 종료
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        libRelease();
        setState(LinkState::DISCONNECTED);
    }

    // 서버 생존 상태(kStatusTopic)를 발행한다. will과 같은 QoS/retain을 써야 두 값이
    // 같은 retain 슬롯을 서로 덮어쓰며 일관되게 유지된다.
    // [주의] on_connect 콜백(네트워크 스레드)에서 불린다 - mosquitto_publish는 스레드
    //        안전하고 콜백 안에서의 호출도 허용된다.
    void publishStatus(const char* payload) {
        if (mosq_ == nullptr) return;
        int rc = mosquitto_publish(mosq_, /*mid=*/nullptr, kStatusTopic,
                                   static_cast<int>(std::strlen(payload)), payload,
                                   kQos, kRetain);
        if (rc != MOSQ_ERR_SUCCESS) {
            // 실패해도 판정 결과 송신은 계속한다. 다음 재연결 때 다시 시도된다.
            LOG_WARN("SERVER", "서버 상태 송신 실패 (토픽: " + std::string(kStatusTopic) +
                                ", 사유: " + mosquitto_strerror(rc) + ")");
        }
    }

    // 한 건 publish. payload는 상류(ResultDispatcher가 부른 toJson)가 만든 JSON 그대로다.
    // TCP 시절 붙이던 '\n' 줄 구분자는 떼어냈다 - MQTT는 메시지 경계를 프로토콜이
    // 보장하므로 줄바꿈이 payload에 그대로 섞여 들어갈 뿐이다.
    bool publishOnce(const std::string& json) {
        if (mosq_ == nullptr) return false;
        int rc = mosquitto_publish(mosq_, /*mid=*/nullptr, topic_.c_str(),
                                   static_cast<int>(json.size()), json.data(),
                                   kQos, kRetain);
        if (rc != MOSQ_ERR_SUCCESS) {
            last_publish_error_ = rc;
            return false;
        }
        return true;
    }

    void run() {
        while (running_) {
            // ── 브로커 미연결 구간 ────────────────────────────────
            // 재연결은 libmosquitto 네트워크 스레드가 백오프를 두고 알아서 한다.
            // 여기서는 큐를 비우지 않고 그대로 쌓아 둔다(publish() 주석 참고) -
            // 미연결 상태로 꺼내서 publish하면 그 건은 그냥 사라진다.
            if (state_.load() != LinkState::CONNECTED) {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(idle_poll_ms_),
                             [this] { return !running_.load(); });
                continue;
            }

            // ── 연결 구간: 큐에서 가장 오래된 항목 하나를 꺼내 전송 (FIFO 순서 보장) ──
            std::string toSend;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(idle_poll_ms_),
                             [this] { return !queue_.empty() || !running_.load(); });
                if (!running_) break;
                if (queue_.empty()) continue;
                toSend = std::move(queue_.front());
                queue_.pop_front();
            }

            if (!publishOnce(toSend)) {
                // 이미 큐에서 꺼낸 뒤라 이 한 건은 유실된다. 재큐잉하지 않는 이유:
                // 다시 넣으면 낡은 결과가 그 사이 들어온 최신 결과보다 뒤에 붙어 나가
                // 단말이 위험도를 거꾸로 보게 된다. 연결이 돌아오면 다음 판정 결과
                // (늦어도 200ms 하트비트)부터 정상 전송된다.
                std::size_t total = ++send_failures_;
                LOG_WARN(logTag(), logTarget() + " 전송 실패로 판정값 1건 건너뜀 (사유: " +
                                      std::string(mosquitto_strerror(last_publish_error_)) +
                                      ", 누적: " + std::to_string(total) + ")");
            }
        }
    }

    std::string terminal_id_;
    std::string broker_host_;
    uint16_t    broker_port_;
    MqttTlsOptions tls_;
    // 서버 상태 publisher는 명시적으로 ServerStatus 역할을 받은 인스턴스만 맡는다.
    // 기본값을 서버 상태로 두면 단말 publisher 생성 경로가 누락됐을 때
    // 첫 단말이 서버 상태를 대신 발행하는 단일 단말 가정이 다시 생길 수 있다.
    bool manage_server_status_ = false;
    std::string topic_;                  // "forklift/risk/<terminal_id>" (생성자에서 1회 계산)

    struct mosquitto* mosq_ = nullptr;   // libmosquitto 핸들 (start()~stop() 동안만 유효)
    int last_publish_error_ = 0;         // 마지막 mosquitto_publish 실패 코드 (로그용)

    int idle_poll_ms_ = 100;             // 큐가 빈 동안의 대기 슬라이스 (stop() 반응 지연 상한)

    std::atomic<bool> running_{false};
    std::atomic<LinkState> state_{LinkState::DISCONNECTED};
    std::thread worker_;

    // 송신 대기 큐 (FIFO). mtx_로 보호된다.
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;

    // 큐 최대 길이. 초과 시 가장 오래된 항목부터 버린다.
    // 100건 = 판정 주기를 고려하면 수 초 분량 버퍼로, 브로커가 잠시 느려져도 흡수 가능.
    std::size_t max_queue_size_ = 100;

    std::atomic<std::size_t> dropped_overflow_{0};
    std::atomic<std::size_t> send_failures_{0};
    std::atomic<std::size_t> connected_{0};

    // 드랍 로그 rate limit 상태 (publish() 안에서만 접근하므로 mtx_로 보호됨).
    // 첫 1건은 즉시 찍고, 그 뒤로는 마지막 로그 이후 drop_log_interval_건이
    // 쌓일 때마다 요약 1줄만 찍는다.
    std::size_t last_logged_drop_total_ = 0;
    std::size_t drop_log_interval_ = 100;

    std::function<void(LinkState)> onStateChange_;
};

} // namespace risk_transport
