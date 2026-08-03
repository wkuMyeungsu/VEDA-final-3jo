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
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cerrno>
#include <functional>

// ResultPublisher - 판정 결과 TCP 송신기 (헤더 온리, POSIX 전용)
//
// [구조 변경 2026-08-03] client(connect) -> server(listen/accept)
//   단말이 현장에서 이동하며 IP가 바뀌기 때문에 서버가 단말 주소를 알 수 없다.
//   -> 서버가 고정 포트에서 대기하고, 단말이 접속해 오는 구조로 팀 협의 확정.
//   큐/백프레셔/드랍 로그 정책은 그대로 두고 소켓을 여는 방향만 뒤집었다.
//
// 단말은 동시에 1대만 붙는 전제다(활성 지게차 1대 기준). 연결 중에 들어온 추가 접속은
// accept하지 않고 listen 백로그에 남겨두며, 기존 연결이 끊기면 그때 accept된다.

namespace risk_transport {

// 링크 상태.
// 서버 구조로 바뀌면서 CONNECTING(단말로 connect 시도 중)은 의미가 없어져
// LISTENING(고정 포트에서 단말 접속 대기 중)으로 대체했다.
enum class LinkState {
    DISCONNECTED,  // listen 소켓조차 없는 상태 (start() 전 / stop() 후 / bind 실패)
    LISTENING,     // 포트에서 대기 중, 아직 단말 미접속
    CONNECTED      // 단말 1대와 연결됨
};

class ResultPublisher {
public:
    // bind_host: 대기할 로컬 주소. ""/"0.0.0.0"이면 모든 인터페이스(INADDR_ANY).
    //            단말 주소가 아니라 서버가 열 주소라는 점에 주의 (구조 변경 전과 의미가 다름).
    ResultPublisher(std::string bind_host, uint16_t port)
        : bind_host_(std::move(bind_host)), port_(port) {}

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
            // 워커가 끝난 뒤에 닫아야 소켓 fd에 대한 경쟁이 없다.
            // listen 소켓을 먼저 닫아야 상태가 CONNECTED -> DISCONNECTED로 한 번에 간다
            // (반대 순서면 중간에 LISTENING 콜백이 한 번 더 튄다).
            closeListener();
            closeConnection();
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
    // [주의] 단말 미접속 구간에도 큐는 계속 쌓인다. 단말이 뒤늦게 붙으면 밀린 결과가
    //        한꺼번에 나가는데, 각 줄에 utc_time이 있으므로 단말이 오래된 줄을 구분할 수 있다.
    //        (여기서 큐를 비워버리면 "접속 직전에 발생한 DANGER"가 통째로 사라져서
    //         안전 측면에서 더 나쁘다고 판단해 유지했다.)
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

    // 단말이 접속한 누적 횟수 (재접속 동작 확인용).
    std::size_t acceptedCount() const { return accepted_.load(); }

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

    // 단말과의 연결만 닫는다. listen 소켓은 그대로 두므로 곧바로 재대기로 돌아간다.
    void closeConnection() {
        if (conn_fd_ >= 0) { ::close(conn_fd_); conn_fd_ = -1; }
        setState(listen_fd_ >= 0 ? LinkState::LISTENING : LinkState::DISCONNECTED);
    }

    void closeListener() {
        if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
        if (conn_fd_ < 0) setState(LinkState::DISCONNECTED);
    }

    // listen 소켓을 준비한다(이미 있으면 그대로 재사용).
    // bind/listen 실패는 예외를 던지지 않고 false를 돌려준다 -> 호출부가 백오프 후 재시도.
    bool ensureListening() {
        if (listen_fd_ >= 0) return true;

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { logListenFailure("socket() 실패"); return false; }

        // 프로세스 재시작 직후 TIME_WAIT 때문에 bind가 막히는 걸 방지.
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port_);
        if (bind_host_.empty() || bind_host_ == "0.0.0.0") {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (::inet_pton(AF_INET, bind_host_.c_str(), &addr.sin_addr) <= 0) {
            ::close(fd);
            logListenFailure("bind 주소 파싱 실패: " + bind_host_);
            return false;
        }

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            logListenFailure("bind 실패 (포트 " + std::to_string(port_) + " 사용 중?)");
            return false;
        }
        if (::listen(fd, backlog_) < 0) {
            ::close(fd);
            logListenFailure("listen 실패");
            return false;
        }

        // accept를 논블로킹으로 돌려서, 단말이 안 붙어도 워커가 running_ 해제에 계속 반응한다.
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        listen_fd_ = fd;
        listen_error_logged_ = false;   // 다음 실패는 다시 한 번 찍히도록 리셋
        std::cerr << "[ResultPublisher] listen 시작 - "
                  << (bind_host_.empty() ? std::string("0.0.0.0") : bind_host_)
                  << ':' << port_ << " (단말 접속 대기)\n";
        setState(LinkState::LISTENING);
        return true;
    }

