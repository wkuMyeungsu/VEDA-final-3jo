// test_sensor_uplink_receiver.cpp
// SensorUplinkReceiver(단말->서버 센서 업링크 수신기, MQTT 구독) 검증
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// [전송 방식 변경] TCP+NDJSON+hello 핸드셰이크 -> MQTT 구독으로 교체됐다(서버 측 수신 경로
// "최신값 캐시" 방식 팀 확정, 박명수 확인 완료). 이 파일도 그에 맞춰 재구성했다:
//
//   유지(캐시/파싱 동작 검증 - 전송 계층이 바뀌어도 의미가 그대로인 것들):
//     [테스트 1] 정상 수신 - getLatest()가 보낸 값과 일치, 최신값이 이전 값을 덮어씀
//     [테스트 2] 깨진 JSON을 받아도 죽지 않고 그 메시지만 버리고 계속 수신
//     [테스트 3] 마지막 수신 이후 timeout_ms를 넘기면 isStale() == true
//     [테스트 4] 초기 상태 규약(받은 게 없으면 항상 stale) - hasSample()/getLatest() 포함
//     [테스트 5] terminal_id별로 캐시가 분리됨 (map 기반으로 바뀌면서 새로 생긴 요구사항 -
//                옛 TCP 버전은 1:1 연결이라 terminal_id가 전역 상태 하나였다)
//
//   제거(TCP 프레이밍/hello 전용 개념이라 MQTT에는 대응 개념이 없음, 재작성하지 않음):
//     - 과대 줄(oversized line) 재동기화 테스트: mosquitto 라이브러리가 메시지 경계를
//       자체적으로 보장하므로(우리가 직접 recv 버퍼를 파싱하던 것과 다름) 이 클래스
//       레벨에서는 더 이상 의미 있는 테스트 대상이 아니다.
//     - hello 누락 시 연결 종료 테스트: hello 자체가 없어졌다(terminal_id는 이제 MQTT
//       토픽에서 옴). 대응 개념 없음.
//
//   보류(재작성 "방안만" 제안 - 이번에 구현하지 않음):
//     - 옛 [테스트 5] "단말 재접속 시 re-listen, 캐시는 유지" (TCP accept 카운트 기반)의
//       MQTT 대응물은 "브로커와의 연결이 끊겼다가 mosquitto_reconnect_delay_set()(3~30초)
//       설정대로 재접속되고, 그동안 캐시는 유지되며, 재접속 후 isConnected()가 다시
//       true로 돌아오는지" 검증이 될 것이다. 다만 이걸 신뢰성 있게 테스트하려면
//       (a) 로컬 mosquitto 브로커 프로세스를 테스트 안에서 죽였다 살리거나
//       (b) 브로커와 클라이언트 사이에 테스트가 제어할 수 있는 TCP 프록시를 끼워 끊는 식의
//       인프라가 필요하다 - 지금 짜여있는 "로컬 브로커에 직접 연결" 방식 테스트 그대로는
//       재현하기 어려워서 별도 합의 후 넣기로 하고 이번 범위에서는 제외했다.
//
// [테스트 방식] 실제 로컬 mosquitto 브로커(127.0.0.1:1883)에
// 붙는다(이 저장소 개발 환경에는 mosquitto 브로커가 시스템 서비스로 이미 떠 있다).
// 브로커가 없는 환경에서 돌리면 접속 자체가 안 돼 타임아웃으로 실패한다 - 별도 mock
// 브로커를 두지 않은 이유는 SensorUplinkReceiver가 mosquitto C 클라이언트 API를 얇게
// 감싸는 수준이라, 진짜 브로커로 검증하는 편이 mock의 신뢰도 문제를 피할 수 있어서다.
//
// 실행: ./test_sensor_uplink_receiver   (종료코드 0=성공, 1=실패)

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <functional>

#include <unistd.h>
#include <mosquitto.h>

#include "network/sensor_uplink_receiver.hpp"

