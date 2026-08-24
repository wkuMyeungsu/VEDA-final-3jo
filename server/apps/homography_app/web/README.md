# 호모그래피 웹 앱

등록된 CCTV를 선택하고 각 채널의 캡처·ArUco 검출·호모그래피 산출·전체 정합을
브라우저에서 수행하는 LAN용 HTTP UI/API다. 실제 계산은 C++
`processing/build/homography_tool`에 위임한다.

## 한눈에 보기

- 기본 주소: `http://<server-address>:8001`
- 바인드 주소: `ADMIN_GUI_HOST`, 기본 `0.0.0.0`
- 포트: `HOMOGRAPHY_APP_PORT`, 기본 `8001`
- 공통 설정: `SERVER_COMMON_CONFIG_DIR`, 기본 `server/config`
- 호모그래피 설정: `HOMOGRAPHY_CONFIG_DIR`, 기본 `server/config/homography`
- 처리 도구: `HOMOGRAPHY_TOOL`, 기본 `processing/build/homography_tool`
- 임시 결과: `HOMOGRAPHY_RESULT_DIR`, 기본 `/tmp/homography-results`
- 최종 H 저장: `SAFETY_SERVER_HOMOGRAPHY_DIR`, 기본 `server/config/homography`
- 실행 방식: Python 표준 라이브러리 HTTP server + subprocess
- 인증: 애플리케이션 인증 없음, 신뢰된 LAN 또는 별도 reverse proxy 범위에서 사용

## 구조

### 목록

- 공통 설정 로더: `camera_model.json`, `camera_list.json`
- CCTV 입력: RTSP preview·고해상도 HTTP snapshot
- 처리 위임: `homography_tool gen-marker`, `detect-markers`, `solve-manual`,
  `align-markers`
- 작업 결과: capture ID별 임시 디렉터리
- 운영 결과: 전체 정합 성공 시 카메라별 H JSON 원자적 저장
- 정적 UI: `static/index.html`, `static/homography.js`, `static/homography.css`

### 상세

```text
브라우저
  → web/server.py
  → camera_list.json의 CCTV·채널 검증
  → RTSP/HTTP 캡처
  → processing/build/homography_tool
  → /tmp/homography-results/<capture_id>
  → 전체 정합 성공 시 config/homography/CAM_*/ 운영 H
```

웹 앱은 `camera_id`와 `channel`을 `stream_id`로 평탄화한다.

```text
CAM_01 + channel 1 → CAM_01_CH_01
CAM_01 + channel 3 → CAM_01_CH_03
CAM_02 + channel 1 → CAM_02_CH_01
```

요청은 `stream_id` 사용을 우선한다. 일부 camera API는 기존 호출과의 호환을 위해
`channel`만 받은 요청도 허용하지만, CCTV가 여러 대인 환경에서는 `stream_id`를
사용해야 채널이 섞이지 않는다.

## 작업

### 목록

- 스트림 선택
- preview·고해상도 캡처
- ArUco marker 검출
- marker 꼭짓점 보정
- 채널별 카메라 화면 펴기
- 전체 CCTV×채널 겹침 구간 연결
- 겹침 맞춤 오차·겹침 마커 하나 제외 확인
- verification region 저장

### 상세

1. 화면에서 CCTV와 채널을 선택한다.
2. `선택 스트림 캡처`로 RTSP preview와 고해상도 이미지를 만든다.
3. 검출된 marker ID와 네 꼭짓점을 확인한다.
4. 오검출 코너가 있으면 화면에서 보정한다.
5. marker 한 변의 실제 길이(mm)를 입력하고 카메라 화면을 편다.
6. 참여하는 모든 스트림의 카메라 화면 펴기 결과를 준비한다.
7. anchor stream과 capture ID 목록을 선택해 겹침 구간 연결을 실행한다.
8. 마커 모양 오차, 겹침 맞춤 오차, 겹침 마커 하나 제외 확인을 확인한다.
9. 겹침 구간 연결이 성공하면 `config/homography/<camera_id>/`에 최종 H가 저장된다.

카메라 화면 펴기 단계에서는 운영 H를 갱신하지 않는다. 운영 파일은
`/api/homography/global-align` 성공 시에만 저장된다.

## API

### 목록

| Method | 경로 | 역할 |
| --- | --- | --- |
| GET | `/` | 호모그래피 UI |
| GET | `/api/status` | 카메라·스트림·정합 정책 상태 |
| GET | `/api/camera/frame` | 최신 JPEG frame |
| GET | `/api/camera/video` | MJPEG preview, `overlay=1` 지원 |
| GET | `/api/camera/detections` | 최신 검출 결과 |
| GET | `/artifacts/<capture_id>/<name>` | 임시 결과 파일 |
| POST | `/api/camera/settings` | 현재 카메라·모델 선택 저장 |
| POST | `/api/camera/detect` | 캡처와 marker 검출 |
| POST | `/api/homography/solve` | 캡처 기준 local H 산출 |
| POST | `/api/homography/global-align` | 전체 스트림 정합·운영 H 저장 |
| POST | `/api/homography/region` | 검증 영역 저장 |
| POST | `/api/homography/solve-manual` | 정적 이미지 기반 수동 산출 |

### 주요 요청

camera API와 전체 정합 요청은 `stream_id`를 사용한다.

```json
{
  "stream_id": "CAM_01_CH_03"
}
```

전체 정합은 최소 두 stream과 anchor stream, 각 stream의 capture ID가 필요하다.

