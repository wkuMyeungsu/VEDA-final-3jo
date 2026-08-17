// sensor_uplink_receiver.cpp
// 선언·설계 메모는 sensor_uplink_receiver.hpp 참고.
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// libmosquitto의 mosquitto_loop_start()가 내부 네트워크 스레드를 대신 띄워주므로
// (이전 TCP 버전처럼) 이 클래스가 직접 소켓/select 루프를 들고 있지 않는다.
// 재접속도 mosquitto_reconnect_delay_set()으로 라이브러리에 맡긴다.
//
// JSON 필드 파서(extractString/extractBool/extractDouble/extractInt64, parseSample)는
// 기존 TCP 버전(sensor_uplink_receiver.cpp git 이력)의 것을 그대로 재사용했다 - payload
// 스키마가 안 바뀌었으므로 손댈 이유가 없다. 바뀐 건 "이 값을 어디서 얻는가"(recv 버퍼의
// 한 줄 -> MQTT 메시지 payload)와 "누가 보낸 값인가"(hello로 받은 terminal_id -> 토픽에서
// 파싱한 terminal_id)뿐이다.

#include "network/sensor_uplink_receiver.hpp"

#include <iostream>
#include <cstdlib>
#include <mosquitto.h>

namespace risk_transport {

namespace {

// ── 최소 JSON 필드 파서 (기존 TCP 버전 재사용, 로직 변경 없음) ──────────────
// CameraAssignmentServer::extractJsonStringField()와 같은 방식의 문자열 검색 파서다.
// 한 줄짜리 평면 오브젝트(중첩 없음, 이스케이프 없음)만 다루는 전제이며, 그 밖의 입력은
// "파싱 실패"로 떨어져 호출부가 그 메시지를 버린다 - 즉 헐거운 파서라도 오탐이 사고로
// 이어지지 않는다.

std::size_t findValueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) return std::string::npos;
    auto colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string::npos) return std::string::npos;
    auto value_pos = json.find_first_not_of(" \t", colon_pos + 1);
    return value_pos;   // npos면 콜론 뒤가 비어 있음 -> 호출부가 실패로 처리
}

bool extractString(const std::string& json, const std::string& key, std::string& out) {
    auto pos = findValueStart(json, key);
    if (pos == std::string::npos || json[pos] != '"') return false;
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return false;
    out = json.substr(pos + 1, end - pos - 1);
    return true;
}

bool extractBool(const std::string& json, const std::string& key, bool& out) {
    auto pos = findValueStart(json, key);
    if (pos == std::string::npos) return false;
    if (json.compare(pos, 4, "true") == 0)  { out = true;  return true; }
    if (json.compare(pos, 5, "false") == 0) { out = false; return true; }
    return false;
}

// 숫자 값 하나를 읽는다. strtod가 한 글자도 못 먹었으면(=숫자가 아님) 실패로 본다.
bool extractDouble(const std::string& json, const std::string& key, double& out) {
    auto pos = findValueStart(json, key);
    if (pos == std::string::npos) return false;
    const char* begin = json.c_str() + pos;
    char* end = nullptr;
    double v = std::strtod(begin, &end);
    if (end == begin) return false;
    out = v;
    return true;
}

bool extractInt64(const std::string& json, const std::string& key, int64_t& out) {
    auto pos = findValueStart(json, key);
    if (pos == std::string::npos) return false;
    const char* begin = json.c_str() + pos;
    char* end = nullptr;
    long long v = std::strtoll(begin, &end, 10);
    if (end == begin) return false;
    out = static_cast<int64_t>(v);
    return true;
}

