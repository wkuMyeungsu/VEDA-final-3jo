// nearest_person_selector.h
// 지게차 최근접 사람 선택 - 공개 인터페이스 (데이터 구조 + 선택 로직 + 출력 헬퍼 선언)
// 담당: 검출·추적 & IMU·ToF 센서 (박수빈)
//
// 목적: cross_camera_reid.cpp가 돌려주는 여러 명의 Track 중, 특정 지게차에서 가장 가까운
//       사람 1명만 골라 danger_judgment_engine의 CameraInput.person으로 넘길 값을 만든다.
//       (박명수 파트가 "사람 중복 제거는 안 한다"고 결정했으므로, 여러 명 중 1명을
//        추리는 책임은 이쪽에 있다 - 워크가이드에 명시된 결정사항.)
//
// 구현은 nearest_person_selector.cpp, 독립 실행용 main()은
// nearest_person_selector_main.cpp에 있다. 이 헤더는 표준 헤더 + 엔진 헤더만 쓰므로
// POSIX 의존성이 없다.
//
// ── WorldPoint 정의 위치 주의 ────────────────────────────────
// 예전에는 nearest_person_selector.cpp가 WorldPoint를 자체 정의했다. 그런데
// judgment_pipeline이 이 헤더와 danger_judgment_engine.h를 같은 TU에서 include하는
// 순간 같은 이름의 구조체가 두 번 정의돼 컴파일이 안 된다. 그래서 여기서는 정의하지 않고
// danger_judgment_engine.h 쪽 정의를 재사용한다 (원래도 두 정의의 필드 구성이 동일했으므로
// 동작 차이는 없다).
//
// [교체 지점] 원래 계획대로 server/common/types.hpp가 생기면 WorldPoint를 거기로 옮기고
//             이 헤더와 danger_judgment_engine.h가 모두 그걸 include하게 바꾼다.
//             지금은 엔진 헤더가 수정 범위 밖이라 "추적 -> 판정" 방향으로 헤더를 참조한다.
//             심볼은 필요 없고 타입 정의만 필요하므로 라이브러리 링크 의존성은 아니다.

#pragma once

#include <limits>
#include <string>
#include <vector>

#include "logic/judgment/danger_judgment_engine.h"   // WorldPoint

// ============================================================
// 1. 데이터 구조 정의
// ============================================================

// cross_camera_reid.cpp의 Track과 동일한 필드 구성 (실제로는 그쪽 출력이 그대로 들어옴)
//
// [주의] 지금 cross_camera_reid.cpp의 Track은 여기보다 필드가 많다(last_bbox, last_seen_s).
//        두 파일을 한 실행파일에 링크하면 같은 이름의 서로 다른 정의가 되어 ODR 위반이다.
//        cross_camera_reid.cpp를 빌드에 넣을 때 Track 정의를 이 헤더로 통일해야 한다.
//        (그 파일은 지금 어떤 CMake 타깃에도 안 들어가 있어 당장 문제는 없다.)
struct Track {
    int        track_id;
    int        camera_id;
    WorldPoint last_world;
    int        missed_frames = 0;
};

// 최근접 사람 선택 결과
struct NearestPersonResult {
    bool       found = false;
    int        track_id = -1;
    int        camera_id = -1;   // 선택된 트랙을 보고 있던 카메라 (found=false면 -1 유지)
    WorldPoint position;
    double     distance_m = std::numeric_limits<double>::max();
};

// ============================================================
// 2. 선택 로직
// ============================================================

// [주의] cross_camera_reid.cpp도 같은 이름의 전역 함수를 정의하고 있다. 그 파일을
//        빌드에 넣으면 중복 정의로 링크가 깨지므로, 그때 이 헤더 쪽으로 통일해야 한다.
double euclideanDistance(const WorldPoint& a, const WorldPoint& b);

// 지게차 world 좌표 기준, tracks 중 가장 가까운 사람 1명을 찾는다.
// missed_frames > 0인 트랙(이번 프레임에 실제로 검출 안 된 트랙)은 후보에서 제외한다.
NearestPersonResult selectNearestPerson(const WorldPoint& forklift,
                                        const std::vector<Track>& tracks);

// ============================================================
// 3. 출력 헬퍼
// ============================================================

// 선택 결과를 콘솔에 여러 줄로 출력 (시나리오 이름 + 지게차 위치 + 선택된 트랙).
// danger_judgment_engine.h의 printResult(scenario, JudgmentResult)와는 인자 구성이
// 달라 오버로드로 공존한다(같은 TU에서 둘 다 보여도 모호하지 않다).
void printResult(const std::string& scenario, const WorldPoint& forklift,
                 const NearestPersonResult& r);
