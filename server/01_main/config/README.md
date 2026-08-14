# 중앙 안전 서버 설정

서버는 다음 명령으로 실행한다.

```text
forklift_safety_server [--config-dir PATH] [--common-config-dir PATH]
```

`PATH`를 생략하면 `server/01_main/config`를 찾는다. 공통 카메라 설정은
`server/config`에서 읽으며, 다른 위치에 둘 때는 `--common-config-dir PATH`를 함께 지정한다.
운영 설정은 장비마다 달라지므로 Git에는 샘플만 남기고 실제 값은 별도 파일로 둔다.

## 파일 역할

- `server/config/camera_model.json`: 카메라 모델별 채널 수
- `server/config/camera_list.json`: CCTV 목록, RTSP 주소, 채널별 H 파일과 해상도
- `forklift_device_config.json`: TERM, ArUco marker, 지게차 충돌 반경
- `danger_judgment_config.json`: 모든 TERM이 공유하는 위험 임계값과 단위
- `system_config.json`: MQTT, 추적, 핸드오버, 센서, 스트림, 저장 경로

예를 들어 `PNO-A9081RG`는 `channel_count: 1`, `PNM-C16083RVQ`는
`channel_count: 4`로 `camera_model.json`에 정의한다. `camera_list.json`은 모델명을
참조하고, 그 모델의 채널 번호 1부터 `channel_count`까지 모두 작성해야 한다.

## 식별자 규칙

`camera_id`는 물리 CCTV 장비 이름이다. `channel`은 그 장비 내부 채널 번호다.
서버는 둘을 합쳐 전역 스트림 이름을 자동으로 만든다.

```text
CAM_01 + channel 1  -> CAM_01_CH_01
CAM_02 + channel 1  -> CAM_02_CH_01
```

따라서 서로 다른 CCTV의 channel 1은 충돌하지 않는다. 위험 결과와 CSV에는
`stream_id`, `camera_id`, `channel`을 함께 기록한다.

## 기동 검증

설정 하나라도 틀리면 서버는 기동하지 않는다.

- 모델·카메라·TERM·marker·stream_id 중복
- 모델이 정한 채널 수와 실제 채널 목록 불일치
- RTSP 주소, H 파일, 인증서 파일 누락
- H의 단위가 mm가 아니거나 3×3 행렬·해상도가 잘못됨
- 거리 임계값 순서, 포트, 추적·스트림 정책 범위 오류

H 파일의 상대 경로와 저장 경로는 설정 디렉터리를 기준으로 해석한다. 모든 월드 좌표와
거리는 mm이며 실행 중 단위 변환을 하지 않는다.

호모그래피 산출물은 물리 CCTV별 폴더에 보관한다.

```text
homography/CAM_01/homography_channel_3_mm.json
homography/CAM_02/homography_channel_1_mm.json
```

`camera_list.json`에서는 위 상대경로를 채널별로 참조하고, 서버가 이를
`CAM_01_CH_03`, `CAM_02_CH_01` 같은 `stream_id`로 자동 연결한다.

## MQTT

- 센서: `forklift/sensor/TERM_N`
- 위험 결과: `forklift/risk/TERM_N`
- 카메라 전환: `forklift/assignment/TERM_N` (QoS 1, retain)
- 서버 상태: `forklift/status/server`

assignment는 `type`, `terminal_id`, `stream_id`, `camera_id`, `channel`, `utc_time`을
포함한다. TCP 9001 카메라 할당 서버는 사용하지 않는다.