    // 단말 접속을 accept_poll_ms_ 동안만 기다린다. 아무도 안 붙으면 그냥 돌아간다
    // (이 함수가 블로킹되면 stop()이 그만큼 늦어지므로 타임아웃을 둔다).
    void acceptOnce() {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd_, &rfds);
        timeval tv{0, accept_poll_ms_ * 1000};
        if (::select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return;

        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (fd < 0) return;   // EAGAIN(위 select가 헛readiness) 등 -> 다음 루프에서 재시도

        // 판정 결과는 한 줄이 짧아 Nagle 지연(최대 40ms)이 100ms 목표에 그대로 얹힌다.
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        char ip[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));

        conn_fd_ = fd;
        ++accepted_;
        std::cerr << "[ResultPublisher] 단말 접속 - " << ip << ':' << ntohs(peer.sin_port) << "\n";
        setState(LinkState::CONNECTED);
    }

    // 보낼 게 없는 동안에도 단말이 조용히 끊은 걸 감지한다.
    // (전송 실패로만 감지하면 무전송 구간에서 끊긴 연결을 계속 CONNECTED로 들고 있게 된다.)
    bool peerClosed() {
        if (conn_fd_ < 0) return true;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(conn_fd_, &rfds);
        timeval tv{0, 0};   // 폴링만, 대기하지 않음
        if (::select(conn_fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) return false;

        char scratch[256];
        ssize_t n = ::recv(conn_fd_, scratch, sizeof(scratch), MSG_DONTWAIT);
        if (n == 0) return true;                                        // 정상 종료(EOF)
        if (n < 0) return !(errno == EAGAIN || errno == EWOULDBLOCK);   // 그 외 오류는 끊김으로 처리
        return false;   // 단말이 뭔가 보냄 - 이 채널은 단방향이라 읽고 버린다
    }

    void logListenFailure(const std::string& what) {
        // 포트가 계속 점유되면 매 재시도마다 찍혀서 로그가 묻히므로 성공할 때까지 1회만 남긴다.
        if (listen_error_logged_) return;
        listen_error_logged_ = true;
        std::cerr << "[ResultPublisher] " << what << " - "
                  << listen_retry_ms_ << "ms 뒤 재시도 (errno=" << errno << ")\n";
    }

    bool sendLine(const std::string& json) {
        std::string payload = json + "\n";
        size_t sent = 0;
        while (sent < payload.size()) {
            ssize_t n = ::send(conn_fd_, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    void run() {
        while (running_) {
            // ── 단말 미접속 구간: listen 준비 -> accept 대기 ───────────────
            // 여기서 실패해도 루프를 벗어나지 않는다(서버가 죽지 않고 계속 재대기).
            if (state_.load() != LinkState::CONNECTED) {
                if (!ensureListening()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(listen_retry_ms_));
                    continue;
                }
                acceptOnce();
                continue;
            }

            // ── 접속 구간: 큐에서 가장 오래된 항목 하나를 꺼내 전송 (FIFO 순서 보장) ──
            std::string toSend;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait_for(lk, std::chrono::milliseconds(idle_poll_ms_),
                             [this] { return !queue_.empty() || !running_; });
                if (!running_) break;
                if (queue_.empty()) {
                    lk.unlock();
                    if (peerClosed()) {
                        std::cerr << "[ResultPublisher] 단말 연결 종료 감지 - 재대기\n";
                        closeConnection();
                    }
                    continue;
                }
                toSend = std::move(queue_.front());
                queue_.pop_front();
            }

            if (!sendLine(toSend)) {
                // 이미 큐에서 꺼낸 뒤라 이 한 건은 유실된다. 재큐잉하지 않는 이유:
                // sendLine()이 일부 바이트만 보내고 실패했을 수 있어서 그대로 재전송하면
                // 수신 측에 잘린 줄 + 온전한 줄이 겹쳐 들어간다.
                // 단말이 재접속하면 다음 판정 결과(늦어도 200ms 하트비트)부터 정상 전송된다.
                std::size_t total = ++send_failures_;
                std::cerr << "[ResultPublisher] 전송 실패로 결과 1건 드랍 (누적 전송실패="
                          << total << ") - 재대기\n";
                closeConnection();
            }
        }
    }

    std::string bind_host_;
    uint16_t port_;
    int listen_fd_ = -1;          // 단말 접속을 받는 대기 소켓 (연결이 끊겨도 유지)
    int conn_fd_   = -1;          // 현재 접속한 단말 소켓
    int backlog_ = 4;
    int accept_poll_ms_  = 100;   // accept 대기 슬라이스 (stop() 반응 지연 상한)
    int idle_poll_ms_    = 100;   // 큐가 빈 동안의 대기 슬라이스 (연결 종료 감지 주기)
    int listen_retry_ms_ = 1000;  // bind/listen 실패 시 재시도 간격

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
    std::atomic<std::size_t> accepted_{0};

    // 드랍 로그 rate limit 상태 (publish() 안에서만 접근하므로 mtx_로 보호됨).
    // 첫 1건은 즉시 찍고, 그 뒤로는 마지막 로그 이후 drop_log_interval_건이
    // 쌓일 때마다 요약 1줄만 찍는다.
    std::size_t last_logged_drop_total_ = 0;
    std::size_t drop_log_interval_ = 100;

    // listen 실패 로그 rate limit (워커 스레드에서만 접근).
    bool listen_error_logged_ = false;

    std::function<void(LinkState)> onStateChange_;
};

} // namespace risk_transport
