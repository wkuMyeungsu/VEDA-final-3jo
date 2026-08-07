# Server

카메라의 ONVIF 메타데이터를 수신해 객체와 ArUco 마커를 파싱하고,
좌표 변환·추적·위험 판정을 거쳐 단말로 결과를 전달하는 서버이다.

## 목표 데이터 흐름

```text
카메라 RTSP 메타데이터
  -> RTP 패킷 수신 및 XML 재조립
  -> 토픽별 파싱 (객체 검출 / ArUco)
  -> 공통 타입으로 정규화
  -> 시각 동기화 및 좌표 변환
  -> 사람 추적 및 최근접 대상 선택
  -> 위험도 판정
  -> 운전자·관제 단말 전송
```

## 디렉터리 구조

```text
server/
├── CMakeLists.txt                    # 루트 단일 CMake 빌드 스크립트
├── README.md
├── .gitignore                        # *.db / *.csv / build 산출물 등 무시
│
├── config/                           # 설정 파일 (JSON 등)
│   ├── camera_config.json
│   ├── terminal_TERM_01.json         # 단말 1대분 설정 (단말당 파일 1개)
│   └── README.md
│
├── third_party/                      # 벤더링한 외부 라이브러리 (수정 금지)
│   ├── nlohmann/json.hpp             # JSON 파서 single-header v3.11.3 (MIT)
│   └── README.md                     # 출처·버전·왜 벤더링인지
│
├── tests/                            # 테스트 코드만 독립 보관
│   ├── CMakeLists.txt
│   ├── common_types_test.cpp
│   ├── test_onvif_metadata_parser.cpp
│   ├── test_aruco_metadata_parser.cpp
│   ├── test_metadata_router.cpp
│   ├── test_terminal_config.cpp
│   ├── test_marker_channel_tracker.cpp
│   ├── test_judgment_pipeline.cpp
│   ├── test_exception_trigger.cpp    # ctest 타깃명: judgment_engine_test
│   ├── test_event_logger.cpp
│   ├── test_result_publisher.cpp
│   ├── test_camera_assignment_server.cpp
│   └── sensor_fusion_smoke_test.cpp  # [빌드 미포함] 라즈베리파이 실기 전용 HW 스모크 테스트
│
├── _tools/                           # 보조 도구 (실행 파이프라인과 무관)
│   ├── event_log_viewer_main.cpp     # 이벤트 DB 조회용 실행파일
│   └── analyze_detections.py         # 검출 CSV 분석 스크립트
│
└── src/                              # 모든 실제 소스 코드
    ├── app/
    │   └── main.cpp                  # 유일한 서버 실행 진입점 (RTSP 수신 조립)
    │
    ├── common/                       # 공통 타입 및 유틸
    │   ├── types.hpp
    │   ├── metadata_timing.hpp
    │   └── latency_stamps.hpp        # 서버 내부 지연 계측 스탬프
    │
    ├── config/                       # 설정 파일 로드 (config/*.json -> 구조체)
    │   └── terminal_config.*         # 단말별 마커 ID / 단말 ID / 핸드오버 파라미터
    │
    ├── input/                        # 데이터 수신 및 파싱 (RTSP, ONVIF, ArUco)
    │   ├── rtp_metadata_receiver.*   # RTP 헤더 파싱 / XML 조각 재조립
    │   ├── onvif_metadata_parser.*   # 객체탐지(VideoAnalytics) XML 파서
    │   ├── aruco_metadata_parser.*   # ArUco 마커 XML 파서
    │   ├── metadata_router.*         # XML 종류 분기(라우팅)
    │   └── sensor_collector_reader.* # [빌드 미포함] IMU/ToF 어댑터 (외부 헤더 대기, 미연결)
    │
    ├── logic/                        # 핵심 연산 (좌표변환, 추적, 위험판정)
    │   ├── homography/               # 호모그래피 좌표 변환 (README 자리만 확보)
    │   ├── tracking/
    │   │   ├── nearest_person_selector.*
    │   │   ├── marker_channel_tracker.* # 지게차 마커가 보이는 카메라 채널 판정(핸드오버)
    │   │   └── cross_camera_reid.cpp # [빌드 미포함] 카메라 간 ID 유지(Re-ID), 미완성 짝
    │   └── judgment/
    │       ├── danger_judgment_engine.*
    │       └── judgment_pipeline.*
    │
    ├── network/                      # 외부 통신 (TCP 소켓, 디스패처)
    │   ├── result_publisher.*
    │   └── result_dispatcher.hpp
    │
    └── logging/                      # 로그 & DB 기록 (쓰기 전용 로거 코드)
        ├── csv_logger.*              # 객체탐지 메타데이터 프레임 → CSV
        ├── aruco_csv_logger.*        # ArUco 프레임 → CSV
        ├── event_logger.*            # 위험 판정 이벤트 → SQLite
        └── latency_logger.*          # 지연 계측 → CSV
```

> `*` 는 `.cpp`/`.hpp`(일부 `.h`) 쌍을 뜻한다. `[빌드 미포함]`은 현재 CMake
> 대상이 아닌 보존 파일이다. 실행 중 생성되는 데이터 디렉터리는 아래 "런타임 데이터" 참고.

```bash
cmake -S . -B build
cmake --build build -j2
./build/forklift_safety_server "rtsp://<user>:<password>@192.168.0.3:554/0/onvif/profile2/media.smp"
cd build && ctest --output-on-failure
```

