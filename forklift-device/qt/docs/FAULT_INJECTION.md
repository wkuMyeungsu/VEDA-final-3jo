# 결함 주입 시험 절차서 — 지게차 안전 단말 (forklift-device/qt, operator_terminal)

> **이 문서는 시험 절차서이며 결과 보고서가 아님.** 아래 표의 모든 행은 아직 실행되지 않았고,
> `결과` 열은 예외 없이 `미실시`로 비워둠. Pass/Fail 판정이나 실측치는 실제 시험 수행 후
> 이 문서와 별도의 결과 기록에 채워 넣어야 함. 이 문서 자체를 시험 결과로 인용하지 말 것.

작성일: 2026-08-14 / 대상 브랜치: `fix/forklift-device/video-backpressure-lazy-start` 시점 코드

---

## 1. 목적과 범위

### 시험 대상 (In scope)

`forklift-device/qt` 운전자 단말(`operator_terminal`)이 **주변 시스템의 결함에 대해
클라이언트 측에서 어떻게 반응하는지**를 검증. 구체적으로 아래 5개 클래스가 담당하는
"연결 관리·재시도·타임아웃·데이터 검증" 로직만을 대상으로 함.

| 컴포넌트 | 파일 | 담당 결함 |
|---|---|---|
| `SerialWarningDevice` | `common/network/SerialWarningDevice.{h,cpp}` | FPGA UART 링크: heartbeat 무수신, 체크섬/프로토콜 오류, 포트 재연결 |
| `RiskEventSource` | `common/network/RiskEventSource.{h,cpp}` | MQTT 위험 판정 구독: 브로커 단절, stale/retained 메시지, publish 정지 |
| `HandoverClient` | `common/network/HandoverClient.{h,cpp}` | 핸드오버 제어 채널(TCP): 서버 단절, 잘못된 대상 메시지 |
| `RtspVideoSource` | `common/video/RtspVideoSource.{h,cpp}` | RTSP 영상 수신: 접속 실패, 스트림 단절, 프레임 배압 |
| `ActiveCameraController` | `apps/operator_terminal/ActiveCameraController.cpp` | 카메라 전환: 죽은 카메라로의 핸드오버, 전환 마감/유예 |

### 시험 대상이 아닌 것 (Out of scope, 명시적 제외)

- **FPGA 내부(Verilog) 동작** — `gpio-control/src/*.v`, `warning_latch`의 2000ms 하강 지연,
  `self_test_allowed` 조건식, `event_tx_fifo` overflow 처리 등. 이 문서는
  `gpio-control/src/PROTOCOL.md`를 **읽기 전용 참고 자료**로만 사용하며, FPGA 담당자의
  구현물을 검증하지 않음.
- **서버(위험도 판정) 로직** — `server/` 하위 `result_dispatcher.hpp` 등 위험도 계산·발행
  주기 자체의 정확성. 서버가 "무엇을" 보내는지가 아니라, 단말이 그 신호가 "끊겼을 때"
  어떻게 반응하는지만 다룸.
- **`operator-device/qt`** — 완전히 별도 사본(현재 저장소 컨벤션상 forklift-device와 독립
  유지)이므로 대상 아님.
- **동시 다발 결함 조합** (예: FPGA+MQTT+RTSP 동시 단절) — 이 문서는 단일 결함 주입만 다룸.

---

## 2. 시험 환경

### 공통 준비물

- `operator_terminal` 실행 파일 (Windows: `cmake --preset windows-mingw` 빌드, Pi: 기본 CMake 빌드 — `docs/README.md` 빌드 절차 참고)
- 시험용 설정 디렉터리 (`config/terminal.json`, `config/cameras.json` 사본 — 운영 설정을 직접 건드리지 않도록 `--config <dir>` 옵션으로 분리 권장)
- 로그 확인용 터미널 (`QT_LOGGING_RULES` 환경변수로 디버그 로그까지 강제 노출 — 5절 참고)

### 항목별 필요 자원

