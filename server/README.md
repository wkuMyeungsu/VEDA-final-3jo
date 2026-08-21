# Server

`server/`는 CCTV 메타데이터와 단말 센서값을 받아 위험도를 판정하고, 단말별
결과와 관제 채널 전환 정보를 MQTT로 발행하는 중앙 서버 영역이다.

## 한눈에 보기

- 핵심 실행 파일: `apps/main_app/forklift_safety_server`
- 입력: CCTV RTSP의 application metadata track, ArUco 메타데이터, 단말 센서 MQTT
- 판정: CCTV×채널별 호모그래피 변환, 다중 카메라 사람 추적, 단말별 위험도 계산
- 단말 구조: `forklift_device_config.json`에 등록된 모든 `terminal_id`를 기동 시 생성
- 브로커: 기본 `127.0.0.1:1883` Mosquitto, 설정으로 외부 브로커와 TLS 사용 가능
- 결과: `forklift/risk/<terminal_id>`, `forklift/assignment/<terminal_id>`,
  `forklift/status/server`
- 로그: UTC `server.log`, 원자적 runtime 상태 snapshot, 위험 상태 SQLite, 선택형 원시 CSV 3종
- 웹 앱: 호모그래피 UI/API `:8001`, 읽기 전용 모니터링 API `:8000`
- 검증: 기본 CTest 16개, 외부 브로커 통합 테스트는 별도 활성화

## 구조

### 목록

- `apps/main_app`: 중앙 안전 서버와 입력·판정·통신·로깅 코드
- `apps/homography_app/web`: CCTV 선택, 캡처, 호모그래피 보정 웹 UI/API
- `apps/homography_app/processing`: ArUco 검출과 호모그래피 계산 C++ 엔진
- `apps/monitoring_app`: 운영 화면 placeholder와 상태 API
- `config`: 공통 장비 설정, 안전 정책, 호모그래피 결과
- `scripts`: 실행·배포 스크립트와 main server systemd 유닛
- `deploy/systemd`: 호모그래피·모니터링 웹 앱 systemd 유닛
- `var`: 로컬 실행 중 생성되는 DB·CSV·텍스트 로그
- `third_party`: 서버 빌드에 필요한 저장소 내부 의존 코드

### 상세

```text
20_server_workspace/
├── run_server.sh                         # 로컬 안전 서버 실행 진입점
└── server/
    ├── apps/
    │   ├── main_app/
    │   │   ├── main.cpp                  # CentralServer, RTSP worker, 종료 처리
    │   │   ├── src/input/                # RTP·ONVIF·ArUco 메타데이터 입력
    │   │   ├── src/logic/                 # 호모그래피·추적·위험판정 파이프라인
    │   │   ├── src/network/               # MQTT 센서 수신·결과·assignment 발행
    │   │   ├── src/logging/               # text log·SQLite·CSV 로거
    │   │   ├── tests/                     # 단위·fixture 통합 테스트
    │   │   └── tools/                     # 센서 시뮬레이터·DB 조회·진단 도구
    │   ├── homography_app/
    │   │   ├── web/                       # :8001 웹 UI/API
    │   │   ├── processing/                # 호모그래피 CLI·라이브러리
    │   │   └── tests/                     # 전역 정합 검증
    │   └── monitoring_app/                # :8000 운영 화면 placeholder
    ├── config/
    │   ├── safety/                        # 위험판정·MQTT·추적·출력 정책
    │   ├── homography/                    # 보정 정책·카메라별 최종 H
    │   ├── camera_model.json              # 모델별 채널 수
    │   ├── camera_list.json               # 실제 CCTV 목록·RTSP 정보
    │   └── forklift_device_config.json    # 실제 단말 목록·marker 정보
    ├── scripts/
    │   ├── run_server.sh                  # server 디렉터리 내부 실행 스크립트
    │   └── deploy.sh                      # 세 앱 통합 배포
    ├── deploy/systemd/                    # 세 앱의 단일 systemd 유닛 원본
    ├── build/                             # CMake 빌드 산출물
    ├── var/main_app/storage/              # 로컬 실행 데이터
    └── third_party/
```

메인 서버 내부 객체의 소유 관계는 다음과 같다.

- `StreamWorker`: 유효한 `stream_id`마다 하나씩 생성한다. GStreamer로 CCTV의
  RTSP application track을 받고 RTP 재조립 후 ONVIF/ArUco 메타데이터를 중앙 큐에 넣는다.
