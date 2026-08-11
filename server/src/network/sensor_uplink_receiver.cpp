// sensor_uplink_receiver.cpp
// 선언·설계 메모는 sensor_uplink_receiver.hpp 참고.
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// ResultPublisher(listen/accept/재대기 골격, 로그 rate limit)와 CameraAssignmentServer
// (hello 규약, 최소 JSON 파서)의 패턴을 그대로 따랐다. 다른 점은 방향뿐이다 -
// 이쪽은 서버가 계속 recv만 하고, 보내는 건 없다.
//
// [소켓 구현을 헤더가 아니라 .cpp에 둔 이유]
// 기존 두 채널은 헤더 온리지만, result_publisher.cpp가 "향후 소켓 구현을 헤더에서
// 분리할 수 있게" 남겨둔 방향이 이쪽이다. 신규 파일이라 헤더 온리 호환성을 지킬 이유가
// 없어서 선언/구현을 나눴다.

#include "network/sensor_uplink_receiver.hpp"

#include <iostream>
#include <cstdlib>
#include <cerrno>

#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>

namespace risk_transport {

namespace {

// ── 최소 JSON 필드 파서 ────────────────────────────────────────────────
// CameraAssignmentServer::extractJsonStringField()와 같은 방식의 문자열 검색 파서다.
// 한 줄짜리 평면 오브젝트(중첩 없음, 이스케이프 없음)만 다루는 전제이며, 그 밖의 입력은
// "파싱 실패"로 떨어져 호출부가 그 줄을 버린다 - 즉 헐거운 파서라도 오탐이 사고로
// 이어지지 않는다.

// "key" 뒤의 콜론을 지나 값의 첫 글자 위치를 찾는다. 못 찾으면 npos.
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

// CameraAssignmentServer::parseHello()와 동일 규약.
bool parseHello(const std::string& line, std::string& terminal_id_out) {
    std::string type;
    if (!extractString(line, "type", type)) return false;
    if (type != "hello") return false;
    std::string terminal_id;
    if (!extractString(line, "terminal_id", terminal_id)) return false;
    if (terminal_id.empty()) return false;
    terminal_id_out = terminal_id;
    return true;
}

// 센서 줄 파싱. 스키마의 8개 필드를 전부 요구하고, 하나라도 없거나 형식이 어긋나면
// why에 이유를 담아 false. 일부 필드만 채운 반쪽 스냅샷을 캐시에 올리면 판정이
// 근거 없는 값으로 돌아가므로 "전부 아니면 버림"으로 간다.
bool parseSample(const std::string& line, SensorUplinkSample& out, std::string& why) {
    auto first = line.find_first_not_of(" \t\r");
    auto last  = line.find_last_not_of(" \t\r");
    if (first == std::string::npos || line[first] != '{' || line[last] != '}') {
        why = "JSON 오브젝트 형식이 아님";
        return false;
    }

    SensorUplinkSample s;
    if (!extractString(line, "camera_id", s.camera_id))          { why = "camera_id 누락/형식오류";       return false; }
    if (!extractBool(line, "tof_ok", s.tof_ok))                  { why = "tof_ok 누락/형식오류";          return false; }
    if (!extractBool(line, "imu_ok", s.imu_ok))                  { why = "imu_ok 누락/형식오류";          return false; }

    double tof_mm = 0.0;
    if (!extractDouble(line, "tof_distance_mm", tof_mm))         { why = "tof_distance_mm 누락/형식오류"; return false; }
    s.tof_distance_mm = static_cast<int>(tof_mm);

    if (!extractDouble(line, "imu_accel_x_g", s.imu_accel_x_g))  { why = "imu_accel_x_g 누락/형식오류";   return false; }
    if (!extractDouble(line, "imu_accel_y_g", s.imu_accel_y_g))  { why = "imu_accel_y_g 누락/형식오류";   return false; }
    if (!extractDouble(line, "imu_accel_z_g", s.imu_accel_z_g))  { why = "imu_accel_z_g 누락/형식오류";   return false; }
    if (!extractInt64(line, "ts_ms", s.ts_ms))                   { why = "ts_ms 누락/형식오류";           return false; }

    out = s;
    return true;
}

} // namespace

SensorUplinkReceiver::SensorUplinkReceiver(std::string bind_host, uint16_t port)
    : bind_host_(std::move(bind_host)), port_(port) {}

SensorUplinkReceiver::~SensorUplinkReceiver() { stop(); }

void SensorUplinkReceiver::start() {
    if (running_.exchange(true)) return;
    worker_ = std::thread(&SensorUplinkReceiver::run, this);
}

void SensorUplinkReceiver::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
    // 워커가 끝난 뒤에 닫아야 소켓 fd에 대한 경쟁이 없다 (ResultPublisher::stop()과 동일).
    closeListener();
    closeConnection();
}