| 항목군 | 필요 자원 | Windows 가능 여부 |
|---|---|---|
| FPGA (F-01~F-04) | F-01(케이블 물리 분리)만 Pi GPIO 헤더 실물 배선 필수. F-02~F-04는 QSerialPort가 순수 Qt 크로스플랫폼 API(`CMakeLists.txt`에 `Qt6::SerialPort` PUBLIC 링크, `common/CMakeLists.txt:41`)라 가상 COM 포트 페어(예: com0com)로 대체 가능 | F-01 불가 / F-02~F-04 가능 |
| MQTT (M-01~M-03) | mosquitto 브로커(Windows용 설치본 또는 컨테이너), `mosquitto_pub` CLI | 가능 |
| 핸드오버 (H-01~H-02) | TCP 개행-JSON을 보내는 간단한 스텁 서버(파이썬 소켓 스크립트 등) | 가능 |
| RTSP (R-01~R-02) | 실카메라(`config/cameras.json` 대상, `192.168.0.3` 등 사내망) 또는 RTSP 테스트 서버. GStreamer는 이미 Windows 빌드에도 링크되어 있음(`build/windows-mingw/common/.../RtspVideoSource.cpp.obj` 존재 확인) | 제한적 가능 (동일 네트워크 필요), 프로덕션 재현은 Pi 권장 |
| 배압/OOM (R-03) | 장시간 실카메라 스트리밍 + 리소스 모니터링 | Windows도 가능하나 Pi 실측이 더 유효 |

빌드 디렉터리 참고: 현재 `build/windows-mingw`에는 `RiskEventSource.cpp.obj`, `HandoverClient.cpp.obj`,
`RtspVideoSource.cpp.obj`는 존재하나 `SerialWarningDevice.cpp.obj`는 확인되지 않음(최근 추가된
파일이라 이 빌드 디렉터리가 그 시점 이전에 구성된 것으로 추정). F-02~F-04를 Windows에서 실행하려면
먼저 `cmake --build --preset windows-mingw`로 재빌드해 FPGA 경로가 실제로 컴파일되는지 확인 필요.

---

## 3. 판정 기준의 근거

이 문서의 모든 "판정 기준" 열은 하드코딩된 감(感)이 아니라 코드에 실재하는 상수 하나하나에서
나옴. 아래는 그 상수들과, 코드 주석에 남아있는 유도 근거를 한 곳에 모은 것.

| 상수 | 값 | 위치 | 유도 근거 |
|---|---|---|---|
| `kHeartbeatTimeoutMs` | 600ms | `SerialWarningDevice.cpp:25` | FPGA heartbeat 200ms(`PROTOCOL.md` 2절) × 3주기 무수신 |
| `kWatchdogTxIntervalMs` | 100ms | `SerialWarningDevice.cpp:23` | FPGA watchdog 500ms(`PROTOCOL.md` 1.2절) 대비 5배 여유 |
| `kReconnectDelayMs` (FPGA 포트 재오픈) | 3000ms 고정 | `SerialWarningDevice.cpp:26` | 코드 주석: "RtspVideoSource::scheduleReconnect()와 동일한 재시도 간격" — 그러나 실제 RTSP 쪽은 1000ms 시작 지수 백오프라 값이 다름 (아래 관찰 참고) |
| `kStaleThresholdMs` | 1000ms | `RiskEventSource.cpp:22` | 서버 publish 200ms 주기(2026-08-03 정책 확정) 대비 5배 여유 |
| `kWatchdogThresholdMs` | 600ms (200×3) | `RiskEventSource.cpp:26-28` | 서버 publish 주기 200ms × 3배(FPGA 채널과 동일한 "3주기" 관례) |
| `kReconnectDelayBaseSec/MaxSec` (MQTT) | 3s / 30s | `RiskEventSource.cpp:15-16` | libmosquitto 표준 지수 백오프 (`mosquitto_reconnect_delay_set(...,true)`, 62행) |
| `kReconnectBaseDelayMs/MaxDelayMs` (Handover) | 3000ms / 30000ms | `HandoverClient.cpp:10-11` | 실패마다 2배 증가, 상한 30초 |
| `kConnectTimeoutMs` (RTSP) | 5000ms | `RtspVideoSource.cpp:10` | 코드 주석: "파이 실측상 오픈에 1.1~2초+가 걸려 2초로는 정상 접속도 끊겨서 늘림" (커밋 b6b3c97 실측 기반) |
| `kReconnectBaseDelayMs/MaxDelayMs` (RTSP) | 1000ms / 30000ms | `RtspVideoSource.cpp:11-12` | 실패마다 2배, 상한 30초 |
| `kSwapDeadlineMs` | 3000ms | `ActiveCameraController.cpp:17` | 코드 주석: "파이 실측 최대 1514ms의 두 배로 여유를 둠" (커밋 b6b3c97) |
| `kCameraLingerMs` | 15000ms | `ActiveCameraController.cpp:13` | 재접속 비용(1.1~1.5초) 대비 왕복 전환 여유 |

