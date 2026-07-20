# ArUCoCalibration

- Wisenet 카메라에 올라가는 앱.
- 카메라가 자기 채널 영상에서 ChArUco 보드를 검출해 카메라 intrinsics(camera matrix, 왜곡 계수)를 계산한다.
- 채널 최대 4개까지 독립적으로 캘리브레이션 가능.

## 환경

| 항목 | 값 |
|---|---|
| SDK_VER | `26.05.19` |
| SOC | `cv5` |

## API

| 경로 | 메서드 | 상태 | 설명 |
|---|---|---|---|
| `/board` | POST | 구현됨 | 보드 사양 설정 (`squares_x`, `squares_y`, `square_length_mm`, `marker_length_mm`) |
| `/detect` | GET | 구현됨 | `?channel=1~4` 검출만 확인, 저장 안 함 |
| `/capture` | POST | 구현됨 | 캡처 세션에 누적 저장 |
| `/status` | GET | 구현됨 | 채널별 누적 상태 조회 |
| `/discard` | POST | 예정 | 캡처 프레임 삭제 |
| `/reset` | POST | 예정 | 채널 세션 초기화 |
| `/calibrate` | POST | 예정 | 캘리브레이션 계산 |
| `/result` | GET | 예정 | 결과 조회 (재시작 후 유지) |
| `/undistort` | GET | 예정 | 왜곡 보정 프리뷰 |

## 사용법

카메라 IP를 `<CAM_IP>`로 표기. 모든 요청은 앱 URI(`/board` 등)로 간다.

### 1. 보드 사양 등록 — `/board`

캘리브레이션에 쓸 실물 ChArUco 보드의 규격을 먼저 알려줘야 한다. 보드를 바꾸면(사양이 바뀌면) 그 채널에 쌓아둔 캡처는 전부 초기화된다.

```bash
curl -X POST http://<CAM_IP>/<앱경로>/board \
  -d '{"squares_x": 7, "squares_y": 5, "square_length_mm": 30, "marker_length_mm": 22}'
```

- `squares_x`, `squares_y`: 체스보드 칸 수 (가로, 세로)
- `square_length_mm`: 정사각형 한 변 길이 (mm)
- `marker_length_mm`: 그 안에 들어가는 ArUco 마커 한 변 길이 (mm, 보통 정사각형보다 작음)

응답:
```json
{"ok": true, "squares_x": 7, "squares_y": 5, "square_length_mm": 30, "marker_length_mm": 22}
```

### 2. 검출 미리 확인 — `/detect`

보드를 카메라 화면에 비춘 상태에서, 저장 없이 지금 검출이 잘 되는지만 확인. 캡처하기 전에 위치/각도 잡는 용도.

```bash
curl "http://<CAM_IP>/<앱경로>/detect?channel=1"
```

`channel`은 1~4. 응답에 검출된 마커 좌표, ChArUco 코너 개수(`charuco_corner_count`)가 담긴다. 이 값이 너무 작으면(4개 미만) 캡처가 거부되니, 여기서 미리 각도/거리를 조정한다.

### 3. 캡처 누적 — `/capture`

같은 보드를 여러 각도/거리로 옮겨가며 반복 호출. 한 번 호출 = 한 장 캡처.

```bash
curl -X POST http://<CAM_IP>/<앱경로>/capture -d '{"channel": 1}'
```

- ChArUco 코너 4개 미만이면 `accepted: false`로 거부 (실패해도 에러 아님, 응답만 확인하고 각도 바꿔 재시도)
- 첫 캡처 이후 해상도/줌이 바뀌면 이후 캡처는 전부 거부됨 — 캡처 도중 줌 조작 금지
- 최소 10장 이상, 다양한 각도(정면/기울임/화면 구석구석)로 모으는 걸 권장

응답의 `total_captured`로 몇 장 쌓였는지 확인 가능.

### 4. 진행 상황 확인 — `/status`

```bash
curl "http://<CAM_IP>/<앱경로>/status?channel=1"
```

채널별로 지금까지 몇 장 캡처됐는지(`total_captured`), 캘리브레이션 결과가 있는지(`has_result`)를 반환. `total_captured`가 `min_recommended`(10) 이상이면 `/calibrate` 호출 가능.

### 5. 캘리브레이션 계산 이후 (준비 중)

`/calibrate`, `/result`, `/undistort`, `/discard`, `/reset`은 아직 구현 전. API 표 참고.

## 로컬 설정

```bash
cp config.example.json config.local.json   # admin_pass 채우기, git엔 안 올라감
```

## 빌드

```bash
cp -r <SDK_경로>/opencv2 app/includes/     # 최초 1회
cp <SDK_경로>/*.a app/libs/                # 최초 1회
APP_NAME=ArUCoCalibration SDK_VER=26.05.19 SOC=cv5 docker compose up
```
