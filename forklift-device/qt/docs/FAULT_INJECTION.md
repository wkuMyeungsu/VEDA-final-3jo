# 지게차 안전 단말 결함 주입 시험 절차서

> 💡 **안내**: 본 문서는 소스코드 정적 분석 결과 및 미실시 시험 계획서이며, 실제 시험 결과 보고서가 아닙니다.  
> (참고: H-01/H-02 항목은 MQTT 전환 이전 TCP 기준 작성본으로 향후 갱신 필요)

- **작성일**: 2026-08-14
- **대상 브랜치**: `fix/forklift-device/video-backpressure-lazy-start` 시점 코드
- **대상 프로그램**: `forklift-device/qt` 운전자 단말 (`operator_terminal`)

---

## 1. 목적과 시험 범위

운전자 단말(`operator_terminal`)이 주변 외부 시스템(FPGA, MQTT 서버, RTSP 카메라)의 비정상 상황 및 통신 결함에 대해 **크래시 없이 안전 모드(Fail-Safe)로 수렴하는지**를 검증합니다.

### 1) 시험 대상 (In Scope)
| 컴포넌트 | 소스 파일 | 담당 결함 영역 |
|---|---|---|
| `SerialWarningDevice` | `common/network/SerialWarningDevice.{h,cpp}` | FPGA UART 링크: heartbeat 무수신, 체크섬/프로토콜 오류, 포트 재연결 |
| `RiskEventSource` | `common/network/RiskEventSource.{h,cpp}` | MQTT 위험 판정 구독: 브로커 단절, stale/retained 메시지, publish 정지 |
| `HandoverClient` | `common/network/HandoverClient.{h,cpp}` | 핸드오버 제어 채널: 서버 단절, 타 단말 대상 메시지 |
| `RtspVideoSource` | `common/video/RtspVideoSource.{h,cpp}` | RTSP 영상 수신: 접속 실패, 스트림 단절, 프레임 배압 |
| `ActiveCameraController` | `apps/operator_terminal/ActiveCameraController.cpp` | 카메라 전환: 응답 없는 카메라로의 핸드오버, 전환 마감/유예 |

### 2) 시험 대상 제외 (Out of Scope)
- **FPGA 내부(Verilog) 하드웨어 로직**: `gpio-control/src/*.v` (FPGA 담당자 영역)
- **서버 위험도 계산 알고리즘**: `server/` 하위 위험도 판정 수식
- **관제 센터**: `operator-device/qt` (독립 프로젝트)
- **동시 다발 복합 결함**: 본 문서는 단일 결함 주입만을 다룸

---

## 2. 시험 환경 및 준비물

- **실행 파일**: `operator_terminal` (Windows MinGW 빌드 또는 라즈베리파이 CMake 빌드)
- **설정 파일**: `config/terminal.json`, `config/cameras.json` (시험 시 `--config <dir>`로 분리 권장)
- **로그 활성화**: 터미널에서 디버그 로그 활성화
  - Windows: `$env:QT_LOGGING_RULES = "safety.*.debug=true"`
  - Linux/Pi: `export QT_LOGGING_RULES="safety.*.debug=true"`

---

## 3. 판정 기준 근거 상수 및 분석 발견 사항

코드에 실제로 하드코딩된 상수와 유도 근거입니다.

### 1) 주요 타이밍 상수
| 상수명 | 기본값 | 소스 위치 | 유도 근거 |
|---|---|---|---|
| `kHeartbeatTimeoutMs` | 600ms | `SerialWarningDevice.cpp:25` | FPGA heartbeat(200ms) × 3주기 무수신 감지 |
| `kWatchdogTxIntervalMs` | 100ms | `SerialWarningDevice.cpp:23` | FPGA watchdog(500ms) 대비 5배 여유 주기 송신 |
| `kReconnectDelayMs` (FPGA) | 3000ms | `SerialWarningDevice.cpp:26` | 주석에는 RtspVideoSource와 동일이라 기재되었으나 실제 3초 고정 (발견 1 참고) |
| `kStaleThresholdMs` (MQTT) | 1000ms | `RiskEventSource.cpp:22` | 서버 publish(200ms) 대비 5배 초과 시 오래된 데이터 폐기 |
| `kWatchdogThresholdMs` (MQTT) | 600ms | `RiskEventSource.cpp:26-28` | 서버 publish 주기 200ms × 3주기 무수신 시 단절 처리 |
| `kReconnectDelayBase/Max` (MQTT) | 3s / 30s | `RiskEventSource.cpp:15-16` | libmosquitto 표준 지수 백오프 (실패마다 2배, 최대 30초) |
| `kConnectTimeoutMs` (RTSP) | 5000ms | `RtspVideoSource.cpp:10` | 라즈베리파이 실측 접속 시간(1.1~2초) 고려 여유치 (커밋 `b6b3c97`) |
| `kReconnectBase/Max` (RTSP) | 1s / 30s | `RtspVideoSource.cpp:11-12` | 실패마다 2배 증가 지수 백오프 (최대 30초) |
| `kSwapDeadlineMs` | 3000ms | `ActiveCameraController.cpp:17` | 핸드오버 첫 프레임 수신 마감 (파이 실측 최대 1514ms의 2배 여유, 커밋 `b6b3c97`) |
| `kCameraLingerMs` | 15000ms | `ActiveCameraController.cpp:13` | 구 카메라 연결 유지 유예 시간 (왕복 전환 대비) |

