#pragma once
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cerrno>

// CameraAssignmentServer - 9001 카메라 할당 양방향 서버 (헤더 온리, POSIX 전용)
//
// [배경] 9000(ResultPublisher)은 완전 단방향(서버->단말 판정 결과만)이라 단말이
// 보낸 바이트를 peerClosed()가 읽고 그냥 버린다. 이 채널은 단말이 접속 직후
// "누구인지" 밝혀야 하므로(hello) 그 구조를 재사용할 수 없어 별도 클래스로 뗐다.
// ResultPublisher의 로그 형식·헤더 온리 구조·뮤텍스 패턴은 참고했지만, 단말 1대
// 전제(ResultPublisher)와 달리 이쪽은 여러 단말이 동시에 붙을 수 있어(terminal_id로
// 구분) accept를 단일 소켓으로 제한하지 않는다.
//
// 프로토콜 (개행 구분 JSON, 한 줄 = 한 메시지):
//   단말 -> 서버 (접속 직후 1회, 그 외 시점에는 안 보냄):
//     {"type":"hello","terminal_id":"TERM_01"}
//   서버 -> 단말 (sendCameraAssignment() 호출로 트리거):
//     {"type":"camera_assignment","terminal_id":"...","camera_id":"...",
//      "zone":"...","utc_time":"..."}
//
// hello 검증 실패(JSON 파싱 실패 / type != "hello") 또는 kHelloTimeoutSec 안에
// hello가 안 오면 연결을 끊는다. 같은 terminal_id로 재접속하면 기존 소켓을 닫고
// 새 소켓으로 매핑을 교체한다(단말 재부팅 시 이전 연결이 죽어있을 수 있어서).
//
// 이 클래스는 "보낼 수 있게 해주는 함수"만 제공한다. 언제 누구를 전환시킬지
// 결정하는 로직(자동 핸드오버 판단)은 범위 밖이며 호출부 책임이다.
//
// 프로젝트에 JSON 라이브러리가 없어 danger_judgment_engine.cpp의 toJson()과 같은
// 방식으로 ostringstream 수작업 직렬화를 쓴다. hello 파싱도 같은 이유로 문자열
// 검색 기반 최소 파서를 쓴다(신뢰된 단말 프로토콜이라 정식 JSON 파서는 과함).

namespace risk_transport {

class CameraAssignmentServer {
public:
    // hello 없이 접속만 유지되는 소켓을 끊기까지의 대기 시간(초).
    static constexpr int kHelloTimeoutSec = 5;

    // bind_host: 대기할 로컬 주소. ""/"0.0.0.0"이면 모든 인터페이스(INADDR_ANY).
    CameraAssignmentServer(std::string bind_host, uint16_t port)
        : bind_host_(std::move(bind_host)), port_(port) {}

    ~CameraAssignmentServer() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        worker_ = std::thread(&CameraAssignmentServer::run, this);
    }

    void stop() {
        if (running_.exchange(false)) {
            if (worker_.joinable()) worker_.join();
            closeAll();
        }
    }

    // terminal_id로 매핑된 소켓에 camera_assignment 전송.
    // 매핑에 없으면(단말 미접속/hello 미완료) false 리턴 + 로그, 아무것도 하지 않는다.
    bool sendCameraAssignment(const std::string& terminal_id,
                               const std::string& camera_id,
                               const std::string& zone,
                               const std::string& utc_time) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = terminals_.find(terminal_id);
        if (it == terminals_.end()) {
            std::cerr << "[CameraAssignmentServer] sendCameraAssignment 실패 - "
                      << "매핑에 없는 terminal_id: " << terminal_id << "\n";
            return false;
        }

        std::string payload = toJson(terminal_id, camera_id, zone, utc_time) + "\n";
        int fd = it->second.fd;
        size_t sent = 0;
        while (sent < payload.size()) {
            ssize_t n = ::send(fd, payload.data() + sent, payload.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) {
                std::cerr << "[CameraAssignmentServer] 전송 실패 (terminal_id=" << terminal_id
                          << ") - 매핑 제거\n";
                ::close(fd);
                terminals_.erase(it);
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        std::cerr << "[CameraAssignmentServer] camera_assignment 전송 - terminal_id=" << terminal_id
                  << " camera_id=" << camera_id << " zone=" << zone << "\n";
        return true;
    }

    // 현재 hello로 등록된(매핑된) 단말 수.
    std::size_t connectedCount() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return terminals_.size();
    }

    // 특정 terminal_id가 매핑돼 있는지 (테스트/디버그용).
    bool isRegistered(const std::string& terminal_id) const {
        std::lock_guard<std::mutex> lk(mtx_);
        return terminals_.count(terminal_id) != 0;
    }

private:
    // hello 대기 중인 접속 (아직 terminal_id를 모름).
    struct PendingConn {
        std::string buf;
        std::chrono::steady_clock::time_point connected_at;
    };

    // hello로 확인된 단말 소켓.
    struct TerminalConn {
        int fd;
        std::string buf;
    };

    enum class PendingResult { KEEP_WAITING, PROMOTED, CLOSED };

