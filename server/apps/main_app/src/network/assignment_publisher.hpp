#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <mosquitto.h>

#include "network/mqtt_tls_options.hpp"

namespace risk_transport {

// 지게차 화면 전환 정보를 MQTT로 보낸다.
//
// 위험 결과는 ResultPublisher가 맡고, 카메라 전환은 이 클래스가 맡는다.
// 두 메시지를 분리하면 TERM_01의 전환이 TERM_02의 화면에 섞이지 않는다.
class AssignmentPublisher {
public:
    static constexpr const char* kTopicPrefix = "forklift/assignment/";
    static constexpr int kQos = 1;
    static constexpr bool kRetain = true;

    AssignmentPublisher(std::string broker_host, uint16_t broker_port,
                        MqttTlsOptions tls = {});
    ~AssignmentPublisher();

    void start();
    void stop();

    // 마지막 assignment도 브로커에 retain되므로, 단말이 재구독하면 즉시 받는다.
    void publish(const std::string& terminal_id, const std::string& stream_id,
                 const std::string& camera_id, int channel,
                 const std::string& utc_time);

    static std::string topicFor(const std::string& terminal_id);
    static std::string makePayload(const std::string& terminal_id,
                                   const std::string& stream_id,
                                   const std::string& camera_id, int channel,
                                   const std::string& utc_time);

private:
    struct Message {
        std::string topic;
        std::string payload;
    };

    static void onConnect(struct mosquitto*, void*, int);
    static void onDisconnect(struct mosquitto*, void*, int);
    void run();
    bool connect();
    void disconnect();

    std::string broker_host_;
    uint16_t broker_port_;
    MqttTlsOptions tls_;
    struct mosquitto* mosq_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Message> queue_;
};

}  // namespace risk_transport