### 2) 코드 정독 주요 발견 사항 (Key Findings)
> 🔍 **[발견 1] FPGA 재연결 정책 불일치**  
> RTSP, MQTT, 핸드오버는 실패 시 지수 백오프(최대 30초)를 적용하지만, FPGA 포트 재오픈(`kReconnectDelayMs`)만 3초 고정입니다. FPGA가 장시간 분리되면 3초마다 경고 로그가 지속 누적됩니다 (`SerialWarningDevice.cpp:26`).

> 🔍 **[발견 2] PROTOCOL.md 문서 설명 차이**  
> `gpio-control/src/PROTOCOL.md` 4절에 "heartbeat 타임아웃 감지가 미구현"으로 기재되어 있으나, 실제 C++ 코드에는 `handleHeartbeatWatchTimer()`로 600ms 무응답 감지가 이미 구현 완료되어 있습니다 (`SerialWarningDevice.cpp:235-239`).

> 🔍 **[발견 3] CLEAR_ERROR / SELF_TEST 데드코드 및 현장 리셋 UI 부재**  
> `IWarningDevice` 인터페이스에 `sendClearError()` 및 `sendSelfTest()` 함수가 구현되어 있으나, UI(QML) 및 `main.cpp` 어디에서도 호출부가 없습니다. 시리얼 오류 래치 발생 시 화면에서 해제할 수단이 없어 프로세스 재기동이 필요합니다.

> 🔍 **[발견 4] 통신 계층 자동화 단위 테스트 공백**  
> 현재 `tests/`에는 설정 파서 및 좌표 변환 테스트만 존재하며, 통신 계층 3종에 대한 단위 테스트가 없습니다. 시리얼 프레임 파서 및 재동기화 로직은 가상 바이트 입력으로 검증 가능한 순수 로직이므로 QtTest 자동화 단위 테스트 추가를 권장합니다.

---

## 4. 결함 주입 시험 시나리오 (12건)

> **상태**: 전 항목 `결과 = 미실시` (실기기 수행 계획)  
> 💡 **참고**: F-02~F-04는 실물 FPGA 없이도 Windows 가상 COM 포트 페어(예: com0com)로 사전 재현이 가능합니다.


---

### [FPGA 시리얼 통신 결함]

#### F-01. FPGA 시리얼 케이블 물리 분리
- **주입 방법**: 단말 구동 중 Pi GPIO 8/10번 핀(TXD/RXD, `fpga_serial_port=/dev/serial0`) 배선을 물리적으로 분리
- **기대 동작**: 배선 단절 시 `/dev/serial0` 노드는 유지되어 `QSerialPort::errorOccurred`가 발생하지 않을 수 있음 → 포트 재오픈이 아닌 heartbeat 무수신 타이머가 전담 감지
- **판정 기준**: 마지막 정상 프레임 후 600ms 이내 `fpgaConnectionState`가 Connected → Disconnected로 전환. 100ms watchdog 송신은 계속되며, 배선 재연결 시 다음 heartbeat 수신 즉시(약 200ms 내) 자동 복구
- **코드 근거**: `SerialWarningDevice.cpp:22-26`, `235-239`, `142-145`
- **결과**: `미실시`

#### F-02. FPGA 체크섬 손상 바이트 주입
- **주입 방법**: 5바이트 프레임의 마지막 checksum 바이트를 고의로 틀리게 전송 (예: `[0x55, 0x0E, 0x00, 0x00, 0xFF]`, 정상은 `0x5B`)
- **기대 동작**: `handleReadyRead()`가 checksum 불일치를 감지하고 1바이트만 버린 뒤 다음 `0x55` 헤더를 재탐색(관용적 파싱)
- **판정 기준**:
  1. 프로세스 크래시 없음
  2. 상태값(`fpgaConnectionState`, `estopActive` 등)이 손상 프레임으로 변하지 않음
  3. 손상 주입이 600ms 이상 지속되면 Disconnected 전환