    void closeAll() {
        for (auto& kv : pending_) ::close(kv.first);
        pending_.clear();

        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& kv : terminals_) ::close(kv.second.fd);
        terminals_.clear();

        if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
    }

    void logListenFailure(const std::string& what) {
        // 포트가 계속 점유되면 매 재시도마다 찍혀서 로그가 묻히므로 성공할 때까지 1회만 남긴다.
        if (listen_error_logged_) return;
        listen_error_logged_ = true;
        std::cerr << "[CameraAssignmentServer] " << what << " - "
                  << listen_retry_ms_ << "ms 뒤 재시도 (errno=" << errno << ")\n";
    }

    // listen 소켓을 준비한다(이미 있으면 그대로 재사용).
    bool ensureListening() {
        if (listen_fd_ >= 0) return true;

        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { logListenFailure("socket() 실패"); return false; }

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

        // accept를 논블로킹으로 돌려서, 아무도 안 붙어도 워커가 running_ 해제에 계속 반응한다.
        int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        listen_fd_ = fd;
        listen_error_logged_ = false;
        std::cerr << "[CameraAssignmentServer] listen 시작 - "
                  << (bind_host_.empty() ? std::string("0.0.0.0") : bind_host_)
                  << ':' << port_ << " (단말 접속 대기)\n";
        return true;
    }

    // 대기 중인 접속을 모두 accept한다(listen_fd_가 논블로킹이므로 EAGAIN까지 반복).
    void acceptNew() {
        for (;;) {
            sockaddr_in peer{};
            socklen_t peer_len = sizeof(peer);
            int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
            if (fd < 0) return;   // EAGAIN 등 - 더 이상 대기 중인 접속 없음

            int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            char ip[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
            std::cerr << "[CameraAssignmentServer] 접속 수락 - " << ip << ':' << ntohs(peer.sin_port)
                      << " (hello 대기, fd=" << fd << ")\n";

            pending_.emplace(fd, PendingConn{"", std::chrono::steady_clock::now()});
        }
    }

    // "key":"value" 형태의 문자열 필드 하나를 찾는다. 신뢰된 단말 프로토콜(hello 1줄)
    // 검증용 최소 파서라 중첩 객체/이스케이프는 다루지 않는다.
    static bool extractJsonStringField(const std::string& json, const std::string& key,
                                        std::string& out) {
        std::string needle = "\"" + key + "\"";
        auto key_pos = json.find(needle);
        if (key_pos == std::string::npos) return false;
        auto colon_pos = json.find(':', key_pos + needle.size());
        if (colon_pos == std::string::npos) return false;
        auto quote_start = json.find('"', colon_pos);
        if (quote_start == std::string::npos) return false;
        auto quote_end = json.find('"', quote_start + 1);
        if (quote_end == std::string::npos) return false;
        out = json.substr(quote_start + 1, quote_end - quote_start - 1);
        return true;
    }

    static bool parseHello(const std::string& line, std::string& terminal_id_out) {
        std::string type;
        if (!extractJsonStringField(line, "type", type)) return false;
        if (type != "hello") return false;
        std::string terminal_id;
        if (!extractJsonStringField(line, "terminal_id", terminal_id)) return false;
        if (terminal_id.empty()) return false;
        terminal_id_out = terminal_id;
        return true;
    }

    // 같은 terminal_id로 재접속하면 기존 소켓을 닫고 새 소켓으로 교체한다.
    void registerTerminal(const std::string& terminal_id, int fd) {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = terminals_.find(terminal_id);
        if (it != terminals_.end()) {
            std::cerr << "[CameraAssignmentServer] terminal_id 재접속 - 기존 소켓 교체 "
                      << "(terminal_id=" << terminal_id << ", old_fd=" << it->second.fd
                      << " -> new_fd=" << fd << ")\n";
            ::close(it->second.fd);
            it->second.fd = fd;
            it->second.buf.clear();
        } else {
            terminals_.emplace(terminal_id, TerminalConn{fd, ""});
        }
    }

    // hello 대기 중인 소켓에서 읽는다. 첫 줄이 유효한 hello면 PROMOTED(호출부가 pending_에서
    // 빼서 registerTerminal이 이미 넘겨받은 fd를 terminals_로 옮긴다), 파싱 실패/EOF/오류면 CLOSED.
    PendingResult handlePendingReadable(int fd, PendingConn& pc) {
        char chunk[1024];
        ssize_t n = ::recv(fd, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (n == 0) return PendingResult::CLOSED;   // hello 전에 정상 종료(EOF)
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return PendingResult::KEEP_WAITING;
            return PendingResult::CLOSED;
        }
        pc.buf.append(chunk, static_cast<std::size_t>(n));

        auto pos = pc.buf.find('\n');
        if (pos == std::string::npos) {
            if (pc.buf.size() > kMaxHelloLineBytes) {
                std::cerr << "[CameraAssignmentServer] hello 메시지가 너무 큼(> "
                          << kMaxHelloLineBytes << "B) - 연결 종료\n";
                return PendingResult::CLOSED;
            }
            return PendingResult::KEEP_WAITING;
        }

        std::string line = pc.buf.substr(0, pos);
        std::string terminal_id;
        if (!parseHello(line, terminal_id)) {
            std::cerr << "[CameraAssignmentServer] hello 파싱 실패 또는 type != hello - "
                      << "연결 종료 (수신: " << line << ")\n";
            return PendingResult::CLOSED;
        }

        registerTerminal(terminal_id, fd);
        std::cerr << "[CameraAssignmentServer] hello 수신 - terminal_id=" << terminal_id
                  << " 등록 완료 (fd=" << fd << ")\n";
        return PendingResult::PROMOTED;
    }

    // 확인된 단말 소켓에서 읽는다. 이 채널은 서버->단말이 주 방향이라 뭔가 들어와도
    // 읽고 버린다(ResultPublisher::peerClosed()와 같은 원칙) - EOF/오류면 false.
    bool handleTerminalReadable(TerminalConn& tc) {
        char scratch[256];
        ssize_t n = ::recv(tc.fd, scratch, sizeof(scratch), MSG_DONTWAIT);
        if (n == 0) return false;
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK);
        return true;
    }

    static std::string toJson(const std::string& terminal_id, const std::string& camera_id,
                               const std::string& zone, const std::string& utc_time) {
        std::ostringstream os;
        os << '{'
           << "\"type\":\"camera_assignment\","
           << "\"terminal_id\":\"" << terminal_id << "\","
           << "\"camera_id\":\"" << camera_id << "\","
           << "\"zone\":\"" << zone << "\","
           << "\"utc_time\":\"" << utc_time << "\""
           << '}';
        return os.str();
    }

    void run() {
        while (running_) {
            if (!ensureListening()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(listen_retry_ms_));
                continue;
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(listen_fd_, &rfds);
            int maxfd = listen_fd_;

            for (const auto& kv : pending_) {
                FD_SET(kv.first, &rfds);
                maxfd = std::max(maxfd, kv.first);
            }
            {
                std::lock_guard<std::mutex> lk(mtx_);
                for (const auto& kv : terminals_) {
                    FD_SET(kv.second.fd, &rfds);
                    maxfd = std::max(maxfd, kv.second.fd);
                }
            }

            timeval tv{0, static_cast<suseconds_t>(select_poll_ms_) * 1000};
            int ready = ::select(maxfd + 1, &rfds, nullptr, nullptr, &tv);

            if (ready > 0) {
                if (FD_ISSET(listen_fd_, &rfds)) acceptNew();

                for (auto it = pending_.begin(); it != pending_.end(); ) {
                    if (!FD_ISSET(it->first, &rfds)) { ++it; continue; }
                    PendingResult r = handlePendingReadable(it->first, it->second);
                    if (r == PendingResult::KEEP_WAITING) { ++it; continue; }
                    // CLOSED/PROMOTED 둘 다 fd 소유권이 pending_을 떠나므로 여기서 제거한다
                    // (PROMOTED는 registerTerminal()이 이미 terminals_로 옮겨 담았다).
                    if (r == PendingResult::CLOSED) ::close(it->first);
                    it = pending_.erase(it);
                }

                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    for (auto it = terminals_.begin(); it != terminals_.end(); ) {
                        if (!FD_ISSET(it->second.fd, &rfds)) { ++it; continue; }
                        if (handleTerminalReadable(it->second)) { ++it; continue; }
                        std::cerr << "[CameraAssignmentServer] 단말 연결 종료 - terminal_id="
                                  << it->first << "\n";
                        ::close(it->second.fd);
                        it = terminals_.erase(it);
                    }
                }
            }

            // hello 타임아웃 검사 - select 결과와 무관하게 매 루프마다 확인한다.
            auto now = std::chrono::steady_clock::now();
            for (auto it = pending_.begin(); it != pending_.end(); ) {
                auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                                     now - it->second.connected_at).count();
                if (elapsed_s < kHelloTimeoutSec) { ++it; continue; }
                std::cerr << "[CameraAssignmentServer] hello 타임아웃(" << kHelloTimeoutSec
                          << "s) - 연결 종료 (fd=" << it->first << ")\n";
                ::close(it->first);
                it = pending_.erase(it);
            }
        }
    }

    std::string bind_host_;
    uint16_t port_;
    int listen_fd_ = -1;
    int backlog_ = 8;
    int select_poll_ms_  = 200;   // select 대기 슬라이스 (stop() 반응 지연 상한)
    int listen_retry_ms_ = 1000;  // bind/listen 실패 시 재시도 간격
    static constexpr std::size_t kMaxHelloLineBytes = 4096;

    std::atomic<bool> running_{false};
    std::thread worker_;

    // hello 대기 중인 접속들. run() 스레드에서만 접근하므로 락이 필요 없다.
    std::map<int, PendingConn> pending_;
    bool listen_error_logged_ = false;   // run() 스레드 전용

    // hello로 확인된 단말 매핑. sendCameraAssignment()(외부 스레드)와 run()이 공유하므로
    // mtx_로 보호한다.
    mutable std::mutex mtx_;
    std::map<std::string, TerminalConn> terminals_;
};

} // namespace risk_transport