- `CentralServer`: 여러 worker의 입력을 하나의 처리 큐와 처리 스레드에서 순서대로 소비한다.
- `TerminalContext`: 설정된 단말마다 하나씩 생성한다. 단말별 marker, 센서 reader,
  판정 pipeline, 상태 dispatcher, 위험 결과 publisher를 분리한다.
- `SensorUplinkReceiver`: MQTT 구독 연결은 하나만 유지하고, 수신한 최신 센서값을
  `terminal_id`별 cache로 분리한다.
- `AssignmentPublisher`: 모든 단말의 관제 채널 전환을 하나의 MQTT publisher로 발행한다.
- `ResultPublisher`: 단말별 위험 결과 publisher와 서버 상태 publisher를 분리한다.

## 흐름

### 목록

- CCTV 입력 흐름
- ArUco 기반 관제 채널 전환 흐름
- 단말 센서 입력 흐름
- 위험도 판정·발행 흐름
- 위험 상태 저장 흐름

### 상세

```text
CCTV RTSP application track
  → GStreamer appsink
  → RTP 재조립
  → ONVIF/ArUco metadata parser
  → CentralServer event queue
```

객체 메타데이터는 다음 경로를 거친다.

```text
객체 메타데이터
  → stream_id별 픽셀 좌표
  → H_pixel_to_world 변환(mm)
  → 다중 카메라 사람 track
  → 단말별 최근접 사람 선택
  → CCTV·ToF·IMU 위험도 결합
  → ResultDispatcher
  → forklift/risk/<terminal_id>
```

ArUco 메타데이터는 단말별 marker 추적과 handover 확인에 사용한다.

```text
ArUco 메타데이터
  → 단말 marker 위치 추적
  → 활성 stream_id 변경 확인
  → forklift/assignment/<terminal_id>
```

센서 입력은 단말 수와 관계없이 공통 브로커 연결에서 수신한다.

```text
단말 K개
  → forklift/sensor/<terminal_id>
  → Mosquitto
  → SensorUplinkReceiver
  → terminal_id별 최신값 cache
  → 해당 단말의 판정 pipeline
```

위험 결과는 상태가 바뀌면 즉시 발행하고, 상태가 유지되면 기본 200ms 주기의
heartbeat로 최신 결과를 다시 발행한다. `distance_mm`만 변한 경우에는 상태 변화로
보지 않고 다음 heartbeat에 최신 거리를 싣는다.

## 로그

### 목록

- `server.log`: 모든 운영 text log의 파일 기록(기동별 `run_id` 포함)
- `runtime/runtime-status.json`: 모니터링용 queue·센서·저장·단말별 counter snapshot
- 콘솔 출력: 일반·경고는 stdout, 오류는 stderr
- `events.db`: 위험 상태 변화만 저장하는 SQLite DB
- `detections.csv`: 원시 객체 메타데이터 CSV, debug 전용
- `aruco_markers.csv`: 원시 ArUco 메타데이터 CSV, debug 전용
- `latency.csv`: 서버 내부 지연 CSV, debug 전용
- `event_log_viewer`: SQLite 최근 위험 이벤트 조회 도구
- 태그: `SERVER`, `CONFIG`, `CCTV`, `SENSOR`, `HANDOVER`, `ALERT`, `RISK`,
  `STORAGE`, `DEBUG`

### 경로

| 실행 환경 | 저장 경로 |
| --- | --- |
| 저장소에서 직접 실행 | `server/var/main_app/storage/` |
| systemd 배포 실행 | `/var/log/forklift_safety/` |

기본 파일 이름은 다음과 같다.

```text
runtime/server.log
runtime/runtime-status.json
events.db
detections.csv
aruco_markers.csv
latency.csv
```

`output_storage.server_log`가 지정되면 그 경로를 사용하고, 기존 설정처럼 해당 키가
없으면 `event_db`가 있는 디렉터리의 `server.log`로 fallback한다. 따라서 기존 운영
설정을 교체하지 않아도 된다. 설정 로드 중 발생한 로그도 파일 경로가 확정된 뒤 flush된다.

### 형식

```text
[2026-08-21T02:38:24.581Z] [WARN] [CCTV] CAM_01_CH_03 카메라 연결 끊김 (사유: 네트워크 오류 -> 재연결 2/10) [run_id=20260821T023800.004Z-p1234]
```

