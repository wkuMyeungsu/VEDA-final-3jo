// cross_camera_reid.h
// 카메라 간 객체 ID 유지 (크로스카메라 Re-ID) - 공개 인터페이스
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 목적: 카메라 핸드오버(A -> B) 시 동일 인물에게 동일 tracking ID를 유지시킨다.
//   - 기본: 같은 카메라 내 프레임 간 연속성은 IoU(bbox 겹침 비율)로 매칭
//   - 카메라 전환(핸드오버) 시점: world 좌표(호모그래피 변환 결과) 근접도로 매칭
// 매칭 로직 구현은 cross_camera_reid.cpp 참고. 이 헤더는 데이터 구조와
// CrossCameraTracker의 공개 인터페이스만 선언한다.
//
// BoundingBox/WorldPoint/Track은 여기서 새로 정의하지 않고 기존 타입을 재사용한다:
//   - BoundingBox: input/onvif_metadata_parser.hpp (ONVIF 파서 출력, 픽셀 좌표)
//   - Track/WorldPoint: logic/tracking/nearest_person_selector.h
//     (nearest_person_selector가 이 트래커의 출력(update()의 반환값)을 그대로
//      받아 쓰므로, 두 모듈이 Track 타입을 공유해야 변환 코드가 필요 없다.)

#pragma once

#include <string>
#include <vector>

#include "input/onvif_metadata_parser.hpp"            // BoundingBox
#include "logic/tracking/nearest_person_selector.h"   // Track, WorldPoint

// 한 프레임에서 들어온 검출 결과 (아직 track_id 없음)
struct Detection {
    std::string stream_id;
    std::string camera_id;
    int         channel = -1;
    BoundingBox bbox;    // 자기 카메라 픽셀 좌표 (ONVIF BoundingBox 그대로)
    WorldPoint  world;   // world 좌표 (카메라가 달라도 비교 가능)
    double      timestamp_s;
    // 원본 메타데이터의 UTC 시각. runtime snapshot에서 트랙의 마지막
    // 실제 검출 시각을 표시할 때 사용한다.
    std::string observed_utc;
};

// 크로스카메라 트래커. 한 프레임의 검출 목록을 넣으면 IoU(동일 카메라)/world 거리
// (카메라 전환 포함) 매칭으로 track_id를 유지한 트랙 목록을 돌려준다.
class CrossCameraTracker {
public:
    CrossCameraTracker(double iou_threshold, double world_distance_threshold_mm,
                       double track_timeout_sec);

    // 한 프레임의 검출 목록을 받아 트랙을 갱신하고, 현재 트랙 목록을 반환
    std::vector<Track> update(const std::vector<Detection>& detections, double now_s);

private:
    // 추적 기준은 공통 JSON에서 생성 시 주입받고 실행 중에는 변경하지 않는다.
    const double iou_threshold_;
    const double world_distance_threshold_mm_;
    const double track_timeout_sec_;
    std::vector<Track> tracks_;
    int next_id_ = 1;
};