namespace {

using risk_transport::SensorUplinkReceiver;
using risk_transport::SensorUplinkSample;

// 로컬 mosquitto 브로커(시스템 서비스, 기본 포트). SensorUplinkReceiver 기본값과 동일.
constexpr int kBrokerPort = SensorUplinkReceiver::kDefaultBrokerPort;

int failures = 0;

void check(bool cond, const std::string& what) {
    std::cout << (cond ? "  [OK]   " : "  [FAIL] ") << what << "\n";
    if (!cond) ++failures;
}

// 이 테스트 실행 하나에 고유한 topic 접두어를 만든다 - 같은 브로커를 다른 프로세스와
// 공유해도(예: 실제 단말이 같은 브로커에 붙어 있는 개발 환경) 서로의 terminal_id가
// 부딪히지 않게 하기 위함이다.
std::string uniqueTerminalId(const std::string& suffix) {
    return "TEST_" + std::to_string(static_cast<long long>(::getpid())) + "_" + suffix;
}

std::string topicFor(const std::string& terminal_id) {
    return "forklift/sensor/" + terminal_id;
}

// 문서 스키마 그대로의 센서 payload 한 건.
std::string makeSamplePayload(const std::string& camera_id, int tof_mm, int64_t ts_ms) {
    return std::string("{\"camera_id\": \"") + camera_id + "\", \"tof_ok\": true, "
           "\"tof_distance_mm\": " + std::to_string(tof_mm) + ", \"imu_ok\": true, "
           "\"imu_accel_x_g\": 0.02, \"imu_accel_y_g\": -0.01, \"imu_accel_z_g\": 1.01, "
           "\"ts_ms\": " + std::to_string(ts_ms) + "}";
}

// 테스트 전용 MQTT 발행기. SensorUplinkReceiver는 구독만 하므로, 검증용 메시지를
// 실제로 브로커에 쏘는 쪽은 이 클래스가 맡는다(옛 TCP 버전의 FakeTerminal과 같은 역할).
class TestPublisher {
public:
    TestPublisher() : mosq_(mosquitto_new(nullptr, true, this)) {
        if (mosq_) {
            mosquitto_connect_callback_set(mosq_, &TestPublisher::onConnectTrampoline);
            mosquitto_publish_callback_set(mosq_, &TestPublisher::onPublishTrampoline);
        }
    }
    ~TestPublisher() {
        if (mosq_) {
            mosquitto_disconnect(mosq_);
            mosquitto_destroy(mosq_);
        }
    }

    bool publish(const std::string& topic, const std::string& payload) {
        if (!mosq_) return false;
        connected_ = false;
        if (mosquitto_connect(mosq_, "127.0.0.1", kBrokerPort, /*keepalive=*/60) != MOSQ_ERR_SUCCESS) {
            return false;
        }
        // CONNACK를 실제로 받을 때까지 돌려준다 (동기 connect는 TCP 연결까지만 보장한다).
        for (int i = 0; i < 100 && !connected_; ++i) {
            mosquitto_loop(mosq_, 20, 1);
        }
        if (!connected_) return false;

        // mosquitto_publish()는 QoS 0에서 소켓에 쓸 자리가 있으면 콜백을 그 호출 안에서
        // 동기적으로 불러버린다(반환 전에 이미 완료 통지가 옴) - 그래서 published_는
        // publish() 호출 "전"에 false로 깔아두고, mid 비교 없이 콜백 여부만 본다
        // (이 발행기는 한 번에 메시지 하나만 보내므로 mid 매칭이 필요 없다).
        int mid = 0;
        published_ = false;
        const int rc = mosquitto_publish(mosq_, &mid, topic.c_str(),
                                          static_cast<int>(payload.size()), payload.data(),
                                          /*qos=*/0, /*retain=*/false);
        if (rc != MOSQ_ERR_SUCCESS) return false;

        // 위에서 이미 동기적으로 콜백이 왔을 수도 있고(published_==true), 소켓 버퍼가
        // 꽉 차 있어 다음 loop() 호출까지 밀렸을 수도 있다 - 후자만 대비해 짧게 더 돌린다.
        for (int i = 0; i < 100 && !published_; ++i) {
            mosquitto_loop(mosq_, 10, 1);
        }
        mosquitto_disconnect(mosq_);
        return published_;
    }

private:
    static void onConnectTrampoline(mosquitto*, void* userdata, int rc) {
        auto* self = static_cast<TestPublisher*>(userdata);
        if (rc == 0) self->connected_ = true;
    }