- 시간: UTC ISO-8601 시각, 밀리초 단위
- 수준: `DEBUG`, `INFO`, `WARN`, `ERROR`
- 태그: 처리 영역
- 메시지: 단말·카메라·stream_id를 포함한 한국어 운영 메시지
- `run_id`: 서버 프로세스 기동을 구분하는 UTC 기동시각+PID. 재시작한 서버의 로그를
  같은 파일/journal에서 분리할 때 사용
- `DEBUG`: 일반 운영에서는 끄고 원시 CSV 또는 송신 진단을 켠 경우에만 출력

### 흐름별 예시

기동·설정:

```text
[... ] [INFO] [STORAGE] 위험 이벤트 DB 연결 완료 (경로: .../events.db)
[... ] [INFO] [SERVER] 중앙 안전 서버 기동 완료 (CCTV 스트림: 2개, 지게차 단말: 1대)
```

CCTV 연결·복구:

```text
[... ] [INFO] [CCTV] CAM_01_CH_03 카메라 연결 성공 (최초)
[... ] [WARN] [CCTV] CAM_01_CH_03 카메라 응답 없음 (5초 타임아웃 -> 재연결 1/10)
[... ] [INFO] [CCTV] CAM_01_CH_03 카메라 재연결 성공 (정상 복구)
```

센서 수신:

```text
[... ] [INFO] [SENSOR] 지게차 센서 통합 수신 연결 완료 (127.0.0.1:1883, 구독: forklift/sensor/+)
[... ] [WARN] [SENSOR] 센서 데이터 형식 오류 (사유: camera_id 누락/형식오류, 토픽: forklift/sensor/TERM_01, 수신 계속, 누적: 1)
```

판정·경보:

```text
[... ] [WARN] [ALERT] [TERM_01] 위험도 상승: SAFE -> CAUTION (거리: 420mm, CAM_01_CH_03)
[... ] [ERROR] [ALERT] [TERM_01] 비상 정지 발령: DANGER -> EMERGENCY (거리: 90mm, CAM_01_CH_03)
[... ] [INFO] [ALERT] [TERM_01] 위험 해제: CAUTION -> SAFE (거리: 800mm, CAM_01_CH_03)
```

관제 채널 전환:

```text
[... ] [INFO] [HANDOVER] TERM_01 관제 채널 자동 전환 -> [CAM_01_CH_03]
```

저장·종료:

```text
[... ] [WARN] [STORAGE] DB 저장 대기열 초과 (이전 이벤트 1건 건너뜀, 누적: 1)
[... ] [INFO] [SERVER] 서버 종료 신호 감지 (안전 종료 진행)
[... ] [INFO] [SERVER] 중앙 안전 서버 정상 종료 완료
```

송신 자체를 확인해야 할 때만 `FORKLIFT_DEBUG_SEND_LOG=1`로 기동한다.

```text
[... ] [DEBUG] [RISK] [판정] risk_event seq=12 reason=변화 terminal=TERM_01 stream=CAM_01_CH_03 camera=CAM_01 channel=3 distance_mm=420.0 risk_level=CAUTION exception_state=NONE ...
```

### 저장 형식

`events.db`에는 heartbeat가 아니라 위험 상태 변화만 저장한다. 주요 판정 컬럼은
`utc_time`, `camera_id`, `stream_id`, `channel`, `terminal_id`, `risk_level`,
`previous_risk_level`, `exception_state`, `distance_mm`이고, 통합 검증을 위해
`decision_id`, `sensor_message_id`, `sensor_producer_run_id`, `sensor_sequence`,
`sensor_ts_ms`, `sensor_age_ms`를 선택적으로 함께 저장한다. 기존 DB는 기동 시 누락
컬럼을 `ALTER TABLE`로 보강한다.

debug CSV의 기본 컬럼은 다음과 같다.

- `detections.csv`: 수신 시각, 지연, 객체 ID·class·likelihood, bbox, world 좌표,
  `stream_id`, `camera_id`, `channel`
- `aruco_markers.csv`: 수신 시각, 지연, channel, marker ID, `stream_id`,
  `camera_id`, 네 꼭짓점 좌표
- `latency.csv`: `decision_id`, `queue_wait_ms`, `judge_ms`, `server_total_ms`