- **코드 근거**: `SerialWarningDevice.cpp:158-191`, `22` (`kRxFrameSize=5`)
- **결과**: `미실시`

#### F-03. FPGA 프로토콜 위반 및 알 수 없는 커맨드 주입
- **주입 방법**: ① 데이터 바이트 안에 `0x55`를 포함시켜 오탐 재동기화 유발, ② `processFrame` switch에 없는 알 수 없는 `event_code` 전송
- **기대 동작**: 데이터 내부의 `0x55`는 checksum 불일치로 스킵되고, 미정의 이벤트 코드는 `default:` 분기에서 디버그 로그만 남기고 정상 처리
- **판정 기준**: 프레임 경계 밀림 없이 다음 정상 프레임 파싱, 상태 오동작 및 크래시 없음
- **코드 근거**: `SerialWarningDevice.cpp:164-190`, `199-231`, `PROTOCOL.md 2.1/2.3`
- **결과**: `미실시`

#### F-04. FPGA 포트 오픈 실패 시 재연결 누적 방지 (회귀시험)
- **주입 방법**: `terminal.json`의 `fpga_serial_port`를 존재하지 않는 포트(예: `/dev/serial9`, `COM99`)로 설정 후 5분간 기동
- **기대 동작**: `m_reconnectTimer`가 단일 타이머로 관리되어 예약이 1건으로 유지 (과거 2배씩 폭주하던 결함 검증)
- **판정 기준**: 3초 간격으로 경고 로그가 선형적(5분에 약 100줄)으로 발생, 기하급수적 증가 없음
- **코드 근거**: `SerialWarningDevice.cpp:93-98`, `99-109` (커밋 `59e5c33` 회귀 검증)
- **결과**: `미실시`

---

### [MQTT 위험 이벤트 결함]

#### M-01. MQTT 브로커 프로세스 중단
- **주입 방법**: `RiskEventSource`가 연결 중인 mosquitto 브로커 프로세스 강제 종료 (또는 1883 포트 차단)
- **기대 동작**: `onDisconnect` 콜백 호출 및 지수 백오프(3→6→12→24→30초) 재시도, 600ms 워치독 타임아웃 감지
- **판정 기준**: 중단 후 최대 600ms 이내 화면 예외 상태가 `NETWORK_DISCONNECTED`로 전환, 브로커 재실행 시 자동 재접속
- **코드 근거**: `RiskEventSource.cpp:15-16`, `62`, `105-114`, `163-180`
- **결과**: `미실시`

#### M-02. MQTT Retained 과거 잔여 메시지 수신
- **주입 방법**: 오래된 `utc_time`을 가진 retained 메시지를 발행해 둔 후 단말 실행:  
  `mosquitto_pub -h <broker> -t forklift/risk/TERM_01 -r -m '{"camera_id":"CAM_01","zone":"ZONE_A","risk_level":3,"distance_m":1.0,"exception_state":"NONE","utc_time":"2020-01-01T00:00:00.000Z"}'`
- **기대 동작**: `processPayload()`가 타임스탬프를 검사하여 `kStaleThresholdMs`(1000ms) 초과 시 폐기
- **판정 기준**: 과거 CRITICAL 위험도가 화면에 반영되지 않고 무시됨 (로그 "discarding stale retained message" 확인)
- **코드 근거**: `RiskEventSource.cpp:22`, `151-156`
- **결과**: `미실시`

#### M-03. 위험도 데이터 퍼블리시 중단 (브로커 연결은 유지)
- **주입 방법**: 브로커는 켜둔 채 서버(AI 위험 판정 프로세스)만 종료
- **기대 동작**: MQTT 소켓 연결은 유지되나, 100ms 주기의 워치독 타이머가 600ms 무수신을 감지
- **판정 기준**: 마지막 수신 600ms 후 예외 상태가 `NETWORK_DISCONNECTED`로 전환 (소켓 상태는 Connected 유지)
- **코드 근거**: `RiskEventSource.cpp:29-30`, `163-180`
- **결과**: `미실시`

---

### [핸드오버 제어 채널 결함]