**관찰(설계 비일관성, finding):** FPGA 포트 재오픈(`kReconnectDelayMs`)만 3초 고정이고
지수 백오프가 없음. RTSP/MQTT/Handover 세 채널은 모두 실패마다 간격을 2배로 늘려 30초
상한까지 백오프하는데, FPGA 쪽만 코드 주석에 "RtspVideoSource와 동일"이라고 적혀 있으면서도
실제로는 RTSP의 초기값(1000ms)과도, 백오프 유무와도 다름. 로컬 시리얼 포트 재오픈이라 네트워크
폭주 우려는 낮지만, FPGA가 장시간(수십 분) 분리된 상태로 방치되면 3초마다 영구히 경고 로그가
쌓인다는 차이는 실측으로 확인할 가치가 있음 (F-04 참고).

**FPGA-무응답 감지 관련 중요 확인 사항 (finding):** `gpio-control/src/PROTOCOL.md` §4는
"아직 구현되지 않은 것: heartbeat 자체가 안 오는 상황(FPGA 전원 꺼짐/케이블 분리)에 대한
타임아웃 감지... 마지막 수신 시각을 기록해두고 600ms(heartbeat 3주기) 이상 아무 것도 못 받으면
FPGA 응답 없음을 별도로 판단하는 로직을 추가로 넣는 걸 권장"이라고 명시(라인 279-283).
**실제로 확인한 결과, 이 기능은 이미 구현되어 있음** — `SerialWarningDevice::handleHeartbeatWatchTimer()`
(`SerialWarningDevice.cpp:235-239`)가 정확히 이 권장안대로 `m_lastRxTimer` + 600ms 폴링으로
FPGA 무응답을 감지해 `fpgaConnectionState`를 `Disconnected`로 전환함. 즉 `PROTOCOL.md` §4의
갭 설명은 Pi 쪽 구현 기준으로는 **현재 사실과 다름(stale)**. `PROTOCOL.md`는 다른 담당자의
문서라 이 문서에서 직접 수정하지 않았으나, 담당자에게 갱신을 요청할 가치가 있는 발견.

---

## 4. 시험 항목표

전 항목 `결과 = 미실시`. 아래 절차는 시험 수행자가 그대로 따라할 수 있도록 구체적 명령/값을 포함.