risk payload에는 기존 필드 외에 `server_run_id`, `decision_id`, `publish_seq`,
`send_reason`, 센서 message context가 optional로 들어간다. 구형 Qt 수신기는 모르는
JSON 키를 무시하므로 기존 위험 필드의 의미는 바뀌지 않는다. assignment는
`assignment_id`/`revision`/`server_run_id`를 포함하고 QoS 1 PUBACK과 publish 실패를
`HANDOVER` 로그로 남긴다. 서버 상태 retained payload도 `server_run_id`와 `status_seq`를
포함한다.

## 네트워크

### 포트 목록

| 포트 | 주체 | 방향 | 용도 |
| --- | --- | --- | --- |
| `1883` | Mosquitto | 서버·단말 ↔ 브로커 | MQTT 기본 포트, `system_config.json`으로 변경 가능 |
| `554` | CCTV | 서버 → CCTV | RTSP metadata track, `camera_list.json`에서 지정 |
| `80` | CCTV | 서버 → CCTV | 호모그래피 캡처용 HTTP snapshot/카메라 API |
| `8000` | monitoring app | 운영 PC → 서버 | 운영 화면 placeholder/API |
| `8001` | homography web | 작업 PC → 서버 | 호모그래피 UI/API |

메인 안전 서버는 별도의 TCP listen 포트를 열지 않는다. MQTT 브로커와 CCTV로
연결하는 client이며, 과거 TCP 카메라 할당 포트 `9001`과 센서 업링크 포트 `9002`는
현재 운영 경로가 아니다.

### MQTT API

| 방향 | 토픽 | QoS | Retain | 용도 |
| --- | --- | ---: | :---: | --- |
| 단말 → 서버 | `forklift/sensor/<terminal_id>` | 0 | 아니오 | IMU·ToF 최신 스냅샷 |
| 서버 → 단말 | `forklift/risk/<terminal_id>` | 0 | 예 | 단말별 위험 결과 |
| 서버 → 단말 | `forklift/assignment/<terminal_id>` | 1 | 예 | 관제 채널 전환 |
| 서버 → 전체 | `forklift/status/server` | 0 | 예 | 서버 online/offline 상태 |

센서 토픽의 `<terminal_id>`는 payload가 아니라 토픽에서 읽는다. 서버 설정에
등록되지 않은 ID는 cache에 넣지 않고 거부한다.

센서 업링크:

```json
{
  "camera_id": "CAM_01",
  "tof_ok": true,
  "tof_distance_mm": 420,
  "imu_ok": true,
  "imu_accel_x_g": 0.02,
  "imu_accel_y_g": -0.01,
  "imu_accel_z_g": 1.01,
  "ts_ms": 1754380800123
}
```

센서 메시지는 위 필드를 모두 가져야 하며, 일부만 있는 payload는 해당 메시지만
버리고 MQTT 구독은 계속 유지한다. `ts_ms`는 단말 시각이고 stale 판정은 서버
수신 시각을 사용한다.

위험 결과:

```json
{
  "utc_time": "2026-08-20T02:24:03.000Z",
  "zone": null,
  "terminal_id": "TERM_01",
  "stream_id": "CAM_01_CH_03",
  "camera_id": "CAM_01",
  "channel": 3,
  "exception_state": "NONE",
  "distance_mm": 420.00,
  "risk_level": 1
}
```

- `risk_level`: `0=SAFE`, `1=CAUTION`, `2=DANGER`, `3=EMERGENCY`
- `exception_state`: `NONE`, `SENSOR_FAULT`, `DEAD_RECKONING`,
  `EMERGENCY_IMPACT`, `UNCONFIRMED_PROXIMITY`
- 거리·식별 정보가 아직 없으면 빈 문자열 대신 JSON `null`을 사용한다.
- 값이 바뀌면 즉시 발행하고, 같은 상태에서는 기본 200ms heartbeat로 재발행한다.

관제 채널 전환:

```json
{
  "type": "camera_assignment",
  "terminal_id": "TERM_01",
  "stream_id": "CAM_01_CH_03",
  "camera_id": "CAM_01",
  "channel": 3,
  "utc_time": "2026-08-20T02:24:03.000Z"
}
```

서버 상태:

```json
{"state":"online"}
{"state":"offline"}
```

정상 연결 시 `online`을 직접 발행하고, 비정상 종료 시 MQTT LWT가 `offline`을
발행한다. 정상 종료에서도 서버가 `offline`을 직접 발행해 retain 값이 `online`으로
남지 않게 한다.

### 브로커

