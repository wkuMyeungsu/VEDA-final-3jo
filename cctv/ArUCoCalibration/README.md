# ArUCoCalibration

- Wisenet 카메라에 올라가는 앱.
- 카메라가 자기 채널 영상에서 ChArUco 보드를 검출해 카메라 intrinsics(camera matrix, 왜곡 계수)를 계산한다.
- 채널 최대 4개까지 독립적으로 캘리브레이션 가능.

흐름: 보드 사양 등록(`/board`) → 검출 확인(`/detect`) → 여러 각도로 캡처 누적(`/capture`) → 캘리브레이션 계산(`/calibrate`) → 결과 조회/왜곡 보정 프리뷰(`/result`, `/undistort`).

## 환경

| 항목 | 값 |
|---|---|
| SDK_VER | `26.05.19` |
| SOC | `cv5` |
| 딕셔너리 | `cv::aruco::DICT_4X4_50` |
| 보드 | ChArUco (`cv::aruco::CharucoBoard`) |

## API

| 경로 | 메서드 | 상태 | 설명 |
|---|---|---|---|
| `/board` | POST | 구현됨 | 보드 사양 설정 (`squares_x`, `squares_y`, `square_length_mm`, `marker_length_mm`) |
| `/detect` | GET | 구현됨 | `?channel=1~4` 검출만 확인, 저장 안 함 |
| `/capture` | POST | 예정 | 캡처 세션에 누적 저장 |
| `/status` | GET | 예정 | 채널별 누적 상태 조회 |
| `/discard` | POST | 예정 | 캡처 프레임 삭제 |
| `/reset` | POST | 예정 | 채널 세션 초기화 |
| `/calibrate` | POST | 예정 | 캘리브레이션 계산 |
| `/result` | GET | 예정 | 결과 조회 (재시작 후 유지) |
| `/undistort` | GET | 예정 | 왜곡 보정 프리뷰 |

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
