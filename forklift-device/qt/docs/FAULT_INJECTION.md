# 지게차 안전 단말 결함 주입 시험 절차서

> 💡 **안내**: 본 문서는 현행 소스코드(기준 커밋 `1e5a27e`) 정적 분석 및 실측 사양을 반영하여 현행화한 절차서입니다.

- **작성일**: 2026-08-21 (현행화 완료)
- **대상 프로그램**: `forklift-device/qt` 운전자 단말 (`operator_terminal`)

---

## 1. 목적과 시험 범위

운전자 단말(`operator_terminal`)이 주변 외부 시스템(FPGA, MQTT 서버, RTSP 카메라)의 비정상 상황 및 통신 결함에 대해 **크래시 없이 안전 모드(Fail-Safe)로 수렴하는지**를 검증합니다.

### 1) 시험 대상 (In Scope)
| 컴포넌트 | 소스 파일 | 담당 결함 영역 |
|---|---|---|
| `SerialWarningDevice` | `common/network/SerialWarningDevice.{h,cpp}` | FPGA UART 링크: heartbeat 무수신(600ms), 체크섬/프로토콜 오류, 포트 재연결(3초) |
| `RiskEventSource` | `common/network/RiskEventSource.{h,cpp}` | MQTT 위험 판정 구독: 브로커 단절, stale/retained 메시지(첫 1건), 무발행 워치독(1000ms), 기동 유예(5초) |
| `HandoverClient` | `common/network/HandoverClient.{h,cpp}` | MQTT 핸드오버 채널 (`forklift/assignment/{id}`): 브로커 단절, 타 단말 대상 메시지 필터링 |
| `RtspVideoSource` | `common/video/RtspVideoSource.{h,cpp}` | RTSP 영상 수신: 접속 실패, 스트림 단절, 프레임 배압 (appsink drop) |
| `ActiveCameraController` | `apps/operator_terminal/ActiveCameraController.cpp` | 카메라 전환: 응답 없는 카메라로의 핸드오버 마감(3초) 및 구 카메라 유예(15초) |

### 2) 시험 대상 제외 (Out of Scope)
- **FPGA 내부(Verilog) 하드웨어 로직**: `gpio-control/src/*.v` (FPGA 담당자 영역)
- **서버 위험도 계산 알고리즘**: `server/` 하위 위험도 판정 수식
- **관제 센터**: `operator-device/qt` (독립 프로젝트)
- **동시 다발 복합 결함**: 단일 결함 주입 시험 우선 검증

---

## 2. 시험 환경 및 준비물

- **실행 파일**: `operator_terminal` (Windows MinGW 빌드 또는 라즈베리파이 CMake 빌드)
- **설정 파일**: `config/terminal.json`, `config/cameras.json` (시험 시 `--config <dir>`로 분리 권장)
- **로그 활성화**:
  - **Windows (PowerShell)**: `$env:QT_LOGGING_RULES = "safety.*.debug=true"`
  - **Linux / Raspberry Pi (Bash)**: `export QT_LOGGING_RULES="safety.*.debug=true"`

---

## 3. 판정 기준 근거 상수 및 분석 발견 사항

코드에 실제로 구현된 상수와 유도 근거입니다.

### 1) 주요 타이밍 상수
| 상수명 | 기본값 | 소스 위치 | 유도 근거 |
|---|---|---|---|
| `kHeartbeatTimeoutMs` (FPGA) | 600ms | `SerialWarningDevice.cpp:28` | FPGA heartbeat(200ms) × 3주기 무수신 감지 |
| `kWatchdogTxIntervalMs` (FPGA) | 100ms | `SerialWarningDevice.cpp:25` | FPGA watchdog(500ms) 대비 5배 여유 주기 송신 |
| `kReconnectDelayMs` (FPGA) | 3000ms | `SerialWarningDevice.cpp:30` | 단일 타이머 기반 3초 주기 선형 재시도 |
| `kServerHeartbeatMs` (MQTT) | 200ms | `RiskEventSource.cpp:27` | 서버 위험 판정 발행 주기 |
| `kWatchdogMultiplier` (MQTT) | 5배 | `RiskEventSource.cpp:28` | 워치독 허용 배수 |
| `kWatchdogThresholdMs` (MQTT) | **1000ms** | `RiskEventSource.cpp:29` | 서버 publish(200ms) × 5배 무수신 시 링크 단절 처리 |
| `kStartupGracePeriodMs` (MQTT) | **5000ms** | `RiskEventSource.cpp:24` | 부팅 직후 네트워크 접속 및 서버 기동 대기 유예 시간 |
| `kStaleThresholdMs` (MQTT) | 1000ms | `RiskEventSource.cpp:23` | 접속 직후 첫 1건(`m_staleCheckArmed`)에 대해 1초 초과 retain 메시지 폐기 |
| `kReconnectDelayBase/Max` (MQTT) | 3s / 30s | `RiskEventSource.cpp:19-20` | libmosquitto 표준 지수 백오프 (실패마다 2배, 최대 30초) |
| `kConnectTimeoutMs` (RTSP) | 5000ms | `RtspVideoSource.cpp:10` | 라즈베리파이 실측 접속 시간(1.1~2초) 고려 여유치 |
| `kReconnectBase/Max` (RTSP) | 1s / 30s | `RtspVideoSource.cpp:11-12` | 실패마다 2배 증가 지수 백오프 (최대 30초) |
| `kSwapDeadlineMs` | 3000ms | `ActiveCameraController.cpp:17` | 핸드오버 첫 프레임 수신 마감 |
| `kCameraLingerMs` | 15000ms | `ActiveCameraController.cpp:13` | 구 카메라 연결 유지 유예 시간 (왕복 전환 대비) |

