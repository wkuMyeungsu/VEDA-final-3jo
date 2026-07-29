#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <cstring>
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
        if (!running_.exchange(false)) return;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        closeSocket();
    }

    void publish(const std::string& json) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            pending_ = json;
            has_pending_ = true;
        }
        cv_.notify_one();
    }

    LinkState state() const { return state_.load(); }

    void onStateChange(std::function<void(LinkState)> cb) { onStateChange_ = std::move(cb); }

private:
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

            std::string toSend;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(100), [this] { return has_pending_ || !running_; });
                if (!running_) break;
                if (!has_pending_) continue;
                toSend = pending_;
                has_pending_ = false;
            }

            if (!sendLine(toSend)) {
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

    std::mutex mtx_;
    std::condition_variable cv_;
    std::string pending_;
    bool has_pending_ = false;

    std::function<void(LinkState)> onStateChange_;
};

} // namespace risk_transport