| ID | 주입 방법 (구체적 조작 절차) | 기대 동작 | 판정 기준 (정량, 타이밍 포함) | 근거 (file:line) | 결과 | 비고 |
|---|---|---|---|---|---|---|
| **F-01** FPGA 시리얼 케이블 분리 | 단말 구동 중(`warning_device_type=serial`, `terminal.json:19`), Pi GPIO 8/10번 핀(TXD/RXD, `fpga_serial_port=/dev/serial0`, `terminal.json:15`) 배선을 물리적으로 분리 | 배선만 끊겼을 뿐 `/dev/serial0` 노드 자체는 살아있어 `QSerialPort::errorOccurred`가 발생하지 않을 가능성이 높음(물리 계층 단절은 OS가 감지 못함) → 포트 재오픈 로직이 아니라 heartbeat 무수신 감시가 전담 | 마지막 정상 프레임 후 600ms 이내 `fpgaConnectionState` Connected→Disconnected 전환. 이 구간에도 100ms watchdog TX 송신은 계속되고(`scheduleReconnect()` 미호출, 포트 재오픈 시도 없음) 배선 재연결 시 다음 heartbeat 수신 즉시(최대 약 200ms 이내) 자동으로 Connected 복귀, 수동 조작 불필요 | `SerialWarningDevice.cpp:22-26`(상수), `235-239`(무수신 판정), `142-145`(watchdog TX 계속 송신) | 미실시 | **가정 주의**: "물리 배선 분리 시 errorOccurred 미발생"은 Pi UART 드라이버 동작에 대한 추론이며 코드만으로 확정 불가 — 실기 실측 필수. 만약 반대로 errorOccurred가 발생한다면 3초 고정 간격 재오픈 루프(F-04와 동일 경로)를 타게 되므로 두 경로를 구분해서 기록할 것 |
| **F-02** FPGA 체크섬 손상 바이트 주입 | FPGA 자리에 테스트 지그(2번째 USB-TTL 어댑터 또는 가상 COM 포트 페어)를 연결, pyserial 등으로 5바이트 프레임의 마지막 checksum 바이트만 고의로 틀리게 전송. 예: `[0x55,0x0E,0x00,0x00,0xFF]` (정상 checksum은 `0x55^0x0E^0x00^0x00=0x5B`, `0xFF`로 대체해 불일치 유발) | `handleReadyRead()`가 checksum 불일치를 감지, 1바이트만 버리고 다음 `0x55` 재탐색(관용적 파싱) → 크래시/행 없음, 손상 프레임 자체는 아무 상태도 바꾸지 않음(`processFrame` 미호출) | (a) 주입 중/후 프로세스 생존, (b) `fpgaConnectionState`/`estopActive`/`movementCutoffActive`/`fpgaErrorLatched`가 손상 프레임만으로는 변하지 않음, (c) 손상 프레임 직후 정상 heartbeat를 보내면 다음 프레임부터 정상 파싱 재개, (d) 손상 주입이 600ms 이상 지속되면 F-01과 동일하게 Disconnected로 전환(별개 메커니즘의 정상 동작) | `SerialWarningDevice.cpp:158-191`(파싱/재동기화 루프), `22`(`kRxFrameSize=5`) | 미실시 | 순수 파싱 로직이라 Windows 가상 COM 포트로도 재현 가능(실물 FPGA/Pi 불필요) |
| **F-03** FPGA 프로토콜 위반 프레임 (재동기화 스트레스) | ① 정상 checksum 프레임의 데이터 바이트 값 자체를 `0x55`로 채워(예: `event_detail=0x55`) 파서가 데이터 내부의 우연한 `0x55`에서 오탐 재동기화를 시도하는지 스트레스. ② 이어서 `processFrame`의 `switch`에 없는 `event_code`(0x01~0x04, 0x09~0x0D, 0x0F, 0x10)를 정상 checksum으로 주입 | ① 데이터 안의 `0x55`는 checksum 불일치로 감지되어 1바이트만 스킵 후 진짜 다음 헤더를 정확히 찾음. ② `switch`에 없는 이벤트 코드는 `default:` 분기에서 `qCDebug` 로그만 남기고 크래시 없음. 단, 4번째 바이트 bit6(`movement_cutoff_active`)는 `event_code`와 무관하게 매 프레임 반영되므로(라인 203-204) 0x0F/0x10 프레임에서도 그 값 자체는 정상 갱신됨(예외로 간주) | 재동기화 후 다음 유효 프레임이 프레임 경계 밀림 없이 정확히 파싱(로그 대조), `default` 분기 이벤트 수신 시 movementCutoffActive 이외의 상태(estop/latch/connection)에 스퓨리어스 변경 없음, 크래시 없음 | `SerialWarningDevice.cpp:164-190`(재동기화 루프), `199-231`(`processFrame` switch/default), `PROTOCOL.md` 2.1/2.3절(이벤트 코드 표) | 미실시 | Windows 가상 COM 포트로 재현 가능 |
| **F-04** FPGA 포트 열기 실패 재연결 폭주 (회귀시험) | `terminal.json`의 `fpga_serial_port`를 존재하지 않는 포트명(예: `/dev/serial9`, Windows는 `COM99`)으로 바꿔 기동, 5분 이상 방치 | `m_reconnectTimer`가 멤버 `QTimer` 1개로 관리되어 실패마다 예약이 정확히 1건으로만 유지(과거처럼 2배씩 불어나지 않음) | 3초(`kReconnectDelayMs`) 간격으로 "failed to open ..." 경고 로그가 **선형적으로**(약 1회/3초) 발생 — 5분 관찰 시 약 100줄 내외, 1→2→4→8개로 기하급수 증가하지 않음 | `SerialWarningDevice.cpp:93-98`(과거 결함 설명 주석), `99-109`(`scheduleReconnect`) | 미실시 | 커밋 `59e5c33`(포트 열기 실패 시 재연결 예약이 매번 2배로 늘어나던 문제 수정)의 non-regression 시험. 커밋 메시지 자체에 과거 실측(로그 오류 쌍 1→2→4→8개)이 근거로 남아있음 |
| **M-01** MQTT 브로커 중단 | `RiskEventSource`가 접속 중인 mosquitto 브로커 프로세스를 강제 종료(또는 1883/tcp 방화벽 차단) | `onDisconnect` 콜백 발화로 connectionState Disconnected. libmosquitto 내부 스레드(`mosquitto_loop_start`)가 `mosquitto_reconnect_delay_set(3,30,exponential=true)`에 따라 3→6→12→24→30(상한)초로 재시도. 동시에 `handleWatchdogTimeout()`도 600ms마다 폴링해 무수신 시 synthetic `NetworkDisconnected` 메타데이터 방출 | 중단 직후 최대 600ms 이내 화면 예외 상태가 `NETWORK_DISCONNECTED`로 전환, `serverConnection`/`metadataDistributor` 상태도 Disconnected. 브로커 복구 시 3~30초 백오프 창 내 자동 재접속, 재시작 불필요 | `RiskEventSource.cpp:15-16`(3s/30s), `62`(`mosquitto_reconnect_delay_set`), `105-114`(`onDisconnect`), `26-29`, `163-180`(워치독) | 미실시 | mosquitto Windows 설치본/컨테이너로 Windows에서도 재현 가능 |
| **M-02** MQTT retained 과거 메시지 수신 | 오래된 `utc_time`을 가진 retained 메시지를 브로커에 심어둔 뒤 단말을 (재)기동해 최초 구독 시 즉시 수신되게 함: `mosquitto_pub -h <broker> -t forklift/risk/TERM_01 -r -m '{"camera_id":"CAM_01","zone":"ZONE_A","risk_level":3,"distance_m":1.0,"exception_state":"NONE","utc_time":"2020-01-01T00:00:00.000Z"}'` | `processPayload()`가 `ageMs = now - utc_time` 계산, `kStaleThresholdMs`(1000ms) 초과 시 폐기(`metadataReceived` emit 안 함, 워치독 기준 시각도 갱신 안 함) → 2020년 시점의 CRITICAL(risk_level=3) 값이 화면에 절대 반영되지 않아야 함 | 수신 직후 riskLevel/exceptionState 변경 없음(로그 "discarding stale retained message" 확인), 뒤이어 신선한 메시지가 오면 정상 반영 | `RiskEventSource.cpp:22`(`kStaleThresholdMs=1000`), `151-156`(폐기 로직), `153`(로그 문구) | 미실시 | 토픽명은 `forklift/risk/{terminal_id}`(`RiskEventSource.cpp:38`), `terminal_id`는 `terminal.json`의 실제 값(예: `TERM_01`)으로 치환 |
| **M-03** 센서 데이터 stale (퍼블리시만 정지) | 브로커/네트워크는 정상 유지한 채 서버(퍼블리셔) 프로세스만 중단 — mosquitto 자체는 살아있어 `onDisconnect`는 발화하지 않음 | MQTT 연결(TCP/keepalive)은 Connected 유지하되, `handleWatchdogTimeout()`이 100ms마다 폴링해 마지막 수신 이후 600ms 경과 시 synthetic `NetworkDisconnected` 방출, 재수신 전까지 매 600ms 재통지 | 마지막 publish 후 정확히 600ms(±100ms 폴링 오차) 이내 exceptionState가 `NETWORK_DISCONNECTED`로 전환. **이 시점에도 connectionState 자체는 Connected 유지**(M-01과 감지 경로가 다름을 확인하는 것이 핵심), publish 재개 시 즉시 정상 복귀 | `RiskEventSource.cpp:29-30`(주석: "keepalive는 연결 자체가 끊긴 경우만 감지... 별도로 100ms마다 점검"), `163-180` | 미실시 | M-01과 반드시 구분해서 기록 — 이 항목만 성공해야 워치독 폴링 존재 의의가 검증됨 |
| **H-01** 핸드오버 서버 중단 | `HandoverClient`가 접속 중인 TCP 서버(`handover_port=9001`, `terminal.json:6`) 프로세스를 강제 종료 | `handleDisconnected()`/`handleError()` 발화로 Disconnected. `scheduleReconnect()`가 3000ms부터 시작해 실패마다 2배(상한 30000ms)로 재시도. 재접속 성공 시 backoff 즉시 리셋 및 `sendHello()` 재전송 | 중단 직후 서버 연결 상태 Disconnected, 재시도 간격이 3→6→12→24→30(상한)초 패턴으로 로그에 나타남. 서버 복구 후 첫 접속 성공 시 hello 메시지 재전송 및 이후 정상 handover 수신 재개 | `HandoverClient.cpp:10-11`(상수), `60-72`(disconnected/error→scheduleReconnect), `74-95`(백오프), `40-45`(재접속 시 리셋+sendHello) | 미실시 | 간단한 TCP 스텁(파이썬 소켓 서버 등)으로 Windows에서도 재현 가능 |
| **H-02** 잘못된 terminal_id 수신 | handover 스텁 서버에서 이 단말의 `terminal_id`(예: `TERM_01`)와 다른 값을 담은 메시지 전송: `{"type":"camera_assignment","terminal_id":"TERM_99","camera_id":"CAM_02"}\n` | `processLine()`이 메시지 내 `terminal_id`와 자신의 값 불일치를 감지, `cameraHandoverRequested`를 emit하지 않고 경고 로그만 남김 | 다른 `terminal_id` 메시지 수신 후 `activeCameraId`/화면이 전혀 변경되지 않음(카메라 전환 없음), 로그에 "다른 단말(TERM_99) 대상 명령 무시" 기록 | `HandoverClient.cpp:120-131` | 미실시 | **finding**: `m_terminalId`가 빈 문자열이면 이 필터링 자체가 통과되어 아무 메시지나 수용됨(`HandoverClient.cpp:120` 조건문). `main.cpp:117`에서 항상 `appConfig.terminalId`를 설정하므로 정상 배포 경로에선 발생하지 않으나, `terminal.json`에 `terminal_id`가 빈 값으로 배포되면 이 필터가 조용히 무력화됨 — 코드 결함이 아니라 설정 검증 부재 |
| **R-01** RTSP 스트림 끊김 | 활성 카메라 스트리밍 중 해당 카메라 네트워크 경로 차단(케이블 분리/방화벽/카메라 전원 차단) | GStreamer 파이프라인에서 `GST_MESSAGE_ERROR` 또는 `EOS` 발생 → `busLoop()`이 메인스레드로 `scheduleReconnect()` 위임. `videoConnectionState` Disconnected, 1000ms부터 시작해 실패마다 2배(상한 30000ms)로 백오프 재시도 | 끊김 직후 Disconnected 전환, 재연결 로그("reconnecting in ... ms")가 1→2→4→...→30(상한)초 패턴으로 정확히 증가, 복구 후 첫 성공 재생 시 `m_reconnectDelayMs`가 0으로 리셋(다음 실패는 다시 1초부터) | `RtspVideoSource.cpp:10-13`(상수), `296-308`(ERROR/EOS→scheduleReconnect), `205-224`(백오프), `320-322`(성공 시 리셋) | 미실시 | 실카메라 또는 RTSP 테스트 서버로 Windows에서도 제한적 재현 가능, 프로덕션 재현은 Pi 권장 |
| **R-02** 죽은 카메라로 핸드오버 | 응답 없는 RTSP 주소를 가진 `camera_id`로 `camera_assignment` 전송(또는 `cameras.json`에 unreachable IP 카메라를 등록 후 그 카메라로 전환) | `setActiveCameraId()`가 새 카메라의 첫 `frameReady`를 기다리며 화면을 즉시 바꾸지 않음(이전 화면 유지). 최대 3000ms(`kSwapDeadlineMs`) 동안 첫 프레임이 없으면 프레임 없이 강제 전환, 이후 "신호 없음" 상태로 표시 | 핸드오버 요청 시각부터 정확히 3000ms(±폴링 오차) 이내 화면 전환(이전 카메라에 고착되지 않음), 경고 로그("... sent no frame within 3000 ms -- switching anyway") 확인. **RTSP 자체의 연결 타임아웃(5000ms)이 스왑 마감(3000ms)보다 길어 두 타이머가 독립적으로 동작** — 전환 시점의 videoConnectionState가 아직 Connecting일 수 있음을 확인 포인트로 기록 | `ActiveCameraController.cpp:13-17`(상수+주석), `52-59`(마감 타이머 강제 전환), `65-103`, `RtspVideoSource.cpp:10`(5000ms, 스왑마감보다 김) | 미실시 | 3000ms는 "파이 실측 최대 1514ms의 두 배" 여유(코드 주석) — 최신 파이/네트워크 환경에서 이 실측치 재검증 여지 있음 |
| **R-03** RTSP 배압/느린 소비자 (OOM 회귀시험) | 정상 스트리밍 중 메인 스레드(UI)를 인위적으로 오래 블로킹(무거운 동기 연산 또는 디버거로 메인 스레드 일시정지)시켜 수십 초 이상 유지 | `appsink`(`max-buffers=2 drop=true`)와 단일 슬롯 mailbox(`m_pendingFrame`) 설계로 소비 못한 프레임이 누적되지 않고 즉시 최신 것으로 덮어써짐(드롭 카운트만 증가), 큐가 무한정 자라지 않음 | 블로킹 해제 후 프로세스 RSS가 블로킹 시작 전 대비 비정상적으로 증가해 있지 않음(수십~수백MB 단위 누수 없음), `stop()` 호출 시 "dropped N frames"(N>0) 로그 확인 | `RtspVideoSource.cpp:99-102`(appsink 설정), `226-236`(단일 슬롯 교체+드롭 카운트), `198-200`(정지 시 드롭 수 로그) | 미실시 | 커밋 `03c2a35`(프레임 큐 무한 증가로 인한 OOM 수정)의 non-regression 시험. RSS는 Windows `Get-Process operator_terminal \| Select WS`, Pi `/proc/<pid>/status`의 `VmRSS`로 관찰 |