두 번째 인자로 단말 설정 파일 경로를 줄 수 있다. 생략하면 `TERM_01` 기본 파일을
`config/` → `../config/` → `server/config/` 순으로 찾는다(실행 위치에 따라 상대경로가
달라지는 문제를 완화하기 위한 것으로, 아래 "런타임 데이터"의 CWD 주의사항과 같은 성격이다).
설정을 못 읽으면 서버는 그 자리에서 종료된다(종료코드 2).

```bash
./build/forklift_safety_server "rtsp://..." config/terminal_TERM_02.json
```

## 빌드 구성 (CMake)

```text
server_input    ← src/input/*                        (pugixml)
server_logging  ← src/logging/*                      (SQLite3, Threads)
server_config   ← src/config/*                       (third_party/nlohmann, PRIVATE)
server_tracking ← src/logic/tracking/nearest_person_selector.cpp
                                     marker_channel_tracker.cpp
server_judgment ← src/logic/judgment/*               (→ server_tracking)
server_network  ← src/network/result_publisher.cpp   (→ server_judgment)

실행파일: forklift_safety_server  (src/app/main.cpp)
          event_log_viewer        (_tools/event_log_viewer_main.cpp)
```

`json.hpp`는 헤더 하나가 약 900KB라 빌드 시간에 영향을 준다. `server_config`
타깃에서만 PRIVATE으로 include하고 다른 계층 헤더에는 노출하지 않는다
(`terminal_config.hpp`의 공개 API에 nlohmann 타입이 하나도 없는 이유).

## 카메라 자동 전환 (핸드오버)

지게차에 붙은 ArUco 마커가 어느 카메라에 보이는지를 `MarkerChannelTracker`가 추적하고,
액티브 채널이 **실제로 바뀌는 순간에만** 두 가지가 일어난다.

1. `JudgmentPipeline::setActiveCameraId()` — 이후 판정 결과 JSON의 `camera_id`가 새 채널로 나간다
2. `CameraAssignmentServer::sendCameraAssignment()` — 9001 채널로 단말 화면 전환 지시

판정 규칙(설정 `handover` 절에서 조정):

```text
confirm_frames  연속 검출 이만큼 쌓인 카메라만 "확정 후보"     (기본 3)
lost_grace_ms   액티브가 마커를 놓쳐도 이 시간까지는 유지      (기본 500ms)

전환 조건 = 액티브에서 마커를 마지막으로 본 지 lost_grace_ms 초과
            && 다른 카메라가 확정 후보
```

액티브를 새로 잡는 건 즉시, 잡고 있던 걸 놓는 건 신중하게 — `DangerJudgmentEngine`의
EMERGENCY 히스테리시스와 같은 원칙을 시간 축에 적용한 것이다. 서버 기동 직후처럼 아직
어느 카메라에서도 마커가 확정되지 않은 구간에서는 `camera_id`가 `null`로 나간다.

## 런타임 데이터

로거 코드는 `src/logging/`에 있고, 실행 중 남기는 데이터는 아래 디렉터리에 쌓인다.
소스 트리가 아니라 런타임 산출물이며 `.gitignore`로 제외된다.

```text
storage/
  events.db     # 위험 판정 이벤트 (SQLite)
  latency.csv   # 지연 계측 기록
```

- 현재 경로는 상대경로라 **실행 시점의 작업 디렉터리(CWD) 기준**으로 `storage/`가
  자동 생성된다. 예: `server/`에서 실행하면 `server/storage/`, `build/`에서 실행하면
  `build/storage/`. 즉 실행 위치에 따라 데이터가 흩어질 수 있다.
- TODO(배포): CWD에 흔들리지 않도록 데이터 루트 경로를 고정/주입하는 방식(절대경로
  주입 또는 `config/` 기반 설정)은 **배포 설정 단계에서 별도로 확정**한다. 구조
  리팩터링 범위에서는 다루지 않는다.

## ArUco 메타데이터 형식

- Topic: `tns1:OpenApp/ArUCo_Detection/MarkerDetected`
- Source: `Channel` (1부터 시작)
- Message: `UtcTime`
- Data: `MarkerCount`, `MarkerIds`, `Marker{N}Corners`

코너는 원본 픽셀 좌표 `x0,y0,x1,y1,x2,y2,x3,y3` 순서를 그대로 보존한다.
`MarkerCount=0`은 오류가 아니라 해당 시점의 미검출로 처리한다. 카메라 UTC 시각과
서버 수신 시각은 모두 기록한다.

## 구현 순서

1. 기존 모듈별 역할·의존성과 새 위치를 확정한다.
2. 객체·ArUco 토픽 분기와 공통 입력 타입 변환을 연결한다.
3. 채널별 호모그래피 설정, 좌표 변환, 오차 검증 도구를 추가한다.
4. 추적·최근접 대상 선택·위험 판정을 새 진입점에 통합한다.
5. 현장 고정 후 채널별 행렬과 위험 임계값을 실측값으로 확정한다.

호모그래피 최종값은 카메라 위치·각도·줌·해상도와 바닥 기준점이 고정된 뒤에만
산출한다. 그전에는 샘플 설정과 가상 점으로 저장·로드·변환·검증 흐름을 준비한다.