- 기본 브로커: Mosquitto local service
- 기본 주소: `127.0.0.1:1883`
- 설정 위치: `config/safety/system_config.json`의 `network.mqtt_host`, `network.mqtt_port`
- 연결 유지: MQTT keepalive 60초
- 재연결: 3초부터 시작해 최대 30초까지 지수 백오프
- TLS: `tls_enabled=true`와 CA·client certificate·key 경로 사용
- main server systemd: `mosquitto.service`에 의존

브로커 상태와 전체 메시지는 다음처럼 확인한다.

```sh
sudo systemctl status mosquitto --no-pager
mosquitto_sub -h 127.0.0.1 -p 1883 -q 1 -v \
  -t 'forklift/status/server' \
  -t 'forklift/risk/#' \
  -t 'forklift/assignment/#'
```

센서 시뮬레이션은 표준 Python만 사용하는 도구로 실행할 수 있다.

```sh
python3 server/apps/main_app/tools/fake_sensor_uplink_sender.py \
  --host 127.0.0.1 --port 1883 \
  --terminal-id TERM_01 --terminal-id TERM_02 \
  --camera-id CAM_01 --distance 800 --interval 0.5
```

`--disconnect-after`는 센서 stale 상황, `--corrupt-every`는 잘못된 JSON 수신
상황을 재현할 때 사용한다.

## 설정

### 목록

- 공통 카메라 모델: `config/camera_model.json`
- 공통 CCTV 목록: `config/camera_list.json`
- 공통 단말 목록: `config/forklift_device_config.json`
- 안전 정책: `config/safety/danger_judgment_config.json`
- 시스템 정책: `config/safety/system_config.json`
- 호모그래피 정책: `config/homography/homography_config.json`
- 최종 H 결과: `config/homography/<camera_id>/`
- 샘플: 각 운영 JSON 옆의 `*.sample.json`

### 파일 역할

| 영역 | 파일 | 주요 내용 |
| --- | --- | --- |
| 공통 | `camera_model.json` | 카메라 모델별 지원 채널 수 |
| 공통 | `camera_list.json` | `camera_id`, model, RTSP·HTTP 주소, 채널·해상도·H 경로 |
| 공통 | `forklift_device_config.json` | `terminal_id`, ArUco `marker_id`, 충돌 반경 |
| 안전 | `danger_judgment_config.json` | 거리 임계값, 위험 단계, 센서 결합 정책 |
| 안전 | `system_config.json` | MQTT, handover, tracking, sensor, stream, 저장 경로 |
| 호모그래피 | `homography_config.json` | 마커 사전, 산출 정책, 전체 정합 정책 |
| 호모그래피 | `homography/<camera_id>/*.json` | `H_pixel_to_world`, 단위 mm, 영상 해상도 |

실제 장비 주소·비밀번호·운영 단말 목록은 샘플과 분리한다. 실제 CCTV·안전 정책
JSON은 Git에 넣지 않고, 배포 대상에 별도로 설치한다.

### 경로

```text
--config-dir         server/config/safety   # 안전 정책·시스템 정책
--common-config-dir  server/config          # 카메라·단말·공통 H 경로
```

`forklift_device_config.json`은 `camera_list.json`과 같은 `server/config/` 레벨에
있다. 안전 서버는 이 파일에 등록된 모든 단말을 기동 시 생성하며, 설정에 없는
단말을 MQTT 메시지만으로 동적으로 추가하지 않는다.

### 식별자

| 식별자 | 의미 | 예시 |
| --- | --- | --- |
| `camera_id` | 물리 CCTV 장비 ID | `CAM_01` |
| `channel` | CCTV 내부 채널 번호 | `3` |
| `stream_id` | CCTV와 채널을 합친 전역 스트림 ID | `CAM_01_CH_03` |
| `terminal_id` | 단말·MQTT 토픽·판정 결과의 분리 키 | `TERM_01` |
| `marker_id` | 단말 위치 추적용 ArUco ID | `1` |

같은 channel 번호가 여러 CCTV에 있어도 `stream_id`가 달라지므로 충돌하지 않는다.
모든 월드 좌표와 거리는 mm 단위다.

## 실행

### 목록

- CMake configure·build
- 로컬 안전 서버
- debug CSV·송신 로그
- 위험 이벤트 조회
- 호모그래피 웹 앱
- 모니터링 앱

### 안전 서버

