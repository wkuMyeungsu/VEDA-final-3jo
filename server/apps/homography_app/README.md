# 호모그래피 앱

등록된 CCTV의 모든 채널에서 ArUco 마커를 검출하고, 채널별 픽셀 좌표를
공통 월드 좌표(mm)로 변환하는 보정 도구다. 웹 앱과 C++ 처리 엔진을 분리해
구성하며, 최종 H 결과는 안전 서버가 직접 읽는다.

## 한눈에 보기

- 웹 UI/API: `web/`, 기본 `0.0.0.0:8001`
- 처리 엔진: `processing/`, C++17·OpenCV 기반 `homography_tool`
- 입력: CCTV별 채널 이미지, ArUco marker ID와 네 꼭짓점
- 보정: 채널별 local H 산출, 공통 marker 기반 전체 스트림 정합
- 출력: `server/config/homography/<camera_id>/`의 운영 H JSON
- 공통 설정: `server/config/camera_model.json`, `camera_list.json`
- 임시 결과: `/tmp/homography-results/<capture_id>`
- 좌표 단위: `mm`

## 구조

### 목록

- `web`: CCTV 선택·캡처·검출·정합을 제공하는 HTTP UI/API
- `processing`: marker 생성·검출·local H·두 이미지 정합 CLI
- `tests`: 전역 정합과 웹 흐름 검증
- `config/homography`: marker 사전, 정합 정책, 검증 화면 설정

### 상세

```text
apps/homography_app/
├── README.md
├── web/
│   ├── server.py                  # HTTP UI/API
│   ├── static/                    # 브라우저 UI
│   └── README.md
├── processing/
│   ├── main.cpp                   # homography_tool CLI
│   ├── src/                       # OpenCV 계산·JSON·렌더링
│   ├── tests/                     # C++ 단위 테스트
│   └── README.md
└── tests/
    └── test_global_alignment.py   # 웹 전역 정합 테스트
```

실행 경로는 다음과 같다.

```text
브라우저
  → web/ HTTP UI·API
  → processing/ homography_tool
  → ArUco 검출·local H·전체 정합
  → server/config/homography/CAM_*/ 운영 H
  → main safety server
```

## 보정

### 목록

- 스트림 선택: `camera_id`와 `channel`을 조합한 `stream_id`
- 캡처: RTSP preview와 고해상도 snapshot을 함께 확보
- marker 검출: ID·꼭짓점·영상 크기 저장
- local H: 실제 marker 크기와 방향을 이용한 pixel→world 변환
- 전체 정합: 공통 marker 연결 그래프를 이용한 CCTV×채널 정합
- 검증: global RMSE, 공통 marker, 교차검증 결과 확인
- 운영 반영: 전체 정합 완료 시에만 최종 H를 원자적으로 저장

### 상세

1. `CCTV · CH` 스트림을 선택하고 캡처한다.
2. 검출된 ArUco 꼭짓점을 확인하고 필요한 경우 화면에서 보정한다.
3. marker 한 변의 실제 길이(mm)를 입력해 해당 채널의 local H를 산출한다.
4. 모든 참여 스트림에 대해 같은 작업을 반복한다.
5. 공통 marker가 최소 개수 이상이고 전체 스트림이 연결되어 있는지 확인한다.
6. anchor stream을 선택해 전체 정합을 실행한다.
7. global RMSE와 교차검증 결과를 확인한다.
8. 전체 정합이 성공한 경우에만 운영 H 파일이 갱신된다.

고정 격자 좌표, X/Y 기준선, 보드의 사전 좌표는 사용하지 않는다. marker ID,
검출된 네 꼭짓점, 실제 크기와 방향을 대응점으로 사용한다. 공통 marker가 한
직선에 몰리거나 연결 그래프가 끊기면 정합을 거부한다.

## 설정

### 목록

- 공통 카메라 모델: `server/config/camera_model.json`
- 공통 CCTV 목록: `server/config/camera_list.json`
- 호모그래피 정책: `server/config/homography/homography_config.json`
- 검증 화면 제한: `server/config/homography/stream_config.json`
- 최종 H: `server/config/homography/<camera_id>/`

### 상세

