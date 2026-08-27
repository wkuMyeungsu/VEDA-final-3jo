# 설정 템플릿

`server/config.example`는 안전 서버와 호모그래피 앱이 함께 읽는 설정의 완전한 템플릿이다.
처음 설치할 때 `server/config`로 복사하고, 실제 장비 주소·비밀번호·정책은 복사본에서 수정한다.
앱별 정책과 호모그래피 결과는 `safety/`, `homography/`로 분리한다.
호모그래피 결과는 같은 `homography/` 루트 아래 카메라별 디렉터리에 둔다.

```sh
cp -a server/config.example/. server/config/
```

## 파일 역할

- `camera_model.json`: 실제로 존재하는 CCTV 모델과 모델별 채널 수만 정의한다.
  카메라 ID나 설치 주소는 넣지 않는다.
- `camera_list.json`: 설치된 CCTV 목록, `camera_id ↔ model` 매핑, 연결 정보,
  채널별 RTSP 주소와 공통 H 파일 경로를 기록한다.
- `forklift_device_config.json`: 등록된 단말의 `terminal_id`, ArUco marker,
  지게차 충돌 반경, 바닥에서 마커 중심까지의 높이(`marker_height_mm`, 생략 시 0)를 기록한다.
- `site_map.json`: 전체 작업장 외곽과 제외·가림 구역을 공통 월드 좌표(mm)로 기록한다.
- `homography/<camera_id>/...`: 호모그래피 앱이 산출하고 main이 그대로 읽는
  최종 `H_camera_pixels_to_shared_map` 파일이다.

## 전체 맵

호모그래피는 카메라 픽셀을 공통 월드 좌표로 바꾸는 역할만 한다. 영상의 네 모서리를
공장 외곽이나 실제 가시영역으로 간주하지 않는다. `site_map.json`의 `boundary`에 실제
작업장 외곽을, `zones`에 다음 구역을 같은 mm 좌표계로 기록한다.

- `excluded`: 벽·설비·통로 밖처럼 운영 지도에서 제외할 구역
- `blind`: 장애물 때문에 CCTV에서 가려지는 구역

좌표는 `[x_mm, y_mm]`이며 각 폴리곤은 점이 3개 이상이어야 한다. 파일을 생략하면 안전
서버는 기존처럼 기동하고 모니터링의 전체 맵만 설정 대기 상태로 표시된다.

## 경로 기준

`camera_list.json`의 `homography_file`은 설정 루트를 기준으로 한 상대경로다.
따라서 `homography/CAM_01/homography_result_cam01_ch03_mm.json`은 다음 파일을 뜻한다.

```text
server/config/homography/CAM_01/homography_result_cam01_ch03_mm.json
```

main을 다른 위치에서 실행할 때는 다음처럼 공통 설정 폴더를 명시할 수 있다.

```sh
forklift_safety_server \
  --config-dir /path/to/server/config/safety \
  --common-config-dir /path/to/server/config
```

호모그래피 앱은 `SERVER_COMMON_CONFIG_DIR` 환경변수로 같은 폴더를 지정한다.

## 운영 파일 보안

`camera_list.json`에는 CCTV 계정 비밀번호가 들어갈 수 있으므로 운영 장비에서만
작성하고 권한을 `600`으로 유지한다. Git에는 이 템플릿만 남기고 실제 `server/config`는
장비별 운영 설정으로 관리한다.
