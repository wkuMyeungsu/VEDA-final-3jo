# 지게차 단말 시스템 개발 및 트러블슈팅 회고록 (Retrospective)

본 문서는 `forklift-device/qt` 운전자 단말 개발 과정에서 발생한 핵심 기술 이슈, 원인 분석, 해결 전략 및 설계 원칙을 정리한 기술 회고 문서입니다.

---

## 📌 목차
1. [사례 1: RTSP 실시간 영상 스트리밍 메모리 폭증(OOM) 방어](#1-사례-1-rtsp-실시간-영상-스트리밍-메모리-폭증oom-방어)
2. [사례 2: 재연결 비동기 타이머 팬아웃 및 예약 누적 폭주 방지](#2-사례-2-재연결-비동기-타이머-팬아웃-및-예약-누적-폭주-방지)
3. [사례 3: 서버 통신 두절 시 FPGA 워치독 팬아웃 및 하드웨어 자율 안전 모드 보장](#3-사례-3-서버-통신-두절-시-fpga-워치독-팬아웃-및-하드웨어-자율-안전-모드-보장)
4. [사례 4: 분산 단말-서버 시계 스큐(1.18초 오차)와 Stale 메시지 필터링 정책 개선](#4-사례-4-분산-단말-서버-시계-스큐118초-오차와-stale-메시지-필터링-정책-개선)
5. [사례 5: 100ms 주기 송신 구조에 따른 양자화 지연(0~100ms) 분리 계측](#5-사례-5-100ms-주기-송신-구조에-따른-양자화-지연0100ms-분리-계측)

---

## 1. 사례 1: RTSP 실시간 영상 스트리밍 메모리 폭증(OOM) 방어

### 💥 문제 현상
* RTSP 고화질 영상(H.264, 30fps)을 수신할 때, UI 스레드 렌더링 부하나 화면 전환 순간에 GStreamer 버퍼 큐에 미처리 프레임이 누적되면서 프로세스 RSS 메모리가 1GB 이상 지속 상승하다가 OOM(Out of Memory) 크래시가 발생하는 문제 발생.

### 🔍 근본 원인 분석
* GStreamer 파이프라인의 `appsink`가 기본 설정(`max-buffers=0` 무제한)으로 동작하여, 소비 속도가 생산 속도보다 느려지는 순간 버퍼가 메모리에 무한히 큐잉됨.
* 임베디드 단말(라즈베리파이 등)의 한정된 RAM(1~2GB) 환경에서 치명적인 시스템 정지 유발.

### 🛠️ 해결책 및 아키텍처 개선
1. **GStreamer 파이프라인 레벨 배압(Backpressure) 설정**:
   * `appsink emit-signals=true sync=false max-buffers=2 drop=true` 속성을 부여하여 큐 버퍼를 2개로 엄격히 제한하고, 초과분은 즉시 하드웨어/파이프라인 레벨에서 드롭.
2. **C++ 애플리케이션 레벨 최신 프레임 1장 보관 정책 (`pushFrame`)**:
   * 멀티스레드 환경에서 `std::atomic_bool m_deliveryQueued`와 `QMutex`를 결합하여, 소비자가 프레임을 렌더링하기 전에 새 프레임이 도착하면 이전 대기 프레임을 덮어쓰고 드롭 카운터를 증가시키는 구조 구축.
3. **결과**:
   * 24시간 연속 가동 시에도 메모리 점유율이 80MB 내외의 일정한 Flat 라인을 유지함을 검증.

---

## 2. 사례 2: 재연결 비동기 타이머 팬아웃 및 예약 누적 폭주 방지

### 💥 문제 현상
* 카메라 또는 FPGA 시리얼 포트가 분리되었을 때, 재연결 시도가 1초 → 2초 주기가 아니라 초당 수십~수백 건으로 폭주하여 CPU 점유율이 100%로 치솟고 로그 파일이 기하급수적으로 커지는 현상 발생.

### 🔍 근본 원인 분석
* 에러 콜백 또는 타임아웃 이벤트 핸들러에서 `QTimer::singleShot(delay, ...)`을 무분별하게 호출함.
* 연결 실패 이벤트가 발생할 때마다 새로운 1회성 타이머가 이벤트 루프에 누적 등록되어, 재연결 실패가 반복될수록 타이머 개수가 2의 제곱수(2 → 4 → 8 → 16...)로 증식하는 **타이머 팬아웃(Timer Explosion)** 발생.

### 🛠️ 해결책 및 아키텍처 개선
1. **단일 멤버 타이머 소유 패턴 (`QTimer m_reconnectTimer`)**:
   * `singleShot` 대신 클래스가 단 하나의 `QTimer` 인스턴스를 멤버로 소유하고 `setSingleShot(true)`로 구성.
   * `scheduleReconnect()` 호출 시 이미 타이머가 동작 중이면 기존 예약을 취소(`stop()`)하거나 재시작함으로써 **"시스템 전체에서 재연결 예약은 항상 최대 1건"**이라는 불변식(Invariant) 보장.
2. **지수 백오프(Exponential Backoff) 및 상한선 적용**:
   * 1초 → 2초 → 4초 → ... → 최대 30초 상한(`kReconnectMaxDelayMs = 30000`)을 적용하여 재시도 폭주 원천 차단.

---

## 3. 사례 3: 서버 통신 두절 시 FPGA 워치독 팬아웃 및 하드웨어 자율 안전 모드 보장

### 💥 문제 현상
* AI 서버(또는 MQTT 브로커)가 다운되었을 때, 단말기가 안전 상태를 유지하기 위해 위험도 0(`SET_RISK(Safe)`)을 FPGA에 계속 주기적으로 쏘아 보냄.
* 그 결과, 실제로는 AI 서버가 죽어 전방 위험을 전혀 감지하지 못하고 있음에도 불구하고, FPGA의 하드웨어 워치독(500ms)이 지속적으로 갱신되어 FPGA가 통신 장애(`COMM_ERROR`) 경고를 띄우지 못하는 치명적 안전 사각지대 발생.

### 🔍 근본 원인 분석
* FPGA 하드웨어 워치독의 본질은 **"단말 프로세스 생존 확인"**이 아니라 **"서버부터 시작된 진짜 위험도 평가 데이터가 유효하게 흐르고 있는가"**를 확인하는 것임.
* 단말이 서버 단절 상태에서 `SET_RISK(0)`을 계속 송신하는 것은 FPGA를 속이는 행위였음.

### 🛠️ 해결책 및 아키텍처 개선
1. **송신 일시 중단 플래그 (`riskTxSuspended`) 도입**:
   * 단말의 MQTT 워치독(1000ms) 또는 네트워크 두절 감지 시, `m_warningDevice->setRiskTxSuspended(true)`를 호출하여 FPGA로의 `SET_RISK` 프레임 송신을 즉각 중단.
2. **하드웨어 페일세이프(Fail-Safe) 유도**:
   * FPGA는 500ms 동안 `SET_RISK`가 수신되지 않으면 하드웨어 FSM이 스스로 `COMM_ERROR` 상태(황색 LED 점멸 + 경고음)로 전환되어 운전자에게 시스템 이상을 즉각 고지.
3. **서버 링크 복구 시 즉각 1회 송신**:
   * 서버 연결이 복구되면 100ms 주기를 기다리지 않고 즉시 최신 위험도를 1회 송신하여 통신을 즉각 재개.

---

## 4. 사례 4: 분산 단말-서버 시계 스큐(1.18초 오차)와 Stale 메시지 필터링 정책 개선

### 💥 문제 현상
* 서버와 단말이 서로 다른 기기(임베디드 Pi와 x86 서버)에서 동작할 때, 단말의 NTP 시계가 서버보다 1.18초 뒤처져 있는 환경에서 서버가 정상 발행하는 모든 실시간 위험 이벤트 메시지가 "1초 이상 지난 오래된 메시지"로 판정되어 전량 폐기(Silent Drop)되는 현상 발생.

### 🔍 근본 원인 분석
* MQTT 브로커에 남아 있던 과거 retained 메시지(서버 종료 전 마지막 상태)를 걸러내기 위해 `ageMs > 1000ms` 폐기 로직을 작성했으나, 이를 **모든 수신 메시지에 상시 적용**하여 시계 오차(Clock Skew) 환경에서 정상 메시지까지 오판정됨.

### 🛠️ 해결책 및 아키텍처 개선
1. **1회성 검사 무장 플래그 (`m_staleCheckArmed`) 도입**:
   * Stale 검사의 목적은 오직 "브로커 접속 직후 큐에 남아 있던 retained 1건"을 걸러내는 것임.
   * 따라서 **"접속/재접속 직후 도착하는 첫 번째 메시지 1건"**에 대해서만 Stale 검사를 수행하고 즉시 무장을 해제(`m_staleCheckArmed = false`).
   * 두 번째 메시지부터는 연속 스트림이므로 시계 오차와 무관하게 즉시 수용.
2. **단말 시계 선행(미래 시각) 허용**:
   * 단말 시계가 서버보다 앞선 경우(`ageMs < 0`)에도 폐기하지 않고 정상 수용하도록 방어 로직 추가.

---

## 5. 사례 5: 100ms 주기 송신 구조에 따른 양자화 지연(0~100ms) 분리 계측

### 💡 기술적 통찰
* `SerialWarningDevice::setRiskLevel()`은 값을 즉시 시리얼로 쏘지 않고 메모리(`m_lastRiskLevel`)에 저장하며, 실제 전송은 100ms 타이머(`handleWatchdogTxTimer`)가 전담함 (FPGA 워치독 keepalive 겸용 설계).
* 이 구조로 인해 이벤트 수신(T1)부터 FPGA 실제 전송(T3) 사이에는 **0~100ms(평균 50ms)의 구조적 양자화 지연(Quantization Delay)**이 발생함.
* 본 프로젝트에서는 T1(수신), T2(화면 렌더링), T3(첫 100ms 송신) 3개 지점을 단조 시계(`QElapsedTimer`)로 분리 계측하여, **"단말 내부 순수 처리 지연"**과 **"주기 통신 아키텍처에 의한 양자화 지연"**을 명확히 분리하여 정량 분석을 달성함.

---

## 6. 사례 6: 시리얼 포트 미연결 시 T3 허위 계측 방지 및 실제 I/O 기반 계측 보장

### 💥 문제 현상
* FPGA 미연결 상태 또는 시리얼 포트 오픈 실패 상태(예: `COM99`, `/dev/serial0` 부재)에서도 `SerialWarningDevice::handleWatchdogTxTimer`에서 `sendFrame()` 호출 전에 T3를 선행 기록하여, CSV에 "하드웨어 트리거까지 N ms"라는 허위 수치가 정상 기록되는 결함 발생.

### 🔍 근본 원인 분석
* `sendFrame()` 내부의 `if (!m_port.isOpen()) return;` 또는 `write` 실패 여부와 무관하게 `LatencyTracker::onT3RiskTransmitted()`가 무조건 호출됨.
* 또한 송신 실패 시에도 `m_lastTransmittedRiskLevel`이 갱신되어 버려, 추후 포트가 정상 복구된 후 첫 실제 송신 시점의 T3 계측을 놓치는 문제 존재.

### 🛠️ 해결책 및 아키텍처 개선
1. **`sendFrame()`의 실제 쓰기 검증 및 `bool` 반환형 전환**:
   * `m_port.write()` 반환 바이트 수가 정확히 패킷 크기(4바이트)와 일치하는 경우에만 `true`를 반환.
2. **실제 전송 성공 시에만 T3 기록 및 상태 갱신**:
   * `sendFrame()`이 `true`를 반환한 경우에만 `onT3RiskTransmitted()`를 호출하고 `m_lastTransmittedRiskLevel`을 갱신.
   * 포트가 닫혀있거나 쓰기 실패한 경우 `onT3RiskTransmitPortUnavailable()`을 호출하여 `t3_ms = -1`로 미기록 처리하고, `m_lastTransmittedRiskLevel`은 갱신하지 않아 포트 복구 후 첫 송신 시 정확히 T3가 측정되도록 보장.
3. **서버 두절과 포트 미연결의 사유 분리**:
   * 의도적 송신 중단(`TX_SUSPENDED`)과 하드웨어 미연결(`PORT_UNAVAILABLE`)을 명확히 분리하여 로깅 및 진단 가능성 확보.

---

## 7. 사례 7: T2 화면 렌더링 지연 편향(Underestimation) 제거 및 Qt Quick Scene Graph 2단계 무장 동기화

### 💥 문제 현상
* T1(위험 이벤트 수신) 직후 `m_t2Armed`를 설정하고 첫 `frameSwapped`를 T2로 기록하면, 렌더 스레드가 "이미 그리기 시작한 과거 프레임(= 새 위험도 반영 전)"의 버퍼 교체 시점이 T2로 확정되어, 실제 화면에 새 위험도가 표출되기까지의 지연(T1→T2)이 최대 1프레임(60Hz 16.7ms, Pi 30fps 33.3ms)만큼 체계적으로 과소평가(축소)되는 결함 발생.

### 🔍 근본 원인 분석
* `frameSwapped`는 위험도 갱신 여부와 무관하게 렌더링 루프 주기마다 계속 발생함.
* GUI 스레드의 속성 변경(`metadataChanged`)이 렌더 스레드의 Scene Graph 노드에 실제로 동기화(`afterSynchronizing`)되기 전에 이전 렌더 사이클의 `frameSwapped`가 도착하면 옛 화면의 swap 시점을 새 화면의 swap 시점으로 오인함.
* 또한 기본 자동 연결(Auto Connection) 시 메인 스레드 이벤트 큐 대기 시간이 계측 시점에 섞여 들어감.

### 🛠️ 해결책 및 아키텍처 개선
1. **Scene Graph 2단계 무장(AwaitingSync → T2Armed → FrameSwapped) 구조 설계**:
   * **T1 수신**: `m_awaitingSync = true` 및 `m_t2Armed = false` 설정.
   * **`QQuickWindow::afterSynchronizing`**: GUI 스레드의 QML 속성이 렌더 스레드에 복사 완료된 직후 호출되어 `m_awaitingSync`를 끄고 비로소 `m_t2Armed = true` 무장.
   * **`QQuickWindow::frameSwapped`**: 새 데이터가 반영되어 렌더링된 프레임이 실제로 GPU/디스플레이 버퍼에 교체되는 첫 순간에만 T2 확정.
2. **`Qt::DirectConnection`을 통한 이벤트 큐 지연 배제**:
   * 렌더 스레드에서 시그널 발생 즉시 동기 실행되어 순수한 디스플레이 버퍼 교체 시점을 정밀하게 단조 시계(`QElapsedTimer`)로 포착.

---

## 8. 사례 8: 지연 계측 결측 사유(`t3_status`) 명시화 및 임베디드 systemd 경로 견고성 확보

### 💥 문제 현상
* 지연 계측 CSV에서 `t3_ms = -1`이 여러 가지 다른 의미(서버 두절, 강제 조기 플러시, FPGA 포트 미연결)로 겹쳐 쓰여 데이터 분석 시 원인 파악 불가.
* `terminal.json`의 `latency_log_path`가 상대 경로(`latency_measurement.csv`)로 되어 있어, Raspberry Pi에서 `systemd` 서비스로 자동 실행 시 작업 디렉터리가 `/`가 되어 루트 디렉터리 쓰기 권한 오류로 CSV 기록이 유실되는 결함 발생.

### 🛠️ 해결책 및 아키텍처 개선
1. **결측 사유 컬럼(`t3_status`) 추가**:
   * CSV 헤더를 `seq,risk_level,t1_ms,t2_ms,t3_ms,t1_to_t2_ms,t1_to_t3_ms,t3_status`로 확장.
   * `OK`: 100ms 주기 FPGA 실제 전송 성공.
   * `TX_SUSPENDED`: 서버 두절로 인한 의도적 송신 중단 (FPGA 워치독 유도).
   * `PORT_CLOSED`: 시리얼 포트 미오픈 / 하드웨어 I/O 실패.
   * `INCOMPLETE`: 이전 측정이 완료되기 전 새 이벤트 유입으로 강제 기록.
2. **절대 경로 표준화 및 안전 자동 변환**:
   * 기본 설정을 `/home/pi/latency_measurement.csv` 절대 경로로 지정.
   * 상대 경로 입력 시 `QFileInfo::absoluteFilePath()`로 자동 변환하고 경고 로그를 출력하며, 기동 시 실제 절대 경로를 콘솔에 1회 명시.

---

## 9. 사례 9: 렌더 스레드 파일 I/O 배제(메모리 큐 + 배치 플러시) 및 뮤텍스 락 경합 지연 배제

### 💥 문제 현상
* `QQuickWindow::frameSwapped`가 `Qt::DirectConnection`으로 렌더 스레드에서 동작하는데, 계측 행이 완료될 때마다 동기적으로 `QFile` open/write/close를 수행하여 Raspberry Pi SD 카드 쓰기 시 렌더 스레드가 블로킹되어 프레임 드랍 및 후속 계측 왜곡 유발.
* T1/T2/T3 핸들러에서 `QMutexLocker`를 획득한 "이후에" `m_timer.elapsed()`를 호출하여, 스레드 간 락 경합 시간만큼 타임스탬프가 뒤로 밀리는 지연 편향 발생.

### 🛠️ 해결책 및 아키텍처 개선
1. **락 획득 전 단조 시계 타임스탬프 선행 캡처**:
   * `QElapsedTimer::elapsed()`는 내부 상태를 수정하지 않는 `const` 단조 시계 읽기 함수이므로 스레드 락 없이도 호출 안전함.
   * `onT1RiskEventReceived`, `onT2FrameSwapped`, `onT3RiskTransmitted` 모두 `m_mutex` 획득 **직전에** `const qint64 now = m_timer.elapsed()`를 선행 저장하여 락 대기 시간 오염을 원천 차단.
2. **메모리 큐(`QVector<Measurement>`) 및 파일 핸들 상시 유지**:
   * 렌더 스레드에서는 완성된 행을 메모리 벡터(`m_pendingRows`)에 append(1마이크로초 미만)만 수행하고 즉시 복귀.
   * `QFile`을 멤버 객체로 항시 열어두고 1초 주기 타이머(`m_diskFlushTimer`) 또는 앱 종료 시점에 락 구간 밖에서 일괄(`writeRowsToDisk`) 디스크 기록 수행.


