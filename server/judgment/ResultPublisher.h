#pragma once
#include <string>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cerrno>
#include <functional>

namespace risk_transport {

enum class LinkState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED
};

class ResultPublisher {
public:
    ResultPublisher(std::string host, uint16_t port)
        : host_(std::move(host)), port_(port) {}

    ~ResultPublisher() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread(&ResultPublisher::run, this);
    }

    void stop() {
        // 워커 정리는 실제로 돌고 있을 때만 (중복 stop()/join() 방지).
        if (running_.exchange(false)) {
            cv_.notify_all();
            if (worker_.joinable()) worker_.join();
            closeSocket();
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
    void publish(const std::string& json) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (queue_.size() >= max_queue_size_) {
                queue_.pop_front();
                // 카운터는 매 드랍마다 증가시키고(droppedCount()는 항상 정확),
                // stderr 로그만 rate limit한다. 수신 측이 오래 느려지면 드랍이 연속으로
                // 발생하는데, 건건이 찍으면 unbuffered stderr가 판정 루프까지 느리게 만든다.
                std::size_t total = ++dropped_overflow_;
                if (total == 1) {
                    // 첫 드랍은 즉시 (백프레셔가 시작됐다는 신호)
                    std::cerr << "[ResultPublisher] queue full (max=" << max_queue_size_
                              << ") - 가장 오래된 결과 1건 드랍 (누적 드랍=" << total << ")\n";
                    last_logged_drop_total_ = total;
                } else if (total - last_logged_drop_total_ >= drop_log_interval_) {
                    // 이후로는 drop_log_interval_건마다 요약 1줄
                    std::cerr << "[ResultPublisher] dropped " << (total - last_logged_drop_total_)
                              << " more (total: " << total << ")\n";
                    last_logged_drop_total_ = total;
                }
            }
            queue_.push_back(json);
        }
        cv_.notify_one();
    }

    LinkState state() const { return state_.load(); }

    void onStateChange(std::function<void(LinkState)> cb) { onStateChange_ = std::move(cb); }

    // 큐 넘침으로 버려진 누적 건수 (백프레셔 발생 여부 추적용)
    std::size_t droppedCount() const { return dropped_overflow_.load(); }

    // 전송 실패(연결 끊김 등)로 버려진 누적 건수. 큐 넘침과 원인이 달라 따로 센다.
    std::size_t sendFailureCount() const { return send_failures_.load(); }

private:
    // 종료 시점에 아직 로그로 안 남은 드랍 잔여분을 요약 1줄로 남긴다.
    // 잔여분이 없으면 아무것도 찍지 않으므로 여러 번 불려도 안전하다.
    void flushDropLogSummary() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::size_t total = dropped_overflow_.load();
        if (total > last_logged_drop_total_) {
            std::cerr << "[ResultPublisher] dropped " << (total - last_logged_drop_total_)
                      << " more (total: " << total << ") - 종료 시 잔여 요약\n";
            last_logged_drop_total_ = total;
        }
    }

    void setState(LinkState s) {
        if (state_.exchange(s) != s && onStateChange_) onStateChange_(s);
    }

    void closeSocket() {
        if (sockfd_ >= 0) { ::close(sockfd_); sockfd_ = -1; }
        setState(LinkState::DISCONNECTED);
    }

    bool connectOnce() {
        setState(LinkState::CONNECTING);
        sockfd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd_ < 0) return false;

        int flags = fcntl(sockfd_, F_GETFL, 0);
        fcntl(sockfd_, F_SETFL, flags | O_NONBLOCK);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
            closeSocket();
            return false;
        }

        int rc = ::connect(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (rc < 0 && errno != EINPROGRESS) {
            closeSocket();
            return false;
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sockfd_, &wfds);
        timeval tv{connect_timeout_sec_, 0};
        rc = ::select(sockfd_ + 1, nullptr, &wfds, nullptr, &tv);
        if (rc <= 0) { closeSocket(); return false; }

        int so_err = 0; socklen_t len = sizeof(so_err);
        getsockopt(sockfd_, SOL_SOCKET, SO_ERROR, &so_err, &len);
        if (so_err != 0) { closeSocket(); return false; }

        fcntl(sockfd_, F_SETFL, flags);

        int one = 1;
        setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        setState(LinkState::CONNECTED);
        return true;
    }

    bool sendLine(const std::string& json) {
        std::string payload = json + "\n";
        size_t sent = 0;
        while (sent < payload.size()) {
            ssize_t n = ::send(sockfd_, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    void run() {
        auto lastReconnectAttempt = std::chrono::steady_clock::now() - std::chrono::seconds(reconnect_backoff_sec_);

        while (running_) {
            if (state_.load() != LinkState::CONNECTED) {
                auto now = std::chrono::steady_clock::now();
                if (now - lastReconnectAttempt < std::chrono::seconds(reconnect_backoff_sec_)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                lastReconnectAttempt = now;
                if (!connectOnce()) {
                    continue;
                }
            }

            // 큐에서 가장 오래된 항목 하나를 꺼내 전송한다 (FIFO 순서 보장).
            std::string toSend;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return !queue_.empty() || !running_; });
                if (!running_) break;
                if (queue_.empty()) continue;
                toSend = std::move(queue_.front());
                queue_.pop_front();
            }

            if (!sendLine(toSend)) {
                // 이미 큐에서 꺼낸 뒤라 이 한 건은 유실된다. 재큐잉하지 않는 이유:
                // sendLine()이 일부 바이트만 보내고 실패했을 수 있어서 그대로 재전송하면
                // 수신 측에 잘린 줄 + 온전한 줄이 겹쳐 들어간다.
                // 재연결 후 다음 판정 결과부터 정상 전송된다.
                std::size_t total = ++send_failures_;
                std::cerr << "[ResultPublisher] 전송 실패로 결과 1건 드랍 (누적 전송실패="
                          << total << ")\n";
                closeSocket();
            }
        }
    }

    std::string host_;
    uint16_t port_;
    int sockfd_ = -1;
    int connect_timeout_sec_ = 1;
    int reconnect_backoff_sec_ = 1;

    std::atomic<bool> running_{false};
    std::atomic<LinkState> state_{LinkState::DISCONNECTED};
    std::thread worker_;

    // 송신 대기 큐 (FIFO). mtx_로 보호된다.
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;

    // 큐 최대 길이. 초과 시 가장 오래된 항목부터 버린다.
    // 100건 = 판정 주기를 고려하면 수 초 분량 버퍼로, 수신 측이 잠시 느려져도 흡수 가능.
    std::size_t max_queue_size_ = 100;

    std::atomic<std::size_t> dropped_overflow_{0};
    std::atomic<std::size_t> send_failures_{0};

    // 드랍 로그 rate limit 상태 (publish() 안에서만 접근하므로 mtx_로 보호됨).
    // 첫 1건은 즉시 찍고, 그 뒤로는 마지막 로그 이후 drop_log_interval_건이
    // 쌓일 때마다 요약 1줄만 찍는다.
    std::size_t last_logged_drop_total_ = 0;
    std::size_t drop_log_interval_ = 100;

    std::function<void(LinkState)> onStateChange_;
};

} // namespace risk_transport
