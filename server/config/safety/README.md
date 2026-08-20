# 중앙 안전 서버 설정

서버는 다음 명령으로 실행한다.

```text
forklift_safety_server [--config-dir PATH] [--common-config-dir PATH]
```

`PATH`를 생략하면 `server/config/safety`를 찾는다. 공통 카메라·단말 식별 설정은
`server/config`에서 읽으며, 다른 위치에 둘 때는 `--common-config-dir PATH`를 함께 지정한다.
운영 설정은 장비마다 달라지므로 Git에는 샘플만 남기고 실제 값은 별도 파일로 둔다.

## 파일 역할

- `server/config/camera_model.json`: 카메라 모델별 채널 수
- `server/config/camera_list.json`: CCTV 목록, RTSP 주소, 채널별 H 파일과 해상도
- `server/config/forklift_device_config.json`: TERM, ArUco marker, 지게차 충돌 반경
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

현재 지게차 실측 크기 `135mm × 475mm`를 방향 정보 없이 보수적으로 감싸기 위해
충돌 반경은 직사각형 외접원의 반지름 `sqrt(135² + 475²) / 2 = 246.9069mm`로
설정한다. 지게차 heading을 받을 수 있게 되면 직사각형 판정으로 교체한다.

2700mm × 1200mm 작업 공간을 채널 2·3이 나눠 보는 초기 운영값은
`CAUTION 500mm / DANGER 300mm / EMERGENCY 100mm`이며, ToF도
`500mm / 300mm`를 사용한다. 이 값은 실측 전 초기값이므로 실제 지게차 정지거리,
ToF 오차, 카메라 좌표 오차를 측정한 뒤 최종 확정해야 한다.

호모그래피 산출물은 물리 CCTV별 폴더에 보관한다.

```text
homography/CAM_01/homography_result_cam01_ch03_mm.json
homography/CAM_02/homography_result_cam02_ch01_mm.json
```

`camera_list.json`에서는 위 상대경로를 채널별로 참조하고, 서버가 이를
`CAM_01_CH_03`, `CAM_02_CH_01` 같은 `stream_id`로 자동 연결한다.

실행 중 생성되는 DB·CSV는 설정 폴더와 분리된 `server/var/main_app/storage`에 저장한다.

## MQTT

- 센서: `forklift/sensor/TERM_N`
- 위험 결과: `forklift/risk/TERM_N`
- 카메라 전환: `forklift/assignment/TERM_N` (QoS 1, retain)
- 서버 상태: `forklift/status/server`

센서 토픽의 `TERM_N`은 `server/config/forklift_device_config.json`에 등록된
`terminal_id`여야 한다.
서버는 설정에 등록된 K개 단말에 대해서만 pipeline·센서 reader·위험 결과 publisher를
생성하며, 설정에 없는 terminal_id의 센서 메시지는 캐시에 저장하지 않고 거부한다.

assignment는 `type`, `terminal_id`, `stream_id`, `camera_id`, `channel`, `utc_time`을
포함한다. TCP 9001 카메라 할당 서버는 사용하지 않는다.

운영 MQTT는 mTLS listener `8883`을 사용한다. `system_config.json`의
`tls_enabled`를 켜고 CA·중앙 서버용 client 인증서·개인키 경로를 설정해야 한다.
개인키는 Git에 넣지 말고 `/etc/forklift_safety/certs/`에 설치한다.

```text
sudo ./server/scripts/install-server.sh
```

설치 스크립트는 `main_app`, 호모그래피 엔진, 호모그래피 앱, 모니터링 앱을 빌드·설치하고
현재 저장소 경로를 사용하는 systemd 유닛을 등록한다. 운영 기본값에서는 모니터링 앱만
자동 실행하며, 호모그래피 앱은 설치만 하고 중지·비활성화한다. 필요할 때
`sudo systemctl start homography-app.service`로 수동 실행한다. Mosquitto의 평문 `1883` listener는
지게차 단말과 관제 PC의 `8883` 전환을 확인한 뒤 별도로 닫는다.