### 2) 코드 정독 주요 발견 사항 (Key Findings)
> 🔍 **[발견 1] FPGA 재연결 정책**  
> RTSP, MQTT는 실패 시 지수 백오프(최대 30초)를 적용하지만, FPGA 포트 재오픈(`kReconnectDelayMs`)은 3초 고정 선형 재시도입니다. 단일 타이머 객체로 관리되어 재연결 예약이 누적 폭주하지 않습니다 (`SerialWarningDevice.cpp:83-85`).

> 🔍 **[발견 2] PROTOCOL.md 문서 현행화 완료**  
> `gpio-control/src/PROTOCOL.md` 4절에 기재되었던 "heartbeat 타임아웃 감지 미구현" 서술을 정정하고, Pi 측 `SerialWarningDevice::handleHeartbeatWatchTimer()` 600ms 감지 구현 상태를 반영하였습니다.

> 🔍 **[발견 3] 비상정지 래치와 CLEAR_ERROR/SELF_TEST 역할 분리**  
> 비상정지(E-Stop) 및 전진 차단 래치는 FPGA 하드웨어(`estop_latch.v`, `movement_cutoff_latch.v`) 내부 소유이며 현장의 물리 리셋 버튼(`manual_reset_n`)으로만 해제됩니다. `CLEAR_ERROR`는 통신 파싱 에러(Sticky 플래그)만 리셋하는 명령입니다.

> 🔍 **[발견 4] 통신 계층 자동화 단위 테스트 확보**  
> `tests/`에 `test_risk_event_source.cpp` 및 `test_serial_warning_device.cpp`가 추가되어 MQTT 및 시리얼 통신 프레임 파싱/재동기화 단위 시험이 자동화되었습니다.

---

## 4. 결함 주입 시험 시나리오 (12건)

---

### [FPGA 시리얼 통신 결함]

#### F-01. FPGA 시리얼 케이블 물리 분리
- **주입 방법**: 단말 구동 중 Pi GPIO 8/10번 핀(TXD/RXD, `fpga_serial_port=/dev/serial0`) 배선을 물리적으로 분리
- **기대 동작**: 포트가 열린 상태에서 배선만 분리되더라도 `handleHeartbeatWatchTimer()`가 600ms 무수신을 감지하여 연결 끊김 처리
- **판정 기준**: 마지막 정상 프레임 후 600ms 이내 `fpgaConnectionState`가 Connected → Disconnected로 전환. 배선 재연결 시 다음 heartbeat 수신 즉시(약 200ms 내) 자동 복구
- **코드 근거**: `SerialWarningDevice.cpp:279-283`
- **결과**: `미실시`

#### F-02. FPGA 체크섬 손상 바이트 주입
- **주입 방법**: 5바이트 프레임의 마지막 checksum 바이트를 고의로 틀리게 전송 (예: `[0x55, 0x0E, 0x00, 0x00, 0xFF]`, 정상은 `0x5B`)
- **기대 동작**: `handleReadyRead()`가 checksum 불일치를 감지하고 1바이트만 버린 뒤 다음 `0x55` 헤더를 재탐색(관용적 재동기화)
- **판정 기준**:
  1. 프로세스 크래시 없음
  2. 상태값(`fpgaConnectionState`, `estopActive` 등)이 손상 프레임으로 오염되지 않음
  3. 손상 주입이 600ms 이상 지속되면 Disconnected 전환