---

## 5. 관찰 방법

### 로그 카테고리

코드에 정의된 `Q_LOGGING_CATEGORY` 이름 그대로 사용. 기본 상태에서는 `qCDebug`가 안 보일 수
있으므로, 시험 세션 시작 전 아래 환경변수로 전 카테고리의 debug 레벨을 강제 활성화 권장.

```powershell
# Windows
$env:QT_LOGGING_RULES = "safety.*.debug=true"
```
```bash
# Linux/Pi
export QT_LOGGING_RULES="safety.*.debug=true"
```

| 카테고리 | 소스 | 대상 |
|---|---|---|
| `safety.network.fpga` | `SerialWarningDevice.cpp:6` | F-01~F-04 |
| `safety.risk.mqtt` | `RiskEventSource.cpp:13` | M-01~M-03 |
| `safety.handover.client` | `HandoverClient.cpp:8` | H-01~H-02 |
| `safety.video.rtsp` | `RtspVideoSource.cpp:8` | R-01~R-03 |
| `safety.video.manager` | `VideoSourceManager.cpp:11` | R-02 (linger/stop 예약) |
| `safety.activecamera` | `ActiveCameraController.cpp:11` | R-02 |

### 화면 지표 (QML)

- `StatusStrip.qml` — 서버/카메라/센서/FPGA 4개 `ConnectionIndicator`. FPGA 항목은
  `activeCamera.fpgaConnectionState`에 바인딩(`StatusStrip.qml:49`).