    static void onPublishTrampoline(mosquitto*, void* userdata, int /*mid*/) {
        static_cast<TestPublisher*>(userdata)->published_ = true;
    }

    mosquitto* mosq_;
    bool connected_ = false;
    bool published_ = false;
};

bool waitUntil(int timeout_ms, const std::function<bool()>& pred) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

bool waitForConnected(const SensorUplinkReceiver& rx, int timeout_ms) {
    return waitUntil(timeout_ms, [&] { return rx.isConnected(); });
}

bool waitForReceived(const SensorUplinkReceiver& rx, std::size_t want, int timeout_ms) {
    return waitUntil(timeout_ms, [&] { return rx.receivedCount() >= want; });
}

bool waitForParseFailures(const SensorUplinkReceiver& rx, std::size_t want, int timeout_ms) {
    return waitUntil(timeout_ms, [&] { return rx.parseFailureCount() >= want; });
}

bool waitForSample(const SensorUplinkReceiver& rx, const std::string& terminal_id, int timeout_ms) {
    return waitUntil(timeout_ms, [&] { return rx.hasSample(terminal_id); });
}

bool nearlyEqual(double a, double b) { return std::fabs(a - b) < 1e-9; }

// hello 없이(토픽으로 바로 식별) 정상 수신 -> getLatest()가 보낸 값 그대로인지,
// 최신값이 이전 값을 덮어쓰는지(캐시 = last-write-wins).
void testReceivesLatestSample() {
    std::cout << "[테스트 1] 정상 수신 - getLatest()가 보낸 값과 일치, 최신값으로 덮어써짐\n";

    const std::string term = uniqueTerminalId("T1");
    const std::string topic = topicFor(term);

    SensorUplinkReceiver rx;
    rx.start();
    check(waitForConnected(rx, 3000), "브로커에 연결됨");

    TestPublisher pub;
    const int64_t kTs = 1754380800123LL;
    check(pub.publish(topic, makeSamplePayload("cam0", 420, kTs)), "센서 메시지 1건 발행");
    check(waitForReceived(rx, 1, 3000),
          "수신 카운터 1 (실제: " + std::to_string(rx.receivedCount()) + ")");

    SensorUplinkSample s;
    const bool got = rx.getLatest(term, s);
    check(got, "getLatest()가 스냅샷을 돌려줌");
    if (got) {
        check(s.camera_id == "cam0", "camera_id == cam0 (실제: '" + s.camera_id + "')");
        check(s.tof_ok, "tof_ok == true");
        check(s.tof_distance_mm == 420,
              "tof_distance_mm == 420 (실제: " + std::to_string(s.tof_distance_mm) + ")");
        check(s.imu_ok, "imu_ok == true");
        check(nearlyEqual(s.imu_accel_x_g, 0.02), "imu_accel_x_g == 0.02");
        check(nearlyEqual(s.imu_accel_y_g, -0.01), "imu_accel_y_g == -0.01 (음수 파싱)");
        check(nearlyEqual(s.imu_accel_z_g, 1.01), "imu_accel_z_g == 1.01");
        check(s.ts_ms == kTs, "ts_ms == " + std::to_string(kTs) +
              " (실제: " + std::to_string(s.ts_ms) + ")");
        check(s.received_at.time_since_epoch().count() != 0,
              "received_at(서버 수신 시각)이 채워짐");
    }

    check(pub.publish(topic, makeSamplePayload("cam1", 1350, kTs + 500)), "센서 메시지 2건째 발행");
    check(waitForReceived(rx, 2, 3000), "수신 카운터 2");
    if (rx.getLatest(term, s)) {
        check(s.tof_distance_mm == 1350 && s.camera_id == "cam1",
              "최신값으로 덮어써짐 (실제: " + s.camera_id + "/" +
              std::to_string(s.tof_distance_mm) + ")");
    }

    check(rx.parseFailureCount() == 0,
          "파싱 실패 0 (실제: " + std::to_string(rx.parseFailureCount()) + ")");
    rx.stop();
}

// 깨진 JSON/필드 누락을 받아도 죽지 않고, 그 메시지만 버리고 계속 받아야 한다.
void testSurvivesMalformedPayloads() {
    std::cout << "\n[테스트 2] 깨진 JSON - 죽지 않고 그 메시지만 버리고 계속 수신\n";

    const std::string term = uniqueTerminalId("T2");
    const std::string topic = topicFor(term);

    SensorUplinkReceiver rx;
    rx.start();
    check(waitForConnected(rx, 3000), "브로커에 연결됨");

    TestPublisher pub;

    // 정상 1건으로 기준선을 만든다.
    check(pub.publish(topic, makeSamplePayload("cam0", 500, 1754380800000LL)), "정상 메시지 발행(기준선)");
    check(waitForReceived(rx, 1, 3000), "정상 메시지 1건 수신 (기준선)");

    const std::vector<std::pair<std::string, std::string>> broken = {
        {"{\"camera_id\": \"cam0\", \"tof_ok\": tr",                 "잘린 JSON"},
        {"이건 JSON이 아니다",                                        "JSON 아님"},
        {"{}",                                                        "빈 오브젝트"},
        {"{\"camera_id\": \"cam0\", \"tof_ok\": true, \"imu_ok\": true}", "필드 누락(거리/가속도/ts)"},
        {"{\"camera_id\": \"cam0\", \"tof_ok\": true, \"tof_distance_mm\": \"멀다\", "
         "\"imu_ok\": true, \"imu_accel_x_g\": 0.0, \"imu_accel_y_g\": 0.0, "
         "\"imu_accel_z_g\": 1.0, \"ts_ms\": 1}",                     "숫자 자리에 문자열"},
        {"[1,2,3]",                                                   "오브젝트가 아닌 배열"},
    };
    for (const auto& b : broken) check(pub.publish(topic, b.first), "깨진 메시지 발행 - " + b.second);

    check(waitForParseFailures(rx, broken.size(), 3000),
          "깨진 메시지 " + std::to_string(broken.size()) + "건이 전부 파싱 실패로 집계 (실제: " +
          std::to_string(rx.parseFailureCount()) + ")");
    check(rx.receivedCount() == 1,
          "깨진 메시지는 캐시에 반영되지 않음 (수신 카운터 여전히 1, 실제: " +
          std::to_string(rx.receivedCount()) + ")");

    SensorUplinkSample s;
    if (rx.getLatest(term, s)) {
        check(s.tof_distance_mm == 500,
              "캐시는 마지막 정상값 500 그대로 (실제: " +
              std::to_string(s.tof_distance_mm) + ")");
    }

    // 핵심: 깨진 메시지 뒤에도 구독은 살아 있고 다음 정상 메시지가 그대로 들어와야 한다.
    check(rx.isConnected(), "깨진 메시지를 받아도 브로커 연결이 유지됨");
    check(pub.publish(topic, makeSamplePayload("cam0", 777, 1754380801000LL)),
          "깨진 메시지 뒤 정상 메시지 발행");
    check(waitForReceived(rx, 2, 3000),
          "다음 메시지부터 정상 수신 재개 (수신 카운터 2, 실제: " +
          std::to_string(rx.receivedCount()) + ")");
    if (rx.getLatest(term, s)) {
        check(s.tof_distance_mm == 777,
              "복구 후 값이 캐시에 반영됨 (실제: " + std::to_string(s.tof_distance_mm) + ")");
    }

    rx.stop();
}

// 수신 직후에는 fresh, timeout_ms를 넘기면 stale.
void testStaleAfterTimeout() {
    std::cout << "\n[테스트 3] 타임아웃 - 마지막 수신 이후 timeout_ms 초과 시 isStale() == true\n";

    const std::string term = uniqueTerminalId("T3");
    const std::string topic = topicFor(term);

    SensorUplinkReceiver rx;
    rx.start();
    check(waitForConnected(rx, 3000), "브로커에 연결됨");

    TestPublisher pub;
    check(pub.publish(topic, makeSamplePayload("cam0", 420, 1754380800123LL)), "센서 메시지 1건 발행");
    check(waitForReceived(rx, 1, 3000), "센서 메시지 1건 수신");

    check(!rx.isStale(term), "수신 직후에는 기본 타임아웃(" +
          std::to_string(SensorUplinkReceiver::kDefaultStaleTimeoutMs) + "ms) 기준 fresh");
    check(!rx.isStale(term, 200), "수신 직후에는 200ms 기준으로도 fresh");

    // 단말이 조용해지는 구간을 만든다 (구독 자체는 유지, 전송만 멈춤).
    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    check(rx.isStale(term, 200), "마지막 수신 350ms 뒤 - 200ms 타임아웃 기준 stale");
    check(!rx.isStale(term), "같은 시점에 기본 1200ms 기준으로는 아직 fresh");
    check(rx.hasSample(term), "stale이어도 마지막 스냅샷은 남아 있음 (hasSample() == true)");

    SensorUplinkSample s;
    check(rx.getLatest(term, s) && s.tof_distance_mm == 420,
          "stale 상태에서도 getLatest()는 마지막 값을 그대로 돌려줌");

    // 새 메시지가 오면 다시 fresh로 돌아와야 한다.
    check(pub.publish(topic, makeSamplePayload("cam0", 430, 1754380800500LL)), "새 메시지 발행");
    check(waitForReceived(rx, 2, 3000), "새 메시지 수신");
    check(!rx.isStale(term, 200), "새 메시지 수신 후 200ms 기준으로 다시 fresh");

    rx.stop();
}

// 아직 한 번도 못 받은 terminal_id의 isStale() 규약.
void testStaleBeforeAnyData() {
    std::cout << "\n[테스트 4] 초기 상태 규약 - 받은 게 없으면 항상 stale\n";

    const std::string term = uniqueTerminalId("T4");
    const std::string topic = topicFor(term);

    SensorUplinkReceiver rx;

    // (1) start() 전 - MQTT 클라이언트조차 없음
    check(rx.isStale(term), "start() 전 isStale() == true");
    check(!rx.hasSample(term), "start() 전 hasSample() == false");
    SensorUplinkSample s;
    s.tof_distance_mm = -12345;   // getLatest() 실패 시 out을 건드리지 않는지 확인용 표식
    check(!rx.getLatest(term, s), "start() 전 getLatest() == false");
    check(s.tof_distance_mm == -12345, "getLatest() 실패 시 out 파라미터를 건드리지 않음");

    // (2) start() 후, 이 terminal_id로는 아직 아무 메시지도 안 옴 - 여전히 stale이어야 한다.
    rx.start();
    check(waitForConnected(rx, 3000), "브로커에 연결됨 (구독 시작)");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    check(rx.isStale(term), "이 terminal_id로 메시지가 없는 동안 isStale() == true");
    check(!rx.hasSample(term), "hasSample() == false");
    check(!rx.getLatest(term, s), "getLatest() == false");

    // (3) 첫 메시지가 들어온 순간에만 fresh로 바뀐다.
    TestPublisher pub;
    check(pub.publish(topic, makeSamplePayload("cam0", 900, 1754380800123LL)), "첫 센서 메시지 발행");
    check(waitForReceived(rx, 1, 3000), "첫 센서 메시지 수신");
    check(!rx.isStale(term), "첫 센서 메시지 수신 후에야 fresh로 전환");
    check(rx.hasSample(term), "hasSample() == true");

    rx.stop();
}

// terminal_id별로 캐시가 독립적인지 (map 기반으로 바뀌면서 새로 생긴 요구사항).
void testCachePerTerminal() {
    std::cout << "\n[테스트 5] terminal_id별 캐시 분리 - 서로 값이 섞이지 않음\n";

    const std::string termA = uniqueTerminalId("T5A");
    const std::string termB = uniqueTerminalId("T5B");

    SensorUplinkReceiver rx;
    rx.start();
    check(waitForConnected(rx, 3000), "브로커에 연결됨");

    TestPublisher pub;
    check(pub.publish(topicFor(termA), makeSamplePayload("cam0", 111, 1754380800000LL)),
          "단말 A 메시지 발행");
    check(pub.publish(topicFor(termB), makeSamplePayload("cam1", 222, 1754380800001LL)),
          "단말 B 메시지 발행");

    check(waitForSample(rx, termA, 3000), "단말 A 값 수신");
    check(waitForSample(rx, termB, 3000), "단말 B 값 수신");

    SensorUplinkSample sa, sb;
    check(rx.getLatest(termA, sa) && sa.tof_distance_mm == 111 && sa.camera_id == "cam0",
          "단말 A 캐시가 A의 값 그대로 (실제: " + sa.camera_id + "/" +
          std::to_string(sa.tof_distance_mm) + ")");
    check(rx.getLatest(termB, sb) && sb.tof_distance_mm == 222 && sb.camera_id == "cam1",
          "단말 B 캐시가 B의 값 그대로 (실제: " + sb.camera_id + "/" +
          std::to_string(sb.tof_distance_mm) + ")");

    // 존재하지 않는 제3의 terminal_id는 여전히 stale/없음이어야 한다 (다른 단말과 안 섞임).
    const std::string termC = uniqueTerminalId("T5C");
    check(!rx.hasSample(termC), "발행한 적 없는 terminal_id는 hasSample() == false");
    check(rx.isStale(termC), "발행한 적 없는 terminal_id는 isStale() == true");

    check(rx.receivedCount() == 2,
          "수신 카운터는 전체 단말 합산 2 (실제: " + std::to_string(rx.receivedCount()) + ")");

    rx.stop();
}

} // namespace

