// csv_logger.cpp
#include "logging/csv_logger.hpp"
#include <chrono>
#include <iostream>

CsvLogger::CsvLogger(const std::string& filePath) {
    // 파일이 이미 있었는지 미리 확인 -> 있으면 헤더를 또 안 씀 (이어붙이기 모드)
    bool fileExists = std::ifstream(filePath).good();

    file_.open(filePath, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[경고] CSV 파일을 열 수 없음: " << filePath << "\n";
        return;
    }

    if (!fileExists) {
        file_ << "camera_utc,server_received_utc,delta_ms,object_id,parent_id,class,likelihood,"
                 "bbox_left,bbox_top,bbox_right,bbox_bottom,"
                 "ground_x,ground_y,stream_id,camera_id,channel\n";
    }
}

CsvLogger::~CsvLogger() {
    if (file_.is_open()) file_.close();
}

void CsvLogger::logFrame(const MetadataFrame& frame) {
    if (!file_.is_open()) return;
    if (frame.objects.empty()) return;  // 빈 프레임(이벤트 등)은 기록 안 함

    for (const auto& obj : frame.objects) {
        file_ << frame.utcTime << ","
              << frame.serverReceivedUtc << ","
              << frame.deltaMs << ","
              << obj.objectId << ","
              << obj.parentId << ","
              << obj.classInfo.type << ","
              << obj.classInfo.likelihood << ","
              << obj.bbox.left << "," << obj.bbox.top << ","
              << obj.bbox.right << "," << obj.bbox.bottom << ","
              << obj.bbox.groundX() << "," << obj.bbox.groundY() << ","
              << frame.stream_id << "," << frame.camera_id << "," << frame.channel << "\n";
    }

    static auto last_flush = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    if (now - last_flush >= std::chrono::seconds(1)) {
        file_.flush();
        last_flush = now;
    }
}