`camera_model.json`은 모델별 `channel_count`를, `camera_list.json`은 실제
CCTV·채널·RTSP/HTTP 연결 정보를 관리한다. 실제 `camera_list.json`에는 계정
정보가 들어갈 수 있으므로 샘플에서 복사해 운영 장비에만 작성하고 권한을
제한한다.

`homography_config.json`의 핵심 항목은 다음과 같다.

- `dictionary`: 검출할 ArUco dictionary
- `marker_output`: marker PNG/SVG 기본 크기·여백·DPI
- `manual_solve.marker_size_mm`: local H 기본 marker 크기
- `map.min_common_markers`: 전체 정합에 필요한 최소 공통 ID 수
- `outputs.manual`: 임시 local H 파일명

`stream_config.json`의 `verification.max_streams`는 실제 CCTV 수가 아니라
검증 화면의 동시 입력 슬롯 수다. 현재 허용 범위는 2~32다.

## 결과

최종 결과는 다음 형식으로 저장한다.

```text
server/config/homography/CAM_01/homography_result_cam01_ch03_mm.json
```

```json
{
  "schema_version": 2,
  "world_unit": "mm",
  "camera_id": "CAM_01",
  "stream_id": "CAM_01_CH_03",
  "channel": 3,
  "H_pixel_to_world": [[...], [...], [...]],
  "image_size": {"width": 2592, "height": 1520}
}
```

local H, marker 검출 결과, RMSE와 capture ID는 작업 디렉터리에 임시 보관한다.
최종 H만 공용 `config/homography/CAM_*`에 저장하며, 안전 서버는 이 파일을
직접 읽어 실시간 좌표를 변환한다.

## 실행

### 개발 실행

```sh
cd /home/pms/20_server_workspace
cmake -S server/apps/homography_app/processing \
  -B server/apps/homography_app/processing/build \
  -DBUILD_TESTING=ON
cmake --build server/apps/homography_app/processing/build -j2

HOMOGRAPHY_CONFIG_DIR=server/config/homography \
HOMOGRAPHY_TOOL=server/apps/homography_app/processing/build/homography_tool \
SERVER_COMMON_CONFIG_DIR=server/config \
  python3 server/apps/homography_app/web/server.py
```

- 기본 주소: `http://<server-address>:8001`
- 포트 변경: `HOMOGRAPHY_APP_PORT`
- 임시 결과 정리 주기: `ADMIN_GUI_RESULT_TTL_SEC`
- 결과 임시 루트 변경: `HOMOGRAPHY_RESULT_DIR`

### systemd 실행

```sh
sudo systemctl restart homography-app.service
sudo systemctl status homography-app.service --no-pager
```

systemd unit의 `User`, `WorkingDirectory`, `HOMOGRAPHY_TOOL`, 설정 경로는
설치 환경에 맞춰 확인한다.

## 검증

### 목록

- 전역 정합: 연결 그래프, 공통 marker, global RMSE, 교차검증
- 처리 엔진 단위 테스트: marker·local H·정합 계산
- 웹 통합 테스트: 임시 설정·capture 결과·운영 H 저장

### 명령

```sh
cd /home/pms/20_server_workspace

# 처리 엔진
ctest --test-dir server/apps/homography_app/processing/build \
  --output-on-failure

# 웹 전역 정합을 포함한 서버 CTest
cmake -S server -B server/build
cmake --build server/build -j2
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir server/build -R homography_global_alignment_test \
  --output-on-failure
```

## 현황

### 목록

- 자유 배치 ArUco marker 지원
- CCTV×채널 전체 정합 지원
- 최종 H의 `world_unit=mm` 검증
- 독립 체크포인트 기반 전역 정합 테스트
- 렌즈 왜곡 보정: 미적용

### 추가 작업

카메라 내부 파라미터와 렌즈 왜곡 계수를 별도 산출하고, marker 검출 좌표와
안전 서버로 전달할 픽셀 좌표가 같은 undistort 경로를 사용하도록 연결해야 한다.

## 문서

- [호모그래피 처리 엔진](processing/README.md)
- [호모그래피 웹 UI/API](web/README.md)
- [호모그래피 설정·결과](../../config/homography/README.md)
- [서버 전체 문서](../../README.md)
