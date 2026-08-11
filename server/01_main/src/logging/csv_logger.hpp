// csv_logger.hpp
//
// 입력값: MetadataFrame (파서가 뽑아낸 프레임 하나, 여러 객체 포함)
// 처리과정: 프레임 안의 객체마다 한 줄씩 CSV 형식으로 파일에 이어붙임
// 출력값: 누적된 CSV 파일 (엑셀이나 Python pandas로 바로 열어서 분석 가능)
#pragma once
#include <fstream>
#include <string>
#include "input/onvif_metadata_parser.hpp"

class CsvLogger {
public:
    // filePath: 저장할 CSV 경로. 파일이 이미 있으면 헤더 없이 이어붙이고,
    //           없으면 새로 만들면서 헤더 한 줄을 먼저 씀.
    explicit CsvLogger(const std::string& filePath);
    ~CsvLogger();

    // frame 하나를 받아서 그 안의 객체들을 한 줄씩 파일에 추가
    void logFrame(const MetadataFrame& frame);

private:
    std::ofstream file_;
};