// 센서 메시지 파싱. 스키마의 7개 필드(camera_id/tof_ok/tof_distance_mm/imu_ok/
// imu_accel_x_g/imu_accel_y_g/imu_accel_z_g/ts_ms - 8개)를 전부 요구하고, 하나라도
// 없거나 형식이 어긋나면 why에 이유를 담아 false. 일부 필드만 채운 반쪽 스냅샷을
// 캐시에 올리면 판정이 근거 없는 값으로 돌아가므로 "전부 아니면 버림"으로 간다.
// terminal_id는 이제 payload가 아니라 MQTT 토픽에서 오므로 여기서 다루지 않는다.
bool parseSample(const std::string& payload, SensorUplinkSample& out, std::string& why) {
    auto first = payload.find_first_not_of(" \t\r\n");
    auto last  = payload.find_last_not_of(" \t\r\n");
    if (first == std::string::npos || payload[first] != '{' || payload[last] != '}') {
        why = "JSON 오브젝트 형식이 아님";
        return false;
    }

    SensorUplinkSample s;
    if (!extractString(payload, "camera_id", s.camera_id))          { why = "camera_id 누락/형식오류";       return false; }
    if (!extractBool(payload, "tof_ok", s.tof_ok))                  { why = "tof_ok 누락/형식오류";          return false; }
    if (!extractBool(payload, "imu_ok", s.imu_ok))                  { why = "imu_ok 누락/형식오류";          return false; }

    double tof_mm = 0.0;
    if (!extractDouble(payload, "tof_distance_mm", tof_mm))         { why = "tof_distance_mm 누락/형식오류"; return false; }
    s.tof_distance_mm = static_cast<int>(tof_mm);

    if (!extractDouble(payload, "imu_accel_x_g", s.imu_accel_x_g))  { why = "imu_accel_x_g 누락/형식오류";   return false; }
    if (!extractDouble(payload, "imu_accel_y_g", s.imu_accel_y_g))  { why = "imu_accel_y_g 누락/형식오류";   return false; }
    if (!extractDouble(payload, "imu_accel_z_g", s.imu_accel_z_g))  { why = "imu_accel_z_g 누락/형식오류";   return false; }
    if (!extractInt64(payload, "ts_ms", s.ts_ms))                   { why = "ts_ms 누락/형식오류";           return false; }

    out = s;
    return true;
}

// 토픽 "forklift/sensor/<terminal_id>"에서 terminal_id만 뽑아낸다.
// 와일드카드 구독(forklift/sensor/+)이라 형식이 어긋난 토픽(하위 레벨이 더 있거나 접두사가
// 다름)이 들어올 일은 이론상 없지만, 브로커 설정이 잘못돼 다른 토픽이 섞이는 경우를 대비해
// 방어적으로 검사한다.
bool extractTerminalIdFromTopic(const std::string& topic, std::string& terminal_id_out) {
    static const std::string kPrefix = "forklift/sensor/";
    if (topic.size() <= kPrefix.size()) return false;
    if (topic.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    std::string rest = topic.substr(kPrefix.size());
    if (rest.empty() || rest.find('/') != std::string::npos) return false;   // 하위 레벨 있으면 거부
    terminal_id_out = rest;
    return true;
}

// libmosquitto 전역 라이브러리 상태(mosquitto_lib_init/cleanup)는 프로세스 전체에서
// 공유된다. 테스트가 SensorUplinkReceiver 인스턴스를 여러 개 순차 생성/파괴하므로,
// 참조 카운트로 감싸 "마지막 인스턴스가 파괴될 때만 cleanup"되게 한다 - 그렇지 않으면
// 동시에 살아 있는 다른 인스턴스가 있는 상태에서 cleanup이 불려 그쪽 OpenSSL 상태 등이
// 깨질 수 있다.
std::mutex g_mosq_lib_mtx;
int g_mosq_lib_refcount = 0;

void mosqLibAcquire() {
    std::lock_guard<std::mutex> lk(g_mosq_lib_mtx);
    if (g_mosq_lib_refcount++ == 0) mosquitto_lib_init();
}

void mosqLibRelease() {
    std::lock_guard<std::mutex> lk(g_mosq_lib_mtx);
    if (--g_mosq_lib_refcount == 0) mosquitto_lib_cleanup();
}

} // namespace

SensorUplinkReceiver::SensorUplinkReceiver(std::string broker_host, int broker_port,
                                            MqttTlsOptions tls)
    : broker_host_(std::move(broker_host)), broker_port_(broker_port), tls_(std::move(tls)) {
    mosqLibAcquire();
}

SensorUplinkReceiver::~SensorUplinkReceiver() {
    stop();
    mosqLibRelease();
}

