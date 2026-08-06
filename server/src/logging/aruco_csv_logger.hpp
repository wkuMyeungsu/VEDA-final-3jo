// aruco_csv_logger.hpp
//
// 입력값: ArucoFrame (파서가 뽑아낸 프레임 하나, 마커 여러 개 포함)
// 처리과정: 프레임 안의 마커마다 한 줄씩 CSV 형식으로 파일에 이어붙임
// 출력값: 누적된 aruco_markers.csv (엑셀/pandas 로 바로 분석 가능)
//
// object_detection/csv_logger 의 CsvLogger 패턴을 참고했다 (append + 헤더 1회 + flush).
#pragma once
#include <fstream>
#include <string>
#include "input/aruco_metadata_parser.hpp"

class ArucoCsvLogger {
public:
    // filePath: 저장할 CSV 경로. 파일이 이미 있으면 헤더 없이 이어붙이고,
    //           없으면 새로 만들면서 헤더 한 줄을 먼저 씀.
    explicit ArucoCsvLogger(const std::string& filePath);
    ~ArucoCsvLogger();

    // frame 하나를 받아서 그 안의 마커들을 한 줄씩 파일에 추가
    void logFrame(const ArucoFrame& frame);

private:
    std::ofstream file_;
};