```sh
cd /home/pms/20_server_workspace
cmake -S server -B server/build
cmake --build server/build -j2
./run_server.sh \
  --config-dir server/config/safety \
  --common-config-dir server/config
```

바이너리를 직접 실행할 때는 다음과 같다.

```sh
./server/build/apps/main_app/forklift_safety_server \
  --config-dir server/config/safety \
  --common-config-dir server/config
```

`run_server.sh`는 브로커를 확인하고 로컬 바이너리를 실행한다. 운영 systemd 서비스나
배포 바이너리가 이미 실행 중이면 기본적으로 종료 코드 3으로 중단한다. 운영 인스턴스를
명시적으로 로컬 실행으로 교체할 때만 `./run_server.sh --takeover`를 사용한다.

### 진단

```sh
# 원시 객체·ArUco·지연 CSV와 DEBUG 로그 활성화
./run_server.sh --debug

# 위험 결과 publish 변화·heartbeat DEBUG 로그 활성화
FORKLIFT_DEBUG_SEND_LOG=1 ./run_server.sh

# 운영 text log
tail -f server/var/main_app/storage/server.log

# 최근 위험 상태 변화 조회
./server/build/apps/main_app/event_log_viewer \
  20 server/var/main_app/storage/events.db
```

`--debug`는 `detections.csv`, `aruco_markers.csv`, `latency.csv`를 활성화한다.
`FORKLIFT_DEBUG_SEND_LOG`는 프로세스 시작 시 한 번만 읽으므로 실행 전에 지정한다.

### 웹 앱

호모그래피 웹 앱:

```sh
HOMOGRAPHY_CONFIG_DIR=server/config/homography \
HOMOGRAPHY_TOOL=server/apps/homography_app/processing/build/homography_tool \
SERVER_COMMON_CONFIG_DIR=server/config \
  python3 server/apps/homography_app/web/server.py
```

- 기본 주소: `http://127.0.0.1:8001`
- 포트 변경: `HOMOGRAPHY_APP_PORT`
- 결과 임시 경로: `/tmp/homography-results`

모니터링 앱:

```sh
SERVER_MONITORING_PORT=8000 \
SERVER_MONITORING_REFRESH_INTERVAL_SECONDS=1 \
  python3 server/apps/monitoring_app/server.py
```

- 기본 주소: `http://127.0.0.1:8000`
- `SERVER_MONITORING_REFRESH_INTERVAL_SECONDS`: 화면 갱신 주기(초), 기본값 `1`
- `/api/status`: 중앙 서버·MQTT·호모그래피·최근 로그 시각의 읽기 전용 상태
- `/health/live`: 모니터링 앱 자체 liveness

## 검증

### 목록

- 기본 CTest: 16개
- unit: 14개
- fixture 기반 integration: 2개
- 외부 MQTT integration: 별도 옵션
- 실제 CCTV·단말이 필요한 운영 E2E: 미등록
- 호모그래피 처리 엔진: 별도 CMake 테스트

### 안전 서버

```sh
cmake -S server -B server/build
cmake --build server/build -j2
QT_QPA_PLATFORM=offscreen \
  ctest --test-dir server/build --output-on-failure

ctest --test-dir server/build -L unit --output-on-failure
ctest --test-dir server/build -L integration --output-on-failure
```

외부 Mosquitto가 준비된 환경에서만 센서 업링크 통합 테스트를 켠다.

```sh
cmake -S server -B server/build \
  -DENABLE_NETWORK_INTEGRATION_TESTS=ON
cmake --build server/build -j2
ctest --test-dir server/build -L requires-mqtt --output-on-failure
```

브로커가 없을 때 이 테스트가 성공한 것으로 간주하지 않는다.

### 호모그래피

```sh
cmake -S server/apps/homography_app/processing \
  -B server/apps/homography_app/processing/build \
  -DBUILD_TESTING=ON
cmake --build server/apps/homography_app/processing/build -j2
ctest --test-dir server/apps/homography_app/processing/build --output-on-failure
```

전체 CCTV×채널 정합에 대한 Python 전역 정합 테스트는 서버 CMake configure 시
Python interpreter가 발견되면 기본 CTest에 포함된다.

## 운영

### 목록

- systemd main server
- 배포 파일과 설정 경로
- 브로커·서버·웹 앱 상태 확인
- 로그·DB·CSV 보관 정책
- 운영 설정 권한
- 카메라·H 파일·단말 등록 점검