- `StatusStrip.qml` 오류 누적 배지 — checksum/protocol/timeout 래치가 하나라도 서면
  "FPGA 오류 누적: 체크섬 · 프로토콜 · 타임아웃" 배지와 "정비자가 초기화할 때까지
  유지됩니다" 캡션이 나타남(`StatusStrip.qml:58-72`). F-02/F-03의 주 화면 지표.
  개별 플래그는 `activeCamera.checksumErrorLatched` 등으로 각각 바인딩됨
  (`ActiveCameraController.h:41-43`).
- `EstopOverlay.qml` — `estopActive`/`movementCutoffActive`가 true면 "⛔ 비상정지" 또는
  "🚫 전진 차단" 오버레이 표출(`EstopOverlay.qml:14,29`).
- `RiskHud.qml` — `riskLevel`/`exceptionState` 배너.
- **미검증 주의**: 위 오류 누적 배지는 이 문서 작성 시점 기준 **아직 파이에서 빌드·표시
  확인이 안 된 신규 코드**임. F-02/F-03 수행 시 배지 표출 여부 자체도 함께 기록하고,
  배지가 안 뜨면 래치 상태는 `safety.network.fpga` 로그로 대조할 것.

### 네트워크/프로세스 관찰

```bash
# Pi: 브로커/핸드오버 서버로의 TCP 연결 상태 및 재연결 시도 관찰
ss -tn | grep -E ':1883|:9001'
```
```powershell
# Windows 대응
netstat -ano | findstr ":1883 :9001"
```
```bash
# Pi: RSS(메모리) 추이 관찰 (R-03)
watch -n1 'grep VmRSS /proc/$(pgrep -f operator_terminal)/status'
```
```powershell
# Windows 대응
Get-Process operator_terminal | Select-Object WS, PM
```

