#include "network/assignment_publisher.hpp"

#include <chrono>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace risk_transport {
namespace {

std::mutex& mqttMutex() { static std::mutex mutex; return mutex; }
int& mqttUsers() { static int users = 0; return users; }

void retainMqtt() {
    std::lock_guard<std::mutex> lock(mqttMutex());
    if (mqttUsers()++ == 0) mosquitto_lib_init();
}

void releaseMqtt() {
    std::lock_guard<std::mutex> lock(mqttMutex());
    if (--mqttUsers() == 0) mosquitto_lib_cleanup();
}

std::string escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

}  // namespace

AssignmentPublisher::AssignmentPublisher(std::string broker_host, uint16_t broker_port,
                                         MqttTlsOptions tls)
    : broker_host_(std::move(broker_host)), broker_port_(broker_port), tls_(std::move(tls)) {}

AssignmentPublisher::~AssignmentPublisher() { stop(); }

std::string AssignmentPublisher::topicFor(const std::string& terminal_id) {
    return std::string(kTopicPrefix) + terminal_id;
}

std::string AssignmentPublisher::makePayload(const std::string& terminal_id,
                                             const std::string& stream_id,
                                             const std::string& camera_id, int channel,
                                             const std::string& utc_time) {
    std::ostringstream json;
    json << "{\"type\":\"camera_assignment\","
         << "\"terminal_id\":\"" << escape(terminal_id) << "\","
         << "\"stream_id\":\"" << escape(stream_id) << "\","
         << "\"camera_id\":\"" << escape(camera_id) << "\","
         << "\"channel\":" << channel << ","
         << "\"utc_time\":\"" << escape(utc_time) << "\"}";
    return json.str();
}

void AssignmentPublisher::start() {
    if (running_.exchange(true)) return;
    connect();
    worker_ = std::thread(&AssignmentPublisher::run, this);
}

void AssignmentPublisher::stop() {
    if (!running_.exchange(false)) return;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    disconnect();
}

void AssignmentPublisher::publish(const std::string& terminal_id, const std::string& stream_id,
                                  const std::string& camera_id, int channel,
                                  const std::string& utc_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 같은 TERM의 assignment는 최신 값만 의미가 있으므로 오래된 대기값을 버린다.
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [&](const Message& message) {
        return message.topic == topicFor(terminal_id);
    }), queue_.end());
    queue_.push_back({topicFor(terminal_id), makePayload(terminal_id, stream_id, camera_id,
                                                         channel, utc_time)});
    cv_.notify_one();
}

bool AssignmentPublisher::connect() {
    retainMqtt();
    mosq_ = mosquitto_new("forklift-server-assignment", true, this);
    if (!mosq_) { releaseMqtt(); return false; }
    mosquitto_connect_callback_set(mosq_, &AssignmentPublisher::onConnect);
    mosquitto_disconnect_callback_set(mosq_, &AssignmentPublisher::onDisconnect);
    if (tls_.enabled && mosquitto_tls_set(mosq_, tls_.ca_cert_path.c_str(), nullptr,
                                          tls_.client_cert_path.c_str(), tls_.client_key_path.c_str(), nullptr) != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq_); mosq_ = nullptr; releaseMqtt(); return false;
    }
    const int rc = mosquitto_connect(mosq_, broker_host_.c_str(), broker_port_, 60);
    if (rc != MOSQ_ERR_SUCCESS) std::cerr << "[AssignmentPublisher] MQTT 연결 대기: " << mosquitto_strerror(rc) << "\n";
    if (mosquitto_loop_start(mosq_) != MOSQ_ERR_SUCCESS) {
        mosquitto_destroy(mosq_); mosq_ = nullptr; releaseMqtt(); return false;
    }
    return true;
}

void AssignmentPublisher::disconnect() {
    if (!mosq_) return;
    mosquitto_disconnect(mosq_);
    mosquitto_loop_stop(mosq_, false);
    mosquitto_destroy(mosq_); mosq_ = nullptr;
    connected_ = false;
    releaseMqtt();
}

void AssignmentPublisher::onConnect(struct mosquitto*, void* user, int rc) {
    auto* self = static_cast<AssignmentPublisher*>(user);
    self->connected_ = rc == MOSQ_ERR_SUCCESS;
}

void AssignmentPublisher::onDisconnect(struct mosquitto*, void* user, int) {
    static_cast<AssignmentPublisher*>(user)->connected_ = false;
}

void AssignmentPublisher::run() {
    while (running_) {
        Message message;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(200), [this] {
                return !running_ || (!queue_.empty() && connected_);
            });
            if (!running_) break;
            if (queue_.empty() || !connected_) continue;
            message = std::move(queue_.front());
            queue_.pop_front();
        }
        mosquitto_publish(mosq_, nullptr, message.topic.c_str(),
                          static_cast<int>(message.payload.size()), message.payload.data(),
                          kQos, kRetain);
    }
}

}  // namespace risk_transport
