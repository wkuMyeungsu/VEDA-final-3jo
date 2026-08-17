# 공통 카메라 설정

`server/config`는 안전 서버와 호모그래피 앱이 함께 읽는 공용 카메라 영역이다.
앱별 정책은 각각 `safety/`, `homography/`로 분리하고, 실행 중 생성되는 H는
`operational/homography/`에 둔다.

## 파일 역할

- `camera_model.json`: 실제로 존재하는 CCTV 모델과 모델별 채널 수만 정의한다.
  카메라 ID나 설치 주소는 넣지 않는다.
- `camera_list.json`: 설치된 CCTV 목록, `camera_id ↔ model` 매핑, 연결 정보,
  채널별 RTSP 주소와 공통 H 파일 경로를 기록한다.
- `operational/homography/<camera_id>/...`: 호모그래피 앱이 산출하고 main이 그대로 읽는
  최종 `H_pixel_to_world` 파일이다.

## 경로 기준

`camera_list.json`의 `homography_file`은 이 폴더를 기준으로 한 상대경로다.
따라서 `operational/homography/CAM_01/homography_channel_3_mm.json`은 다음 파일을 뜻한다.

```text
server/config/operational/homography/CAM_01/homography_channel_3_mm.json
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
작성하고 권한을 `600`으로 유지한다. Git에는 `camera_list.sample.json`만 남긴다.