---

## 6. 한계 및 미검증 항목

- **자동화 시험 전무**: `tests/` 디렉터리에는 `test_bbox_aspect_fit`, `test_config_loader`,
  `test_mock_metadata_source` 3개만 존재(`tests/CMakeLists.txt` 확인). `SerialWarningDevice`,
  `RiskEventSource`, `HandoverClient`에 대한 QtTest 단위 시험은 없음. 이 문서의 모든 항목은
  현재 100% 수동 시험이며, 코드 변경 후 회귀를 자동으로 잡아줄 장치가 없음.
- **CLEAR_ERROR/SELF_TEST 미배선**: `IWarningDevice::sendClearError()`/`sendSelfTest()`는
  `SerialWarningDevice`/`NoopWarningDevice`에 구현은 있으나, `main.cpp`·`ActiveCameraController`·
  QML 어디에서도 호출되지 않음(전체 저장소 grep 결과 선언/구현 외 호출 0건). 즉 F-02/F-03에서
  한 번 세팅된 checksum/protocol/timeout 오류 래치는 **이 단말 프로세스 안에서 리셋할 방법이
  없음** — 반복 시험 시 매 시험마다 프로세스 재시작이 필요하고, 운영 중 정비자가 화면에서
  직접 CLEAR_ERROR를 보낼 UI 동선 자체가 존재하지 않음.
