# ArUCoCalibration

Wisenet 카메라 앱. 채널별(최대 4개) ChArUco 보드 검출로 카메라 intrinsics(camera matrix, 왜곡 계수) 계산.

## 환경

| 항목 | 값 |
|---|---|
| SDK_VER | `26.05.19` |
| SOC | `cv5` |

## 통신 방식

- 프로토콜: HTTP REST, Body: JSON
- 라우팅: AppDispatcher → FastCGI → `PATH_INFO` 매칭 (`sample_component.cc`)
- Base URL: `http://<카메라 IP>/opensdk/<app_id>/<경로>`

## API

| 경로 | 메서드 | 상태 | 설명 |
|---|---|---|---|
| `/board` | POST | 구현됨 | 보드 사양 설정 (`squares_x`, `squares_y`, `square_length_mm`, `marker_length_mm`) |
| `/detect` | GET | 구현됨 | `?channel=1~4` 검출 미리보기, 저장 안 함 |
| `/capture` | POST | 구현됨 | 캡처 세션에 누적 저장 |
| `/status` | GET | 구현됨 | 채널별 누적 상태 조회 |
| `/discard` | POST | 구현됨 | 캡처 프레임 삭제 (`channel`, `index`) |
| `/reset` | POST | 구현됨 | 채널 세션 초기화 (`channel`) |
| `/calibrate` | POST | 예정 | 캘리브레이션 계산 |
| `/result` | GET | 예정 | 결과 조회 (재시작 후 유지) |
| `/undistort` | GET | 예정 | 왜곡 보정 프리뷰 |

## 사용법

`<CAM_IP>`, `<앱경로>`는 실제 값으로 대체.

**1. 보드 등록**
```bash
curl -X POST http://<CAM_IP>/<앱경로>/board \
  -d '{"squares_x": 7, "squares_y": 5, "square_length_mm": 30, "marker_length_mm": 22}'
```
- 채널별이 아니라 앱 전체에 공통으로 적용됨 (채널마다 따로 등록하는 게 아님)
- 메모리에만 유지됨 — 디스크 저장 안 함, 앱 재시작(리부팅/재배포) 시 초기화되어 재등록 필요
- 재등록(사양 변경) 시 4개 채널 전부의 누적 캡처가 함께 초기화됨 (이전 좌표계로 찍은 캡처는 새 보드와 안 맞으므로)

**2. 검출 미리보기** — 캡처 전 각도/거리 확인용, 저장 안 됨.
```bash
curl "http://<CAM_IP>/<앱경로>/detect?channel=1"
```
`charuco_corner_count`가 4 미만이면 `/capture`에서 거부됨.

**3. 캡처 누적** — 각도/거리 바꿔가며 반복 호출, 1회 = 1장.
```bash
curl -X POST http://<CAM_IP>/<앱경로>/capture -d '{"channel": 1}'
```
- ChArUco 코너 4개 미만 → `accepted: false` (재시도 가능, 에러 아님)
- 첫 캡처 이후 해상도/줌 변경 시 이후 캡처 전부 거부
- 권장: 10장 이상, 다양한 각도

**4. 진행 상황** — `total_captured`, `has_result` 확인. `total_captured ≥ 10`이면 `/calibrate` 가능.
```bash
curl "http://<CAM_IP>/<앱경로>/status?channel=1"
```

**5. 캡처 삭제/초기화**
```bash
curl -X POST http://<CAM_IP>/<앱경로>/discard -d '{"channel": 1, "index": 0}'   # index번째 캡처 삭제
curl -X POST http://<CAM_IP>/<앱경로>/reset -d '{"channel": 1}'                 # 채널 캡처 전체 초기화
```

**6. `/calibrate`, `/result`, `/undistort`** — 미구현, API 표 참고.

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