void SensorUplinkReceiver::onConnectTrampoline(mosquitto*, void* userdata, int rc) {
    static_cast<SensorUplinkReceiver*>(userdata)->onConnect(rc);
}

void SensorUplinkReceiver::onDisconnectTrampoline(mosquitto*, void* userdata, int rc) {
    static_cast<SensorUplinkReceiver*>(userdata)->onDisconnect(rc);
}

void SensorUplinkReceiver::onMessageTrampoline(mosquitto*, void* userdata, const mosquitto_message* msg) {
    static_cast<SensorUplinkReceiver*>(userdata)->onMessage(msg);
}

void SensorUplinkReceiver::onConnect(int rc) {
    if (rc != 0) {
        // 연결 자체가 브로커에게 거부됨(인증/프로토콜 등) - 이 경우는 재접속을 반복해도
        // 안 되는 사유일 수 있어 눈에 띄게 남긴다. 네트워크 단절로 인한 재접속 시도는
        // mosquitto 내부에서 조용히 반복되므로 여기 안 찍힌다(rc==0으로만 불림).
        std::cerr << "[SensorUplinkReceiver] MQTT 연결 실패 (rc=" << rc << ": "
                  << mosquitto_connack_string(rc) << ")\n";
        return;
    }
    connected_ = true;
    std::cerr << "[SensorUplinkReceiver] MQTT 연결됨 - " << broker_host_ << ':' << broker_port_
              << " (구독: " << kSubscribeTopic << ")\n";

    const int sub_rc = mosquitto_subscribe(mosq_, nullptr, kSubscribeTopic, /*qos=*/0);
    if (sub_rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[SensorUplinkReceiver] 구독 실패 (" << mosquitto_strerror(sub_rc) << ")\n";
    }
}

void SensorUplinkReceiver::onDisconnect(int rc) {
    connected_ = false;
    if (rc != 0) {
        // rc==0이면 stop()에 의한 의도적 disconnect. 그 외에는 네트워크 단절 -
        // mosquitto_reconnect_delay_set()으로 설정한 재접속이 내부적으로 시작된다.
        std::cerr << "[SensorUplinkReceiver] MQTT 연결 끊김 (rc=" << rc
                  << ") - 재접속 대기 중\n";
    }
}

void SensorUplinkReceiver::logParseFailure(const std::string& why, const std::string& topic,
                                            const std::string& payload) {
    // 카운터는 매번 정확히 증가시키고 stderr만 rate limit한다. 단말이 깨진 메시지를 계속
    // 흘리면 건건이 찍는 순간 unbuffered stderr가 mosquitto 콜백 스레드를 붙잡는다
    // (ResultPublisher의 드랍 로그와 같은 이유·같은 정책).
    const std::size_t total = ++parse_failures_;
    if (total == 1) {
        std::cerr << "[SensorUplinkReceiver] 파싱 실패로 1건 버림 (" << why
                  << ") - 수신 계속 (누적 실패=" << total << ", topic=" << topic
                  << ", payload=" << payload << ")\n";
        last_logged_parse_failure_total_ = total;
    } else if (total - last_logged_parse_failure_total_ >= kParseLogInterval) {
        std::cerr << "[SensorUplinkReceiver] dropped " << (total - last_logged_parse_failure_total_)
                  << " more (total: " << total << ") - 마지막 사유: " << why << "\n";
        last_logged_parse_failure_total_ = total;
    }
}

void SensorUplinkReceiver::onMessage(const mosquitto_message* msg) {
    if (!msg || !msg->topic) return;
    const std::string topic(msg->topic);

    std::string terminal_id;
    if (!extractTerminalIdFromTopic(topic, terminal_id)) {
        logParseFailure("토픽 형식이 forklift/sensor/<terminal_id>가 아님", topic, "");
        return;
    }

    const std::string payload(msg->payload ? static_cast<const char*>(msg->payload) : "",
                               msg->payload ? static_cast<std::size_t>(msg->payloadlen) : 0);

    SensorUplinkSample sample;
    std::string why;
    if (!parseSample(payload, sample, why)) {
        // 깨진 메시지 하나 때문에 구독을 끊거나 죽으면 안 된다 - 그 메시지만 버리고 계속 받는다.
        logParseFailure(why, topic, payload);
        return;
    }

    sample.received_at = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        latest_by_terminal_[terminal_id] = sample;
    }
    ++received_;
}

