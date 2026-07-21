// test_aruco_parser.cpp
//
// 더미 XML 픽스처로 ArUco 파서 단위 테스트.
//   검증: MarkerCount=3, IDs=[40,38,24], 각 마커 코너 4개가 정확히 파싱되는지.
//   추가: aruco_markers.csv 로 정상 기록되는지.
//
// gtest 등 외부 프레임워크 없이 object_detection/test_main.cpp 처럼 순수 main 으로 작성.
// 실패 시 프로세스 종료코드 1 을 반환한다 (CTest 연동용).
#include "aruco_metadata_parser.hpp"
#include "aruco_csv_logger.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

static int g_failures = 0;

static void checkTrue(bool cond, const std::string& what) {
    if (!cond) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    } else {
        std::cout << "[ OK ] " << what << "\n";
    }
}

static void checkClose(double actual, double expected, const std::string& what) {
    const double eps = 1e-6;
    if (std::fabs(actual - expected) > eps) {
        std::cerr << "[FAIL] " << what
                  << " (기대=" << expected << ", 실제=" << actual << ")\n";
        ++g_failures;
    } else {
        std::cout << "[ OK ] " << what << " = " << actual << "\n";
    }
}

int main() {
    // ── 테스트용 더미 XML 픽스처 (과제 제공 샘플) ──
    const std::string dummyXml = R"(
    <tt:MetadataStream>
      <tt:Event>
        <wsnt:NotificationMessage>
          <wsnt:Topic Dialect="http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet">tns1:OpenApp/ArUCoDummyMetadata/MarkerDetected</wsnt:Topic>
          <wsnt:Message>
            <tt:Message UtcTime="2026-07-21T05:57:03.406Z">
              <tt:Source>
                <tt:SimpleItem Name="Channel" Value="0"/>
              </tt:Source>
              <tt:Key/>
              <tt:Data>
                <tt:SimpleItem Name="MarkerCount" Value="3"/>
                <tt:SimpleItem Name="MarkerIds" Value="40,38,24"/>
                <tt:SimpleItem Name="Marker0Corners" Value="1621.33,139.06,1709.2,141.817,1705.95,223.375,1621.72,221.938"/>
                <tt:SimpleItem Name="Marker1Corners" Value="1149.8,325.523,1262.09,329.126,1260.2,438.925,1150.12,440.001"/>
                <tt:SimpleItem Name="Marker2Corners" Value="165.939,451.573,273.734,448.928,275.618,566.179,164.685,566.143"/>
              </tt:Data>
            </tt:Message>
          </wsnt:Message>
        </wsnt:NotificationMessage>
      </tt:Event>
    </tt:MetadataStream>
    )";

    // ── 1) 토픽 분기 + 파싱 ──
    std::optional<ArucoFrame> parsed = parseArucoMetadata(dummyXml);
    checkTrue(parsed.has_value(), "MarkerDetected 토픽이 파싱됨");
    if (!parsed) return 1;

    const ArucoFrame& frame = *parsed;

    // ── 2) 프레임 메타 ──
    checkTrue(frame.utcTime == "2026-07-21T05:57:03.406Z", "UtcTime 파싱");
    checkTrue(frame.channel == 0, "Channel = 0");
    checkTrue(frame.markers.size() == 3, "마커 개수 = 3");
    if (frame.markers.size() != 3) return 1;

    // ── 3) 마커 ID ──
    const int expectedIds[3] = { 40, 38, 24 };
    for (int i = 0; i < 3; ++i) {
        checkTrue(frame.markers[i].id == expectedIds[i],
                  "Marker[" + std::to_string(i) + "].id = " + std::to_string(expectedIds[i]));
    }

    // ── 4) 코너 좌표 (TL, TR, BR, BL 순 가정 — 확인 필요) ──
    const double expectedCorners[3][8] = {
        { 1621.33, 139.06,  1709.2,  141.817, 1705.95, 223.375, 1621.72, 221.938 },
        { 1149.8,  325.523, 1262.09, 329.126, 1260.2,  438.925, 1150.12, 440.001 },
        { 165.939, 451.573, 273.734, 448.928, 275.618, 566.179, 164.685, 566.143 },
    };
    for (int m = 0; m < 3; ++m) {
        for (int c = 0; c < 4; ++c) {
            std::string tag = "Marker[" + std::to_string(m) + "].corner[" + std::to_string(c) + "]";
            checkClose(frame.markers[m].corners[c].first,  expectedCorners[m][c * 2],     tag + ".x");
            checkClose(frame.markers[m].corners[c].second, expectedCorners[m][c * 2 + 1], tag + ".y");
        }
    }

    // ── 5) 다른 토픽은 무시되는지 (음성 케이스) ──
    const std::string humanXml = R"(
    <tt:MetadataStream>
      <tt:Event>
        <wsnt:NotificationMessage>
          <wsnt:Topic>tns1:VideoAnalytics/tnsaxis:ObjectDetection/Human</wsnt:Topic>
          <wsnt:Message><tt:Message UtcTime="2026-07-21T05:57:03Z"/></wsnt:Message>
        </wsnt:NotificationMessage>
      </tt:Event>
    </tt:MetadataStream>
    )";
    checkTrue(!parseArucoMetadata(humanXml).has_value(),
              "MarkerDetected 아닌 토픽은 nullopt");

    // ── 6) CSV 로깅 확인 ──
    const std::string csvPath = "aruco_markers.csv";
    std::remove(csvPath.c_str());  // 이전 실행 잔여물 제거 (테스트 재현성)
    {
        ArucoCsvLogger logger(csvPath);
        logger.logFrame(frame);
    }  // 소멸자에서 flush/close

    std::ifstream csv(csvPath);
    checkTrue(csv.good(), "aruco_markers.csv 생성됨");
    int lineCount = 0;
    std::string line, firstDataLine;
    while (std::getline(csv, line)) {
        if (lineCount == 1) firstDataLine = line;  // 헤더 다음 첫 데이터 줄
        ++lineCount;
    }
    checkTrue(lineCount == 4, "CSV 줄 수 = 4 (헤더 1 + 마커 3)");
    checkTrue(firstDataLine.rfind("2026-07-21T05:57:03.406Z,0,40,", 0) == 0,
              "CSV 첫 데이터 줄이 utc,channel,marker_id 로 시작");

    // ── 결과 ──
    std::cout << "\n=== " << (g_failures == 0 ? "ALL PASSED" : "FAILED")
              << " (실패 " << g_failures << "건) ===\n";
    return g_failures == 0 ? 0 : 1;
}