- **F-01 가정의 미검증**: GPIO 직결 UART 배선 분리 시 `QSerialPort::errorOccurred`가
  발생하지 않는다는 추론은 Pi 커널 드라이버 동작에 대한 것이며, 코드 리딩만으로는 확정할
  수 없음. 실기 실측 전까지는 가설로 취급.
- **동시 결함 조합 미다룸**: FPGA+MQTT+RTSP 동시 단절, 재연결 폭풍이 겹치는 상황 등은
  이 표에 없음.
- **자동 계측 없음**: 판정 기준의 타이밍은 로그 타임스탬프 수동 대조에 의존. ms 단위
  오차가 중요한 항목(F-01, M-01, M-03, R-02)은 향후 스크립트 기반 harness로 자동
  계측하는 것이 바람직함.
- **`terminal_id` 빈 값 설정 오류**: H-02 비고에 기술한 대로, 코드 결함이 아니라 배포
  설정(`terminal.json`)의 검증 부재이므로 이 표에서 별도 정량 항목화하지 않음.
- **PROTOCOL.md와의 정합성**: 이 문서 작성 중 `gpio-control/src/PROTOCOL.md` §4의 설명이
  Pi 쪽 구현 기준으로 stale함을 발견(3절 참고). FPGA 담당자에게 문서 갱신 필요성을 전달할
  것을 권장하나, 이 문서 자체는 그 문서를 수정하지 않음(다른 사람 담당 영역).