bool SensorUplinkReceiver::getLatest(SensorUplinkSample& out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!has_sample_) return false;
    out = latest_;
    return true;
}

bool SensorUplinkReceiver::hasSample() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return has_sample_;
}

bool SensorUplinkReceiver::isStale(int timeout_ms) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!has_sample_) return true;   // 헤더의 [설계 결정] 참고 - 받은 게 없으면 항상 stale
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - latest_.received_at).count();
    return elapsed > timeout_ms;
}

bool SensorUplinkReceiver::isConnected() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return connected_;
}

std::string SensorUplinkReceiver::terminalId() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return terminal_id_;
}

void SensorUplinkReceiver::logListenFailure(const std::string& what) {
    // 포트가 계속 점유되면 매 재시도마다 찍혀서 로그가 묻히므로 성공할 때까지 1회만 남긴다.
    if (listen_error_logged_) return;
    listen_error_logged_ = true;
    std::cerr << "[SensorUplinkReceiver] " << what << " - "
              << listen_retry_ms_ << "ms 뒤 재시도 (errno=" << errno << ")\n";
}

bool SensorUplinkReceiver::ensureListening() {
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
    std::cerr << "[SensorUplinkReceiver] listen 시작 - "
              << (bind_host_.empty() ? std::string("0.0.0.0") : bind_host_)
              << ':' << port_ << " (단말 접속 대기)\n";
    return true;
}

void SensorUplinkReceiver::acceptOne() {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &peer_len);
    if (fd < 0) return;   // EAGAIN(select가 헛readiness) 등 -> 다음 루프에서 재시도

    // 센서 한 줄은 짧아서 Nagle 지연(최대 40ms)이 업링크 지연에 그대로 얹힌다.
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char ip[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));

    conn_fd_      = fd;
    buf_.clear();
    discarding_   = false;
    hello_done_   = false;
    connected_at_ = std::chrono::steady_clock::now();
    ++accepted_;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        connected_ = true;
    }
    std::cerr << "[SensorUplinkReceiver] 단말 접속 - " << ip << ':' << ntohs(peer.sin_port)
              << " (hello 대기, fd=" << fd << ")\n";
}

void SensorUplinkReceiver::closeConnection() {
    if (conn_fd_ >= 0) { ::close(conn_fd_); conn_fd_ = -1; }
    buf_.clear();
    discarding_ = false;
    hello_done_ = false;

    std::lock_guard<std::mutex> lk(mtx_);
    connected_ = false;
    // latest_/terminal_id_는 지우지 않는다. 끊겼다는 사실은 isStale()이 시간으로 말해주고,
    // "마지막으로 어느 단말에서 무슨 값이 왔는지"는 끊긴 뒤에도 로깅/디버그에 필요하다.
}

void SensorUplinkReceiver::closeListener() {
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
}

void SensorUplinkReceiver::logParseFailure(const std::string& why, const std::string& line) {
    // 카운터는 매번 정확히 증가시키고 stderr만 rate limit한다. 단말이 깨진 줄을 계속
    // 흘리면 건건이 찍는 순간 unbuffered stderr가 수신 루프를 붙잡는다
    // (ResultPublisher의 드랍 로그와 같은 이유·같은 정책).
    const std::size_t total = ++parse_failures_;
    if (total == 1) {
        std::cerr << "[SensorUplinkReceiver] 파싱 실패로 1줄 버림 (" << why
                  << ") - 수신 계속 (누적 실패=" << total << ", 수신: " << line << ")\n";
        last_logged_parse_failure_total_ = total;
    } else if (total - last_logged_parse_failure_total_ >= parse_log_interval_) {
        std::cerr << "[SensorUplinkReceiver] dropped " << (total - last_logged_parse_failure_total_)
                  << " more (total: " << total << ") - 마지막 사유: " << why << "\n";
        last_logged_parse_failure_total_ = total;
    }
}

