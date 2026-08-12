// aruco_csv_logger.cpp
#include "logging/aruco_csv_logger.hpp"
#include <iostream>

ArucoCsvLogger::ArucoCsvLogger(const std::string& filePath) {
    // 파일이 이미 있었는지 미리 확인 -> 있으면 헤더를 또 안 씀 (이어붙이기 모드)
    bool fileExists = std::ifstream(filePath).good();

    file_.open(filePath, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[경고] CSV 파일을 열 수 없음: " << filePath << "\n";
        return;
    }

    if (!fileExists) {
        file_ << "camera_utc,server_received_utc,delta_ms,channel,marker_id,"
                 "x0,y0,x1,y1,x2,y2,x3,y3\n";
    }
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
              << m.id;
        for (int c = 0; c < 4; ++c) {
            file_ << "," << m.corners[c].x
                  << "," << m.corners[c].y;
        }
        file_ << "\n";
    }
    file_.flush();  // 중간에 프로그램이 꺼져도 지금까지 쓴 줄은 보존되게
}