```json
{
  "stream_ids": ["CAM_01_CH_02", "CAM_01_CH_03"],
  "anchor_stream_id": "CAM_01_CH_02",
  "capture_ids": {
    "CAM_01_CH_02": "capture-id-02",
    "CAM_01_CH_03": "capture-id-03"
  }
}
```

`/api/status`는 안전 서버 상태를 수집하는 API가 아니다. 현재 웹 앱 자체의
카메라 목록, 지원 채널 수, stream 목록, 정합 정책과 도구 경로를 반환하며
`server_monitoring` 값은 placeholder다.

## 설정

### 목록

- `server/config/camera_model.json`: 모델별 채널 수
- `server/config/camera_list.json`: CCTV ID·모델·RTSP/HTTP 연결·채널
- `server/config/homography/homography_config.json`: marker·정합 정책
- `server/config/homography/stream_config.json`: 검증 화면 stream 슬롯
- `server/config/homography/CAM_*/`: 최종 운영 H

### 상세

공통 카메라 설정은 안전 서버와 호모그래피 앱이 함께 읽는다. 실제 CCTV 계정이
포함될 수 있는 `camera_list.json`은 샘플에서 생성하고 운영 장비에만 둔다.

```sh
cd /home/pms/20_server_workspace
cp server/config/camera_list.sample.json server/config/camera_list.json
chmod 600 server/config/camera_list.json
```

`camera_model.json`의 `channel_count`와 `camera_list.json`의 실제 채널 목록이
일치해야 한다. 지원 가능한 모델·채널은 앱 기동 시 검증한다.

## 결과

### 목록

- 작업 결과: `/tmp/homography-results/<capture_id>/`
- marker JSON: `markers.json`
- 캡처 이미지: `capture.jpg`, `rtsp-capture.jpg`
- local H: `homography_manual.json` 또는 `outputs.manual` 지정 이름
- 최종 H: `server/config/homography/<camera_id>/`
- 작업 결과 보관: `ADMIN_GUI_RESULT_TTL_SEC`, 기본 3600초

### 상세

최종 운영 H는 다음 필드를 포함한다.

```json
{
  "schema_version": 2,
  "map_unit": "mm",
  "camera_id": "CAM_01",
  "stream_id": "CAM_01_CH_03",
  "channel": 3,
  "H_camera_pixels_to_shared_map": [[...], [...], [...]],
  "image_size": {"width": 2592, "height": 1520}
}
```

작업 결과는 임시 파일이며, TTL 정리 대상이다. 최종 H에 포함되지 않는 마커 모양 오차,
사용 marker, capture ID 등의 상세 결과도 작업 디렉터리와 API 응답에서 확인한다.

## 실행

### 개발 실행

```sh
cd /home/pms/20_server_workspace
cmake -S server/apps/homography_app/processing \
  -B server/apps/homography_app/processing/build \
  -DBUILD_TESTING=ON
cmake --build server/apps/homography_app/processing/build -j2

ADMIN_GUI_HOST=0.0.0.0 \
HOMOGRAPHY_CONFIG_DIR=server/config/homography \
HOMOGRAPHY_TOOL=server/apps/homography_app/processing/build/homography_tool \
SERVER_COMMON_CONFIG_DIR=server/config \
  python3 server/apps/homography_app/web/server.py
```

브라우저에서 다음 주소를 연다.

```text
http://127.0.0.1:8001
http://<server-address>:8001
```

### systemd 실행

```sh
sudo systemctl start homography-app.service
sudo systemctl status homography-app.service --no-pager
sudo systemctl stop homography-app.service
```

unit 파일은 `server/deploy/systemd/homography-app.service`에 있다. 이 앱은
온디맨드 유지보수 도구이므로 설치 시 비활성화되며 자동 재시작하지 않는다.
필요할 때 `start`하고 보정 작업이 끝나면 `stop`한다.

## 검증

### 목록

- C++ 처리 엔진 단위 테스트
- 웹 전역 정합 테스트
- 연결 그래프 단절·공통 marker 부족 검증
- 겹침 맞춤 오차·겹침 마커 하나 제외 확인 검증

### 명령

```sh
cd /home/pms/20_server_workspace

# 처리 엔진 빌드·테스트
cmake -S server/apps/homography_app/processing \
  -B server/apps/homography_app/processing/build \
  -DBUILD_TESTING=ON
cmake --build server/apps/homography_app/processing/build -j2
ctest --test-dir server/apps/homography_app/processing/build \
  --output-on-failure

# 웹 전역 정합 테스트
cmake -S server -B server/build
cmake --build server/build -j2
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir server/build -R homography_global_alignment_test \
  --output-on-failure
```

## 운영

### 목록

- LAN 접근 범위 제한
- `camera_list.json` 권한 제한
- 임시 결과 디스크 사용량 확인
- 운영 H 저장 권한 확인
- systemd unit 경로 확인

### 상세

인증 계층이 없는 내부 도구이므로 `ADMIN_GUI_HOST=0.0.0.0`로 열 때는 방화벽,
관리망, reverse proxy 중 하나로 접근 범위를 제한한다. 카메라 비밀번호가 포함된
`camera_list.json`은 Git에 넣지 않고 `chmod 600`을 유지한다.

## 문서

- [호모그래피 앱 전체 개요](../README.md)
- [호모그래피 처리 엔진](../processing/README.md)
- [호모그래피 설정·결과](../../../config/homography/README.md)
- [서버 전체 문서](../../../README.md)
