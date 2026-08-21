#include "network/assignment_publisher.hpp"
#include "logging/logger.hpp"

#include <chrono>
#include <algorithm>
#include <sstream>
#include <unistd.h>

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
    : broker_host_(std::move(broker_host)), broker_port_(broker_port), tls_(std::move(tls)),
      server_run_id_(forklift::logging::Logger::instance().runId()) {}

AssignmentPublisher::~AssignmentPublisher() { stop(); }

std::string AssignmentPublisher::topicFor(const std::string& terminal_id) {
    return std::string(kTopicPrefix) + terminal_id;
}

std::string AssignmentPublisher::makePayload(const std::string& terminal_id,
                                             const std::string& stream_id,
                                             const std::string& camera_id, int channel,
                                             const std::string& utc_time,
                                             const std::string& assignment_id,
                                             std::uint64_t revision,
                                             const std::string& server_run_id) {
    std::ostringstream json;
    json << "{\"type\":\"camera_assignment\","
         << "\"terminal_id\":\"" << escape(terminal_id) << "\","
         << "\"stream_id\":\"" << escape(stream_id) << "\","
         << "\"camera_id\":\"" << escape(camera_id) << "\","
         << "\"channel\":" << channel << ","
         << "\"utc_time\":\"" << escape(utc_time) << "\"";
    if (!assignment_id.empty())
        json << ",\"assignment_id\":\"" << escape(assignment_id) << "\"";
    if (revision > 0)
        json << ",\"revision\":" << revision;
    if (!server_run_id.empty())
        json << ",\"server_run_id\":\"" << escape(server_run_id) << "\"";
    if (!assignment_id.empty() || revision > 0 || !server_run_id.empty())
        json << ",\"schema_version\":1";
    json << "}";
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
    const std::uint64_t revision = ++revision_;
    const std::string assignment_id = server_run_id_ + "-a" + std::to_string(revision);
    queue_.push_back({topicFor(terminal_id), makePayload(terminal_id, stream_id, camera_id,
                                                         channel, utc_time, assignment_id,
                                                         revision, server_run_id_)});
    cv_.notify_one();
}

bool AssignmentPublisher::connect() {
    retainMqtt();
    const std::string client_id = "forklift-server-assignment-" + std::to_string(::getpid());
    mosq_ = mosquitto_new(client_id.c_str(), true, this);
    if (!mosq_) {
        LOG_ERROR("HANDOVER", "관제 채널 전환 송신 초기화 실패 (mosquitto_new)");
        releaseMqtt();
        return false;
    }
    mosquitto_connect_callback_set(mosq_, &AssignmentPublisher::onConnect);
    mosquitto_disconnect_callback_set(mosq_, &AssignmentPublisher::onDisconnect);
    mosquitto_publish_callback_set(mosq_, &AssignmentPublisher::onPublish);
    if (tls_.enabled) {
        const int tls_rc = mosquitto_tls_set(mosq_, tls_.ca_cert_path.c_str(), nullptr,
                                             tls_.client_cert_path.c_str(),
                                             tls_.client_key_path.c_str(), nullptr);
        if (tls_rc != MOSQ_ERR_SUCCESS) {
            LOG_ERROR("HANDOVER", "관제 채널 전환 TLS 설정 실패 (사유: " +
                                      std::string(mosquitto_strerror(tls_rc)) + ")");
            mosquitto_destroy(mosq_); mosq_ = nullptr; releaseMqtt(); return false;
        }
    }
    const int rc = mosquitto_connect(mosq_, broker_host_.c_str(), broker_port_, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_WARN("HANDOVER", "관제 채널 전환 브로커 연결 대기 (" + broker_host_ + ":" +
                               std::to_string(broker_port_) + ", 사유: " +
                               std::string(mosquitto_strerror(rc)) + ")");
    }
    const int loop_rc = mosquitto_loop_start(mosq_);
    if (loop_rc != MOSQ_ERR_SUCCESS) {
        LOG_ERROR("HANDOVER", "관제 채널 전환 네트워크 스레드 시작 실패 (사유: " +
                                  std::string(mosquitto_strerror(loop_rc)) + ")");
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
    if (self->connected_) {
        LOG_INFO("HANDOVER", "관제 채널 전환 송신 연결 완료 (" + self->broker_host_ + ":" +
                                  std::to_string(self->broker_port_) + ")");
    } else {
        LOG_WARN("HANDOVER", "관제 채널 전환 송신 연결 거부 (사유: " +
                                  std::string(mosquitto_connack_string(rc)) + ")");
    }
}

void AssignmentPublisher::onDisconnect(struct mosquitto*, void* user, int rc) {
    auto* self = static_cast<AssignmentPublisher*>(user);
    self->connected_ = false;
    if (rc != 0 && self->running_) {
        LOG_WARN("HANDOVER", "관제 채널 전환 통신 끊김 (자동 재연결 대기)");
    }
}

void AssignmentPublisher::onPublish(struct mosquitto*, void* user, int mid) {
    auto* self = static_cast<AssignmentPublisher*>(user);
    std::string topic;
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        const auto it = self->pending_publish_topics_.find(mid);
        if (it != self->pending_publish_topics_.end()) {
            topic = it->second;
            self->pending_publish_topics_.erase(it);
        }
        ++self->publish_acks_;
    }
    LOG_INFO("HANDOVER", "assignment PUBACK 수신 (mid=" + std::to_string(mid) +
                              (topic.empty() ? std::string() : ", 토픽: " + topic) + ")");
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
        int mid = 0;
        const int rc = mosquitto_publish(mosq_, &mid, message.topic.c_str(),
                                         static_cast<int>(message.payload.size()), message.payload.data(),
                                         kQos, kRetain);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++publish_failures_;
            LOG_WARN("HANDOVER", "assignment publish 실패 (토픽: " + message.topic +
                                   ", 사유: " + std::string(mosquitto_strerror(rc)) +
                                   ", 누적: " + std::to_string(publish_failures_) + ")");
        } else {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_publish_topics_[mid] = message.topic;
            LOG_DEBUG("HANDOVER", "assignment publish queued (mid=" + std::to_string(mid) +
                                    ", 토픽: " + message.topic + ")");
        }
    }
}

}  // namespace risk_transport