#### H-01. 핸드오버 서버 연결 단절
- **주입 방법**: 핸드오버 브로커/서버 프로세스 강제 종료
- **기대 동작**: `scheduleReconnect()`가 지수 백오프(3초~30초)로 재연결 시도
- **판정 기준**: 연결 상태 Disconnected 전환, 재연결 간격이 3→6→12→24→30초 패턴으로 증가
- **코드 근거**: `HandoverClient.cpp:10-11`, `60-72`, `74-95`
- **결과**: `미실시`

#### H-02. 타 단말 대상 메시지 수신 시 필터링
- **주입 방법**: 다른 단말 ID를 담은 메시지 수신:  
  `{"type":"camera_assignment","terminal_id":"TERM_99","camera_id":"CAM_02"}`
- **기대 동작**: `processLine()`이 `terminal_id` 불일치를 확인하고 무시
- **판정 기준**: 화면 및 `activeCameraId` 변경 없음 (로그 "다른 단말 대상 명령 무시" 기록)
- **코드 근거**: `HandoverClient.cpp:120-131`
- **결과**: `미실시`

---

### [RTSP 영상 스트리밍 결함]

#### R-01. RTSP 카메라 영상 스트림 단절
- **주입 방법**: 스트리밍 중 카메라 네트워크 케이블 분리 또는 전원 차단
- **기대 동작**: GStreamer 버스에서 ERROR/EOS 감지 후 1초~30초 지수 백오프로 재연결 시도
- **판정 기준**: `videoConnectionState` Disconnected 전환, 재연결 간격 1→2→4→...→30초 증가, 복구 시 즉시 재생 복귀
- **코드 근거**: `RtspVideoSource.cpp:10-13`, `205-224`, `296-308`, `320-322`
- **결과**: `미실시`

#### R-02. 응답 없는 카메라로 핸드오버 요청
- **주입 방법**: 통신 불가 카메라로 카메라 전환 명령 수신
- **기대 동작**: 새 카메라의 첫 프레임 도착 전까지 이전 화면 유지, 최대 3000ms(`kSwapDeadlineMs`) 경과 시 강제 전환
- **판정 기준**: 요청 후 3000ms 시점에 화면 전환 및 경고 로그 발생, 이전 카메라에 무한 고착되지 않음
- **코드 근거**: `ActiveCameraController.cpp:13-17`, `52-59`, `RtspVideoSource.cpp:10`
- **결과**: `미실시`

#### R-03. 영상 프레임 배압 및 느린 소비자 (OOM 회귀시험)
- **주입 방법**: 스트리밍 중 UI 스레드를 인위적으로 장시간 지연/일시정지
- **기대 동작**: `appsink(max-buffers=2, drop=true)` 설정으로 소비되지 못한 프레임이 버퍼에 무한 누적되지 않고 즉시 드롭
- **판정 기준**: 프로세스 메모리(RSS)가 비정상 폭증하지 않음 (누수 방지 검증)
- **코드 근거**: `RtspVideoSource.cpp:99-102`, `198-200`, `226-236` (커밋 `03c2a35` 회귀 검증)
- **결과**: `미실시`

---

## 5. 관찰 및 측정 방법

### 1) QML 화면 상태 지표
- `StatusStrip.qml`: 서버, 카메라, 센서, FPGA 4개 상태 인디케이터 및 오류 누적 배지
- `EstopOverlay.qml`: 비상정지(E-Stop) 및 전진 차단(Cutoff) 오버레이 표출 확인
- `RiskHud.qml`: 실시간 위험도 배너

### 2) 시스템 모니터링 명령어
```bash
# 라즈베리파이: TCP 포트 연결 상태 확인
ss -tn | grep -E ':1883|:9001'

# 라즈베리파이: 프로세스 메모리(RSS) 실시간 관찰 (R-03)
watch -n1 'grep VmRSS /proc/$(pgrep -f operator_terminal)/status'
```

---

## 6. 한계 및 미검증 항목

- **CLEAR_ERROR / SELF_TEST 미배선**: `IWarningDevice` 인터페이스에 함수는 정의되어 있으나, 상위 UI(QML) 및 `main.cpp`에서 호출하는 UI 동선이 없습니다. F-02/F-03 발생 후 오류 래치를 화면에서 해제할 수 없으므로 프로세스 재시작이 필요합니다.
- **자동화 테스트 공백**: 통신 계층(`SerialWarningDevice`, `RiskEventSource`, `HandoverClient`)에 대한 QtTest 단위 테스트가 등록되어 있지 않아, 현재 12개 항목 모두 수동 시험에 의존합니다.
- **F-01 물리 배선 분리 가정**: Pi GPIO 직결 UART 배선 분리 시 `QSerialPort::errorOccurred` 미발생 여부는 실기기 실측을 통한 검증이 필요합니다.