### 배포

```sh
cd /home/pms/20_server_workspace
./server/scripts/deploy.sh
```

배포 스크립트는 다음 작업을 수행한다.

- main server 빌드
- `/usr/local/bin/forklift_safety_server` 설치
- `/usr/local/bin/event_log_viewer` 설치
- `/usr/local/bin/homography_tool` 설치
- `server/config/`를 `/etc/forklift_safety/`로 복사
- 로그 디렉터리 `/var/log/forklift_safety/` 생성
- 세 systemd 유닛을 `server/deploy/systemd/`에서 등록
- Mosquitto·중앙 서버·모니터링 앱 활성화
- 호모그래피 앱 중지·비활성화

main server systemd 실행 경로는 다음과 같다.

```text
/usr/local/bin/forklift_safety_server
  --config-dir /etc/forklift_safety/safety
  --common-config-dir /etc/forklift_safety
```

서비스 수명 주기는 다음과 같이 분리한다.

- `forklift_safety_server.service`: 상시 실행, 비정상 종료 시 자동 복구
- `monitoring-app.service`: 상시 실행, 중앙 서버가 내려가도 독립적으로 상태 제공
- `homography-app.service`: 온디맨드 유지보수 도구, 부팅 자동 실행과 자동 재시작 안 함

호모그래피 작업을 시작하고 끝낼 때는 다음 명령을 사용한다.

```sh
sudo systemctl start homography-app.service
sudo systemctl stop homography-app.service
```

### 상태 확인

```sh
sudo systemctl status mosquitto --no-pager
sudo systemctl status forklift_safety_server --no-pager
sudo systemctl status homography-app.service --no-pager
sudo systemctl status monitoring-app.service --no-pager
```

서버 기동 직후 다음 항목을 순서대로 확인한다.

- `CONFIG`: 카메라·채널·H·단말이 제외되지 않았는지
- `STORAGE`: `events.db`와 `server.log`가 열렸는지
- `SENSOR`: `forklift/sensor/+` 구독이 완료됐는지
- `CCTV`: 각 `stream_id` 최초 연결이 성공했는지
- `SERVER`: 스트림 수와 단말 수가 설정과 일치하는지
- MQTT 모니터: `status`, `risk`, `assignment` retained message가 보이는지

### 운영 점검

- 실제 JSON과 카메라 비밀번호는 샘플 파일과 분리하고 권한을 제한한다.
- `camera_list.json`의 모든 활성 채널에 유효한 RTSP URL과 H 파일이 있어야 한다.
- `camera_model.json`의 채널 수와 `camera_list.json`의 채널 목록을 일치시킨다.
- `forklift_device_config.json`의 `terminal_id`와 단말이 발행하는 센서 토픽 suffix를
  일치시킨다.
- `terminal_id`를 추가·삭제하면 서버 재기동으로 `TerminalContext`를 다시 만든다.
- `server.log`, SQLite, CSV는 append 방식이므로 디스크 사용량과 보관 기간을 별도로 관리한다.
- 저장소에는 logrotate 설정이 없으므로 운영 설치 시 로그·CSV·DB 보관 정책을 추가한다.
- MQTT를 외부 호스트로 옮기거나 TLS를 켤 때 방화벽·인증서·브로커 ACL을 함께 점검한다.

## 현황

### 목록

- 다중 CCTV·다중 채널 처리
- 다중 단말별 pipeline·센서 cache·위험 결과 분리
- MQTT 기반 단말 센서·위험 결과·관제 전환
- 서버 상태 online/offline와 LWT
- 공통 설정 루트와 안전 정책 루트 분리
- 모니터링 화면 실제 상태 연동: 미완료
- 실제 장비 기반 운영 E2E 자동화: 미등록
- `zone` 매핑: 현재 결과에서 `null` 가능

### 제한

현재 모니터링 앱은 서비스 연결 확인용 placeholder다. `/api/status`는 실제
서버·브로커·카메라 상태를 집계하지 않는다. 운영 상태 확인은 systemd, `server.log`,
MQTT subscribe를 사용한다.

## 문서

- [안전 서버 설정](config/safety/README.md)
- [공통 카메라·단말 설정](config/README.md)
- [호모그래피 전체 개요](apps/homography_app/README.md)
- [호모그래피 웹 UI/API](apps/homography_app/web/README.md)
- [호모그래피 처리 엔진](apps/homography_app/processing/README.md)