void SensorUplinkReceiver::handleLine(const std::string& line) {
    if (!hello_done_) {
        std::string terminal_id;
        if (!parseHello(line, terminal_id)) {
            // hello는 CameraAssignmentServer와 같은 규약으로 실패 시 연결을 끊는다.
            // 센서 줄과 정책이 다른 이유: hello가 없으면 이 연결이 어느 단말 것인지 영원히
            // 알 수 없어서 뒤에 오는 값을 전부 못 믿는다(줄 하나 버리고 넘어갈 문제가 아니다).
            std::cerr << "[SensorUplinkReceiver] hello 파싱 실패 또는 type != hello - "
                      << "연결 종료 (수신: " << line << ")\n";
            closeConnection();
            return;
        }
        hello_done_ = true;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            terminal_id_ = terminal_id;
        }
        std::cerr << "[SensorUplinkReceiver] hello 수신 - terminal_id=" << terminal_id
                  << " 등록 완료 (fd=" << conn_fd_ << ")\n";
        return;
    }

    SensorUplinkSample sample;
    std::string why;
    if (!parseSample(line, sample, why)) {
        // 깨진 줄 하나 때문에 연결을 끊거나 죽으면 안 된다 - 그 줄만 버리고 계속 받는다.
        logParseFailure(why, line);
        return;
    }

    sample.received_at = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        latest_     = sample;
        has_sample_ = true;
    }
    ++received_;
}

void SensorUplinkReceiver::handleReadable() {
    char chunk[2048];
    ssize_t n = ::recv(conn_fd_, chunk, sizeof(chunk), MSG_DONTWAIT);
    if (n == 0) {
        std::cerr << "[SensorUplinkReceiver] 단말 연결 종료 감지 - 재대기\n";
        closeConnection();
        return;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;   // select 헛readiness
        std::cerr << "[SensorUplinkReceiver] recv 실패 (errno=" << errno << ") - 재대기\n";
        closeConnection();
        return;
    }
    buf_.append(chunk, static_cast<std::size_t>(n));

    for (;;) {
        auto pos = buf_.find('\n');
        if (pos == std::string::npos) {
            // 개행이 안 오는데 버퍼만 계속 커지면 스트림이 어긋난 것으로 본다.
            // 연결을 끊는 대신 다음 개행까지 버리고 재동기화한다(끊으면 정상 줄까지 잃는다).
            if (buf_.size() > kMaxLineBytes) {
                if (!discarding_) {
                    std::cerr << "[SensorUplinkReceiver] 한 줄이 너무 큼(> " << kMaxLineBytes
                              << "B) - 다음 개행까지 버리고 재동기화\n";
                    discarding_ = true;
                }
                buf_.clear();
            }
            return;
        }

        std::string line = buf_.substr(0, pos);
        buf_.erase(0, pos + 1);

        if (discarding_) { discarding_ = false; continue; }   // 잘린 앞부분 잔여 -> 버림

        if (!line.empty() && line.back() == '\r') line.pop_back();   // CRLF 방어
        if (line.empty()) continue;                                  // 빈 줄(keep-alive 개행)은 무시

        handleLine(line);
        if (conn_fd_ < 0) return;   // handleLine()이 연결을 끊었다(hello 실패)
    }
}

void SensorUplinkReceiver::run() {
    while (running_) {
        if (!ensureListening()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(listen_retry_ms_));
            continue;
        }

        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;

        // 1:1 채널이라 연결 중에는 listen 소켓을 감시하지 않는다. 추가 접속은 백로그에
        // 남아 있다가 기존 연결이 끊긴 뒤에 accept된다(ResultPublisher와 같은 전제).
        if (conn_fd_ < 0) {
            FD_SET(listen_fd_, &rfds);
            maxfd = listen_fd_;
        } else {
            FD_SET(conn_fd_, &rfds);
            maxfd = conn_fd_;
        }

        timeval tv{0, static_cast<suseconds_t>(select_poll_ms_) * 1000};
        int ready = ::select(maxfd + 1, &rfds, nullptr, nullptr, &tv);

        if (ready > 0) {
            if (conn_fd_ < 0) {
                if (FD_ISSET(listen_fd_, &rfds)) acceptOne();
            } else if (FD_ISSET(conn_fd_, &rfds)) {
                handleReadable();
            }
        }

        // hello 타임아웃 검사 - select 결과와 무관하게 매 루프마다 확인한다.
        if (conn_fd_ >= 0 && !hello_done_) {
            const auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::steady_clock::now() - connected_at_).count();
            if (elapsed_s >= kHelloTimeoutSec) {
                std::cerr << "[SensorUplinkReceiver] hello 타임아웃(" << kHelloTimeoutSec
                          << "s) - 연결 종료 (fd=" << conn_fd_ << ")\n";
                closeConnection();
            }
        }
    }
}

} // namespace risk_transport