void SensorUplinkReceiver::start() {
    if (running_.exchange(true)) return;

    mosq_ = mosquitto_new(/*id=*/nullptr, /*clean_session=*/true, this);
    if (!mosq_) {
        std::cerr << "[SensorUplinkReceiver] mosquitto_new() 실패\n";
        running_ = false;
        return;
    }

    mosquitto_connect_callback_set(mosq_, &SensorUplinkReceiver::onConnectTrampoline);
    mosquitto_disconnect_callback_set(mosq_, &SensorUplinkReceiver::onDisconnectTrampoline);
    mosquitto_message_callback_set(mosq_, &SensorUplinkReceiver::onMessageTrampoline);

    // 끊긴 뒤 재접속 지연: 3초에서 시작해 최대 30초까지 지수적으로 늘어남(마지막 true).
    // 단말/브로커가 잠깐 흔들려도 재접속 폭주로 브로커를 두들기지 않게 하기 위함.
    mosquitto_reconnect_delay_set(mosq_, 3, 30, true);

    // TLS/mTLS는 connect 전에 걸어야 한다(ResultPublisher::connectBroker()와 같은 이유).
    // 실패하면 평문으로 폴백하지 않고 여기서 포기한다 - "TLS 켰다고 착각했는데 실제로는
    // 안 걸린" 상태를 만들지 않기 위함.
    if (tls_.enabled) {
        const int trc = mosquitto_tls_set(mosq_,
                                          tls_.ca_cert_path.c_str(),
                                          /*capath=*/nullptr,
                                          tls_.client_cert_path.c_str(),
                                          tls_.client_key_path.c_str(),
                                          /*pw_callback=*/nullptr);
        if (trc != MOSQ_ERR_SUCCESS) {
            std::cerr << "[SensorUplinkReceiver] mosquitto_tls_set 실패 (" << mosquitto_strerror(trc)
                      << ") - ca=" << tls_.ca_cert_path << " cert=" << tls_.client_cert_path
                      << " key=" << tls_.client_key_path << " - MQTT 수신 비활성\n";
            mosquitto_destroy(mosq_);
            mosq_ = nullptr;
            running_ = false;
            return;
        }
    }

    const int rc = mosquitto_connect_async(mosq_, broker_host_.c_str(), broker_port_, /*keepalive=*/60);
    if (rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[SensorUplinkReceiver] MQTT 접속 시작 실패 (" << mosquitto_strerror(rc)
                  << ") - " << broker_host_ << ':' << broker_port_ << "\n";
    }

    // mosquitto_loop_start()가 내부 네트워크 스레드를 띄워 connect/reconnect/read/dispatch를
    // 전부 맡는다. 이 클래스는 콜백만 받으면 되므로 별도 워커 스레드가 필요 없다.
    mosquitto_loop_start(mosq_);
}

void SensorUplinkReceiver::stop() {
    if (!running_.exchange(false)) return;
    if (mosq_) {
        mosquitto_loop_stop(mosq_, /*force=*/true);
        mosquitto_disconnect(mosq_);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    connected_ = false;
}

bool SensorUplinkReceiver::getLatest(const std::string& terminal_id, SensorUplinkSample& out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = latest_by_terminal_.find(terminal_id);
    if (it == latest_by_terminal_.end()) return false;
    out = it->second;
    return true;
}

bool SensorUplinkReceiver::hasSample(const std::string& terminal_id) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return latest_by_terminal_.find(terminal_id) != latest_by_terminal_.end();
}

bool SensorUplinkReceiver::isStale(const std::string& terminal_id, int timeout_ms) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = latest_by_terminal_.find(terminal_id);
    if (it == latest_by_terminal_.end()) return true;   // 헤더의 [설계 결정] 참고 - 받은 게 없으면 항상 stale
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - it->second.received_at).count();
    return elapsed > timeout_ms;
}

} // namespace risk_transport