int main() {
    std::cout << "=== SensorUplinkReceiver(단말->서버 센서 업링크, MQTT) 테스트 ===\n\n";
    std::cout << "※ 로컬 mosquitto 브로커(127.0.0.1:" << kBrokerPort
              << ")가 떠 있어야 통과합니다.\n\n";

    // 이 테스트는 외부 mosquitto 브로커가 필요한 통합 테스트다. 브로커가 없으면
    // 성공으로 위장하지 않고 실패시킨다. 기본 CTest 묶음에는 등록하지 않으며,
    // -DENABLE_NETWORK_INTEGRATION_TESTS=ON으로 명시적으로 활성화한다.
    mosquitto_lib_init();
    mosquitto* probe = mosquitto_new(nullptr, true, nullptr);
    const int probe_rc = probe ? mosquitto_connect(probe, "127.0.0.1", kBrokerPort, 2)
                               : MOSQ_ERR_NOMEM;
    if (probe) {
        if (probe_rc == MOSQ_ERR_SUCCESS) mosquitto_disconnect(probe);
        mosquitto_destroy(probe);
    }
    mosquitto_lib_cleanup();
    if (probe_rc != MOSQ_ERR_SUCCESS) {
        std::cerr << "[실패] MQTT 브로커에 연결할 수 없습니다: "
                  << mosquitto_strerror(probe_rc) << "\n";
        return 1;
    }

    testReceivesLatestSample();
    testSurvivesMalformedPayloads();
    testStaleAfterTimeout();
    testStaleBeforeAnyData();
    testCachePerTerminal();

    std::cout << "\n=== " << (failures == 0 ? "전체 통과" : "실패 " + std::to_string(failures) + "건")
              << " ===\n";
    return failures == 0 ? 0 : 1;
}
