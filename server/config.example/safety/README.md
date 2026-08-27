# 중앙 안전 서버 설정

서버는 다음 명령으로 실행한다.

```text
forklift_safety_server [--config-dir PATH] [--common-config-dir PATH] [--no-sensor]
```

`PATH`를 생략하면 `server/config/safety`를 찾는다. 공통 카메라·단말 식별 설정은
`server/config`에서 읽으며, 다른 위치에 둘 때는 `--common-config-dir PATH`를 함께 지정한다.
이 파일은 템플릿이며, 실제 운영 파일은 `server/config/safety`에 둔다.

센서가 아직 연결되지 않은 카메라 판정 테스트에서는 `--no-sensor`를 추가한다. 이때는
센서 입력을 읽거나 위험 판정에 반영하지 않고 카메라·추적·호모그래피 경로만 검사한다.
옵션을 생략하면 기본값인 실제 네트워크 센서 수신·퓨전 모드로 동작한다.

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
- 유효한 H를 가진 활성 CCTV 스트림이 0개
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

실행 중 생성되는 DB·CSV·runtime text log는 설정 폴더와 분리된
`server/var/main_app/storage`에 저장한다. `output_storage.server_log`를 지정하면
`event_db`와 별도 runtime 경로를 쓸 수 있고, 키를 생략한 기존 설정은 기존
`event_db` 상위의 `server.log`로 fallback한다.
`output_storage.runtime_status`는 모니터링이 읽는 JSON snapshot 경로이며, 생략 시
`runtime/runtime-status.json`을 사용한다. snapshot은 임시 파일을 쓴 뒤 rename하여
모니터링이 반쪽짜리 JSON을 읽지 않게 한다.
메타데이터 처리 큐는 `stream.metadata_queue_capacity`(기본 256)로 제한하며,
초과 시 오래된 프레임부터 버려 최신 판정의 지연과 메모리 증가를 제한한다.

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
포함하고, 새 서버는 `assignment_id`, `revision`, `server_run_id`를 optional로 추가한다.
센서 payload도 기존 필드 외에 `schema_version`, `message_id`, `producer_run_id`,
`sequence`를 optional로 보낼 수 있다. TCP 9001 카메라 할당 서버는 사용하지 않는다.

운영 MQTT는 mTLS listener `8883`을 사용한다. `system_config.json`의
`tls_enabled`를 켜고 CA·중앙 서버용 client 인증서·개인키 경로를 설정해야 한다.
개인키는 Git에 넣지 말고 `/etc/forklift_safety/certs/`에 설치한다.

이 저장소는 배포 패키지나 OS 설치 절차를 제공하지 않는다. 빌드는 CMake로 수행하고,
운영 환경에 배치된 서비스는 `forklift-bscpctl`과 systemd로 관리한다. 운영 기본값에서는
모니터링 앱만 자동 실행하며, 호모그래피·캘리브레이션 앱은 필요할 때만 실행한다.
필요할 때 `sudo systemctl start homography-app.service` 또는
`sudo systemctl start calibration-app.service`로 수동 실행한다. Mosquitto의 평문 `1883` listener는
지게차 단말과 관제 PC의 `8883` 전환을 확인한 뒤 별도로 닫는다.