- **코드 근거**: `SerialWarningDevice.cpp:188-221`
- **결과**: `미실시`

#### F-03. FPGA 프로토콜 위반 및 알 수 없는 커맨드 주입
- **주입 방법**: ① 데이터 바이트 안에 `0x55`를 포함시켜 오탐 재동기화 유발, ② `processFrame` switch에 없는 미정의 `event_code` 전송
- **기대 동작**: 데이터 내부의 `0x55`는 checksum 불일치로 스킵되고, 미정의 이벤트 코드는 `default:` 분기에서 디버그 로그만 남기고 정상 처리
- **판정 기준**: 프레임 경계 밀림 없이 다음 정상 프레임 파싱, 상태 오동작 및 크래시 없음
- **코드 근거**: `SerialWarningDevice.cpp:194-220`, `271-276`
- **결과**: `미실시`

#### F-04. FPGA 포트 오픈 실패 시 재연결 누적 방지 (회귀시험)
- **주입 방법**: `terminal.json`의 `fpga_serial_port`를 존재하지 않는 포트(예: `/dev/serial9`, `COM99`)로 설정 후 5분간 기동
- **기대 동작**: `m_reconnectTimer`가 단일 멤버 타이머로 관리되어 예약이 1건으로 유지
- **판정 기준**: 3초 간격으로 재시도 로그가 선형적(5분에 약 100회 재시도 사이클)으로 발생, 타이머 누적 폭주 없음
- **코드 근거**: `SerialWarningDevice.cpp:115-121`
- **결과**: `미실시`

---

### [MQTT 위험 이벤트 결함]

#### M-01. MQTT 브로커 프로세스 중단
- **주입 방법**: `RiskEventSource`가 연결 중인 mosquitto 브로커 프로세스 강제 종료 (또는 포트 차단)
- **기대 동작**: `onDisconnect` 콜백 호출 및 지수 백오프(3→6→12→24→30초) 재시도
- **판정 기준**: 기동 유예(5초) 경과 후 또는 수신 이력이 있는 경우, 브로커 단절 후 워치독(1000ms) 이내에 `NETWORK_DISCONNECTED` 전환, 브로커 재기동 시 자동 재접속
- **코드 근거**: `RiskEventSource.cpp:19-20`, `109-118`, `180-192`
- **결과**: `미실시`

#### M-02. MQTT Retained 과거 잔여 메시지 수신
- **주입 방법**: `mock_risk_event_server.ps1 -Scenario stale` (또는 과거 `utc_time`을 가진 retained 메시지 발행) 실행 후 단말 접속
- **기대 동작**: 접속 직후 첫 1건(`m_staleCheckArmed = true`)에 대해 `kStaleThresholdMs`(1000ms) 초과 여부를 검사하여 폐기
- **판정 기준**: 10초 전 과거 위험도(CRITICAL 등)가 화면에 반영되지 않고 즉시 폐기됨 (로그 "discarding stale retained message on connect" 확인)
- **코드 근거**: `RiskEventSource.cpp:23`, `148-173`
- **결과**: `미실시`

#### M-03. 위험도 데이터 퍼블리시 중단 (브로커 연결은 유지)
- **주입 방법**: `mock_risk_event_server.ps1 -Scenario silence` 실행 (MQTT 접속/PINGREQ는 유지하되 메시지 무발행)
- **기대 동작**: MQTT 소켓 연결은 유지되나, 100ms 주기의 워치독 타이머가 `kWatchdogThresholdMs`(1000ms, 200ms × 5) 무수신을 감지
- **판정 기준**: 기동 유예(5초) 후 마지막 수신 1000ms 경과 시 예외 상태가 `NETWORK_DISCONNECTED`로 전환
- **코드 근거**: `RiskEventSource.cpp:26-31`, `180-192`
- **결과**: `미실시`

---

### [핸드오버 제어 채널 결함]

#### H-01. 핸드오버 MQTT 브로커 연결 단절
- **주입 방법**: 핸드오버 토픽(`forklift/assignment/{terminal_id}`) 브로커 연결 차단
- **기대 동작**: libmosquitto 지수 백오프(3초~30초)로 자동 재연결 시도
- **판정 기준**: `serverConnectionState`가 Disconnected 전환, 브로커 복구 시 자동 재구독 완료
- **코드 근거**: `HandoverClient.cpp:25-35`, `80-95`
- **결과**: `미실시`

