# ArUCoCalibration

Wisenet 카메라 앱. 채널별(최대 4개) ChArUco 보드 검출로 카메라 intrinsics(camera matrix, 왜곡 계수) 계산.

## 환경

| 항목 | 값 |
|---|---|
| SDK_VER | `26.05.19` |
| SOC | `cv5` |

## 통신 방식

- 프로토콜: HTTP REST, Body: JSON
- 라우팅: AppDispatcher → FastCGI → `PATH_INFO` (`sample_component.cc`)
- Base URL: `http://<카메라 IP>/opensdk/<app_id>/<경로>`
- 채널 지정 파라미터명은 `channel`이 아니라 `ch` (플랫폼이 `channel`이라는 이름을 자체적으로 가로채 검증하는 것으로 추정되어 회피)

## API

| 경로 | 메서드 | 설명 |
|---|---|---|
| `/board` | POST | 보드 사양 설정 (`squares_x`, `squares_y`, `square_length_mm`, `marker_length_mm`). 전 채널 공통, 메모리 전용, 재등록 시 전 채널 캡처 초기화 |
| `/board` | GET | 등록된 보드 사양 조회 |
| `/detect` | GET | `?ch=1~4` 검출 미리보기 (저장 안 함) |
| `/capture` | POST | `{ch}` 캡처 누적 (코너 4개 미만 거부, 세션 중 해상도 변경 시 거부) |
| `/status` | GET | `?ch=1~4` 누적 캡처 수 / 결과 유무 조회 |
| `/discard` | POST | `{ch, index}` 캡처 1건 삭제 |
| `/reset` | POST | `{ch}` 채널 캡처 전체 초기화 |
| `/calibrate` | POST | `{ch, rational_model?}` 계산 + `calib_result_ch<N>.json` 저장 (재계산 시 덮어씀) |
| `/result` | GET | `?ch=1~4` 저장된 결과 조회 (재시작 후 유지) |
| `/undistort` | GET | `?ch=1~4` 왜곡 보정 프리뷰 (JPEG), `/calibrate` 결과 필요 |
| `/captures/image` | GET | `?ch=1~4&index=N` 캡처 당시 축소 썸네일(JPEG) |
| `/preview/image` | GET | `?ch=1~4` 가장 최근 `/detect` 결과 이미지(JPEG), 저장 아님 |

## 사용법

```bash
# 1. 보드 등록 (1회)
curl -X POST http://<CAM_IP>/<앱경로>/board \
  -d '{"squares_x": 7, "squares_y": 5, "square_length_mm": 30, "marker_length_mm": 22}'

# 2~3. 위치 잡고(detect) 캡처(capture) 반복 — 각도 다양하게, 10장 이상 권장
curl "http://<CAM_IP>/<앱경로>/detect?ch=1"
curl -X POST http://<CAM_IP>/<앱경로>/capture -d '{"ch": 1}'

# 4. 계산
curl -X POST http://<CAM_IP>/<앱경로>/calibrate -d '{"ch": 1}'

# 5. 조회
curl "http://<CAM_IP>/<앱경로>/result?ch=1"
```

캡처 좋은 기준: 코너 4개 이상 검출, 각도/위치 다양, 세션 내 해상도 고정.

**앱 간 연동**: 다른 앱(예: ArUCo_Detection)은 파일을 직접 읽지 말고 `http://127.0.0.1/opensdk/<app_id>/result?ch=N`으로 HTTP 호출해서 가져갈 것.

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
