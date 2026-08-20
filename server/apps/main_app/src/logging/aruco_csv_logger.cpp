// aruco_csv_logger.cpp
#include "logging/aruco_csv_logger.hpp"
#include "logging/logger.hpp"
#include <chrono>
#include <iostream>

ArucoCsvLogger::ArucoCsvLogger(const std::string& filePath) {
    // 파일이 이미 있었는지 미리 확인 -> 있으면 헤더를 또 안 씀 (이어붙이기 모드)
    bool fileExists = std::ifstream(filePath).good();

    file_.open(filePath, std::ios::app);
    if (!file_.is_open()) {
        LOG_WARN("DEBUG", "마커 검출 CSV 파일을 열 수 없음 - " + filePath);
        return;
    }

    if (!fileExists) {
        file_ << "camera_utc,server_received_utc,delta_ms,channel,marker_id,stream_id,camera_id,"
                 "x0,y0,x1,y1,x2,y2,x3,y3\n";
    }
    LOG_INFO("DEBUG", "마커 검출 원시 로그 시작 - " + filePath);
}

ArucoCsvLogger::~ArucoCsvLogger() {
    if (file_.is_open()) file_.close();
}

void ArucoCsvLogger::logFrame(const ArucoFrame& frame) {
    if (!file_.is_open()) return;
    if (frame.markers.empty()) return;  // 마커 없는 프레임은 기록 안 함

    for (const auto& m : frame.markers) {
        file_ << frame.utcTime << ","
              << frame.serverReceivedUtc << ","
              << frame.deltaMs << ","
              << frame.channel << ","
              << m.id << ","
              << frame.stream_id << ","
              << frame.camera_id;
        for (int c = 0; c < 4; ++c) {
            file_ << "," << m.corners[c].x
                  << "," << m.corners[c].y;
        }
        file_ << "\n";
    }

    static auto last_flush = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - last_flush >= std::chrono::seconds(1)) {
        file_.flush();
        last_flush = now;
    }
}