#### H-02. 타 단말 대상 메시지 수신 시 필터링
- **주입 방법**: 다른 단말 ID를 담은 핸드오버 JSON 메시지 발행:  
  `{"type":"camera_assignment","terminal_id":"TERM_99","camera_id":"CAM_02"}`
- **기대 동작**: 수신 페이로드의 `terminal_id`가 본인 ID(`TERM_01`)와 일치하지 않음을 확인하고 폐기
- **판정 기준**: 활성 카메라(`activeCameraId`) 변경 없음 및 무시 로그 기록
- **코드 근거**: `HandoverClient.cpp:97-120`
- **결과**: `미실시`

---

### [RTSP 영상 스트리밍 결함]

#### R-01. RTSP 카메라 영상 스트림 단절
- **주입 방법**: 스트리밍 중 카메라 네트워크 케이블 분리 또는 전원 차단
- **기대 동작**: GStreamer 버스에서 ERROR/EOS 감지 후 1초~30초 지수 백오프로 재연결 시도
- **판정 기준**: `videoConnectionState` Disconnected 전환, 재연결 간격 1→2→4→...→30초 증가, 복구 시 자동 재생
- **코드 근거**: `RtspVideoSource.cpp:11-12`, `152-160`, `305-325`
- **결과**: `미실시`

#### R-02. 응답 없는 카메라로 핸드오버 요청
- **주입 방법**: 통신 불가 카메라로 카메라 전환 명령 수신
- **기대 동작**: 새 카메라의 첫 프레임 도착 전까지 이전 화면 유지, 최대 3000ms(`kSwapDeadlineMs`) 경과 시 타임아웃 강제 전환
- **판정 기준**: 요청 후 3000ms 시점에 화면 전환 및 경고 로그 발생, 이전 카메라에 무한 고착되지 않음
- **코드 근거**: `ActiveCameraController.cpp:17`, `52-60`
- **결과**: `미실시`

#### R-03. 영상 프레임 배압 및 느린 소비자 (OOM 방어)
- **주입 방법**: 스트리밍 중 UI 소비 스레드를 인위적으로 지연
- **기대 동작**: `appsink(max-buffers=2, drop=true)` 설정으로 소비되지 못한 프레임이 버퍼에 무한 누적되지 않고 즉시 드롭
- **판정 기준**: 프로세스 메모리(RSS)가 비정상 폭증하지 않음 (누수 방지 검증)
- **코드 근거**: `RtspVideoSource.cpp:40`, `105`
- **결과**: `미실시`

---

## 5. 관찰 및 측정 방법

### 1) QML 화면 상태 지표
- `StatusStrip.qml`: 서버, 카메라, 센서, FPGA 상태 인디케이터
- `EstopOverlay.qml`: 비상정지(E-Stop) 및 전진 차단(Cutoff) 오버레이 표출
- `RiskHud.qml`: 실시간 위험도 배너 및 연결 끊김(`NETWORK_DISCONNECTED`) 배너

### 2) 시스템 모니터링 명령어

#### Windows (PowerShell)
```powershell
# TCP 포트 연결 상태 확인
Get-NetTCPConnection -LocalPort 1883, 9001 -ErrorAction SilentlyContinue

# 프로세스 메모리(RSS) 및 CPU 1초 샘플러 구동
.\tools\monitor_resources.ps1 -OutputFile "fault_injection_mem.csv"
```

#### Linux / Raspberry Pi (Bash)
```bash
# TCP 포트 연결 상태 확인
ss -tn | grep -E ':1883|:9001'

# 프로세스 메모리(RSS) 및 CPU 1초 샘플러 구동
./tools/monitor_resources.sh operator_terminal fault_injection_mem.csv 1
```

---

## 6. 하드웨어 안전 및 주의 사항

- **비상정지(E-Stop) 래치 해제**: 비상정지 래치는 FPGA 하드웨어(`estop_latch.v`) 내부 소유이므로 소프트웨어 UART 명령으로 풀리지 않으며, 반드시 현장의 **물리 리셋 버튼(`manual_reset_n`)**을 눌러야 해제됩니다.
- **오류 해제(CLEAR_ERROR)**: 통신 체크섬/프로토콜 파싱 에러(Sticky 플래그)만 0으로 리셋합니다.
- **자가진단(SELF_TEST)**: 안전 상태(`effective_risk == SAFE` 및 `!comm_error`, `!estop_active`)에서만 시작되며, 실제 위험 발생 시 즉시 자동 중단됩니다.
