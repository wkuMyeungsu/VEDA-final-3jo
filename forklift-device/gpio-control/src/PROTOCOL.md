# UART 프로토콜 — 지게차 FPGA 안전 모듈

시스템 경로: 카메라 → 서버(위험도 계산) → **단말(Pi)** ↔ **FPGA** (이 문서의 범위)

FPGA는 위험도를 "계산"하지 않는다. 계산은 서버가 한다. FPGA의 역할은 딱 하나,
**"단말 Pi의 소프트웨어가 죽거나 응답이 없어도, 사람에게 보이는 경고 표시
(LED/부저)와 물리 비상정지만큼은 하드웨어로 계속 보장하는 것"**이다. 이 문서의
모든 설계 결정(watchdog, 이벤트 push, 래치 등)은 전부 이 한 문장에서 나온다.

---

## 0. 전체 신호 지도

FPGA 내부에서 최종적으로 LED/부저를 결정하는 상태는 `warning_state` 하나이고,
값은 6가지, 우선순위는 위에서 아래 순서다.

| 값 | 이름 | 트리거 |
|---|---|---|
| 5 | **ESTOP** | 물리 버튼(`estop_n`)이 눌림 (최우선, 소프트웨어 개입 불가) |
| 4 | **COMM_ERROR** | Pi로부터 500ms 동안 정상 SET_RISK가 없음 |
| 3 | CRITICAL | 서버가 보낸 위험도 3 |
| 2 | DANGER | 위험도 2 |
| 1 | CAUTION | 위험도 1 |
| 0 | SAFE | 위험도 0, 혹은 최초 상태 |

이 중 ESTOP과 COMM_ERROR는 "패킷 안의 값"이 아니라 **"통신/입력 그 자체의
건강 상태"**로 결정된다는 점이 핵심이다. 나머지(체크섬/프로토콜/타임아웃/
프레이밍 오류)는 이 표에 없다 — 이것들은 상태를 직접 바꾸지 않는 별도의
"패킷 파싱 진단 정보"다 (2.2절 참고).

---

## 1. Pi → FPGA (요청, 4바이트 고정, 3종류)

```
[0] 0xAA        헤더
[1] command
[2] data
[3] checksum = 0xAA ^ command ^ data
```

| command | 이름         | data                                  | 주기 |
|--------:|--------------|------------------------------------------|------|
| 0x01    | SET_RISK     | 0~3 (SAFE/CAUTION/DANGER/CRITICAL)       | **100ms마다 반복 전송** (권장) |
| 0x02    | CLEAR_ERROR  | 0x00 고정                                | 필요할 때 1회 |
| 0x03    | SELF_TEST    | 0=STOP, 1=LED, 2=BUZZER, 3=ALL           | 필요할 때 1회 |

**셋 다 100ms마다 오는 게 아니다.** 반복 전송은 SET_RISK만 해당한다. 그 이유는
바로 아래 watchdog 항목에서 설명한다.

### 1.1 SET_RISK — risk_level과 effective_risk는 다르다

`SET_RISK`로 들어온 값은 바로 `risk_level`이라는 원본 값이다. 이게 그대로
LED/부저에 반영되는 게 아니라, `warning_latch`를 거쳐 `effective_risk`(실제
출력에 쓰이는 값)로 변환된다.

**latch가 하는 일은 "내려갈 때만" 지연시키는 것이다.**
- 위험도가 **올라가는** 방향 (예: SAFE→CRITICAL): 지연 없이 즉시 반영
- 위험도가 **내려가는** 방향 (예: CRITICAL→SAFE): 2000ms 동안 이전 값을 유지한
  뒤에 반영

이유: 위험도 급락은 실제 위험이 해소된 것일 수도 있지만, 카메라 프레임 하나가
순간적으로 사람을 놓친 오탐일 수도 있다. 안전 방향(경고 강화)은 항상 즉시,
해제 방향은 항상 신중하게 — 라는 원칙을 latch 하나로 구현한 것이다.

### 1.2 SET_RISK만이 watchdog을 갱신한다

FPGA 안에는 "Pi가 살아있는지" 감시하는 `watchdog` 모듈이 있고, 이 타이머를
갱신(리셋)시킬 수 있는 건 **오직 SET_RISK 뿐**이다. `CLEAR_ERROR`나
`SELF_TEST`를 아무리 자주 보내도 watchdog은 갱신되지 않는다.

이게 바로 SET_RISK를 100ms마다 반복 전송해야 하는 이유다: watchdog의
타임아웃이 500ms이므로, 100ms 주기는 통신 지연이나 패킷 한두 개 유실에도
충분한 여유(5배)를 준다.

- 정상 SET_RISK를 500ms 동안 한 번도 못 받으면 → `comm_error = 1` →
  `warning_state = COMM_ERROR` (risk_level과 무관하게 강제 전환)
- 정상 SET_RISK를 다시 받으면 → `comm_error`는 **그 즉시 자동으로 0으로
  풀린다.** 별도 명령이 필요 없다.

왜 SELF_TEST/CLEAR_ERROR는 watchdog을 갱신 못 하게 막았을까? 만약 이 둘도
watchdog을 갱신시킨다면, 서버의 위험도 계산 파이프라인이 실제로는 멈춰
있는데도(카메라 프레임이 하나도 안 들어오는 등) Pi가 별 의미 없는
SELF_TEST/CLEAR_ERROR만 반복해서 watchdog을 계속 속여 넘길 수 있다. watchdog은
"Pi 프로세스가 살아있냐"가 아니라 **"진짜 위험도 평가가 살아서 흐르고
있냐"**를 확인하는 게 목적이므로, 그 역할을 SET_RISK 하나로 엄격히 제한했다.

### 1.3 패킷 파싱 오류 — 4가지 소스, 그리고 CLEAR_ERROR의 의미

"통신 오류"로 뭉뚱그리기 쉽지만 실제로는 발생 위치가 다른 4가지가 있다.

| 이름 | 발생 위치 | 무엇을 감지하나 |
|---|---|---|
| CHECKSUM_ERROR | packet_parser | 체크섬 바이트가 계산값과 불일치 |
| PROTOCOL_ERROR | packet_parser | 알 수 없는 command, 혹은 범위를 벗어난 data |
| INTERBYTE_TIMEOUT | packet_parser | 패킷 조립 도중 바이트 사이 간격이 10ms 초과 |
| FRAMING_ERROR | **uart_rx** (더 아래 물리 계층) | UART 스타트/스톱 비트 자체가 깨짐 (보레이트 불일치, 전기적 노이즈) |

이 4가지는 `warning_state`를 직접 바꾸지 않는다. 대신 각각 발생할 때마다
1클럭짜리 이벤트로 Pi에 즉시 보고되고(2.3절), 동시에 **한번 세팅되면 스스로는
절대 안 꺼지는 sticky 플래그** 3개에 누적된다:

- `checksum_error_latched`
- `protocol_error_latched`
- `timeout_error_latched`

(framing_error는 별도의 latched 플래그가 없다 — uart_rx 레벨의 순간 이벤트로만
보고된다.)

이 sticky 플래그들은 매 heartbeat(200ms)마다 status byte에 실려서 계속
보고된다. 즉 "지금 이 순간 문제가 있냐"가 아니라 **"마지막으로 리셋한 이후
한 번이라도 파싱 오류가 있었냐"**를 계속 알려주는 진단 정보다. 케이블
접촉불량으로 패킷 몇 개가 깨졌다가 다시 정상화되어도, 이 플래그는 계속 1로
남아서 사람이 "한 번 문제가 있었다"는 걸 놓치지 않게 해준다.

**`CLEAR_ERROR`는 이 3개의 sticky 플래그만 0으로 리셋하는 명령이다.** 정비자가
문제를 확인하고 "인지했고 리셋한다"는 의미로 보내는 것이며, 다음 heartbeat부터
플래그가 다시 0으로 보고된다.

**`CLEAR_ERROR`가 건드리지 않는 것 (자주 헷갈리는 부분):**

| 상태 | 어떻게 풀리나 |
|---|---|
| `comm_error` (watchdog) | CLEAR_ERROR와 **무관**. 정상 SET_RISK가 들어오면 자동으로 풀림 |
| ESTOP | CLEAR_ERROR와 **무관**. UART로는 절대 못 풂. 오직 물리 `manual_reset_n` 버튼 |
| `checksum/protocol/timeout _latched` | **CLEAR_ERROR로만** 풀림 |

ESTOP을 UART 명령으로 못 풀게 만든 건 의도적인 설계다 — 소프트웨어 버그나
오작동으로 비상정지가 실수로 해제되는 사고를 원천 차단하기 위해, 반드시 사람이
현장에서 물리 버튼을 눌러야만 풀리게 했다.

### 1.4 SELF_TEST — 진입 조건과 진행 중 취소

```
self_test_allowed = (effective_risk == SAFE) && !comm_error && !estop_active
```

시작 조건뿐 아니라 **매 클럭 계속 감시**된다. 진행 중에 위험도가 올라가거나,
comm_error/ESTOP이 새로 발생하면 즉시 중단하고 실제 경고 출력으로 복귀한다
(self-test 패턴이 진짜 경고를 가리는 일이 없도록). 명시적으로 STOP을 안 보내도
5초 뒤 자동 종료된다. mode(LED/BUZZER/ALL)에 따라 물리 출력이 달라진다.

---

## 2. FPGA → Pi (push, 5바이트 고정)

Pi는 아무것도 요청하지 않는다 — FPGA가 상태 변화와 주기적 상태 요약을 스스로
쏴 보낸다. **두 가지 서로 다른 트리거가 같은 5바이트 형식을 공유한다:**

1. **heartbeat** — 200ms마다 규칙적으로, 항상
2. **이벤트** — 상태가 바뀌는 그 즉시, 불규칙하게

### 2.1 프레임 형식

```
[0] 0x55                          고정 헤더
[1] event_code                    무슨 메시지인지 (heartbeat도 하나의 event_code=0x0E)
[2] event_detail                  내용물 — code에 따라 의미가 완전히 다름
[3] {overflow(1bit), movement_cutoff_active(1bit), 미사용(3bit), seq(3bit)}
[4] checksum = XOR([0]..[3])
```

- **seq** (3비트, 0→7→0 반복): 메시지 순번. Pi가 직전 값 +1이 아닌 걸 받으면
  중간에 뭔가 유실됐다는 뜻.
- **overflow** (1비트): FPGA 내부 8-depth 전송 버퍼(`event_tx_fifo`)가 꽉 차서
  이벤트를 하나 이상 못 보내고 버렸다는 뜻. 이 버퍼는 "이력 저장소"가
  아니라 UART가 잠깐 바쁠 때 순서를 지키기 위한 아주 짧은 대기열일
  뿐이다 — 정상 운영 중에는 거의 절대 발생하지 않아야 한다.
- **movement_cutoff_active** (1비트): 전진 차단 릴레이가 지금 걸려있는지.
  `event_tx_fifo`를 거치지 않고 전송 시작 시점의 "현재 값"을 그대로
  스냅샷해서 싣기 때문에, heartbeat뿐 아니라 **모든 메시지에 항상 최신
  상태가 실린다.** 래치 상태라 위험도가 SAFE로 돌아와도 계속 1일 수
  있으므로(4.1절), 이 비트로만 정확한 현재 상태를 알 수 있다.

`event_detail`(3번째 바이트)의 의미는 `event_code`마다 다르다:

| event_code | 이름 | event_detail 의미 |
|---|---|---|
| 0x01 | CHECKSUM_ERROR | 0x00 (코드 자체가 전부) |
| 0x02 | PROTOCOL_ERROR | 0x00 |
| 0x03 | INTERBYTE_TIMEOUT | 0x00 |
| 0x04 | FRAMING_ERROR | 0x00 |
| 0x05 | WATCHDOG_TIMEOUT | 0x00 (comm_error 진입) |
| 0x06 | COMM_RECOVERED | 0x00 (comm_error 해제) |
| 0x07 | ESTOP_ACTIVE | 0x00 |
| 0x08 | ESTOP_CLEARED | 0x00 |
| 0x09 | EFFECTIVE_RISK_CHANGED | `{4bit 미사용, 이전위험도[1:0], 새위험도[1:0]}` |
| 0x0A | SELF_TEST_START | `{6bit 미사용, 실행모드[1:0]}` |
| 0x0B | SELF_TEST_DONE | 0x00 |
| 0x0C | ERROR_CLEARED | 0x00 (CLEAR_ERROR 처리됨) |
| 0x0D | SELF_TEST_REJECTED | `{6bit 미사용, 요청모드[1:0]}` |
| 0x0E | **HEARTBEAT** | 아래 2.2절 |
| 0x0F | **MOVEMENT_CUTOFF_ACTIVE** | 0x00 (CRITICAL 또는 ESTOP로 전진 차단 래치됨) |
| 0x10 | **MOVEMENT_CUTOFF_CLEARED** | 0x00 (manual_reset로 해제됨) |

### 2.2 HEARTBEAT의 event_detail — 상태 요약 비트맵

```
bit7  timeout_error_latched
bit6  protocol_error_latched
bit5  checksum_error_latched
bit4  latch_active        (warning_latch가 하강 지연 중)
bit3  self_test_active
bit[2:0]  warning_state    (0장 표 참고)
```

한 바이트에 "지금 살아있다" + "현재 표시 중인 경고 상태" + "self-test 중인지"
+ "누적 파싱 오류 여부"까지 다 들어있다. Pi는 이 heartbeat만 계속 받아도
FPGA를 다시 붙였을 때 상태를 알기 위해 별도로 조회할 필요가 없다 (요청-응답
왕복 자체가 이 프로토콜에 존재하지 않는다).

### 2.3 EFFECTIVE_RISK_CHANGED, SELF_TEST_START/REJECTED

- `EFFECTIVE_RISK_CHANGED`는 **원본 SET_RISK 값이 아니라 latch를 거친
  effective_risk의 변화**를 보고한다. 즉 이 이벤트가 왔다는 건 실제로
  LED/부저 표시가 바뀌었다는 뜻이다.
- `SELF_TEST_START`의 detail은 **실제로 실행된 모드**, `SELF_TEST_REJECTED`의
  detail은 **Pi가 요청했던 모드**다 — 이 둘은 서로 다른 내부 신호에서
  오므로 값이 다를 수 있다 (예: 요청은 ALL이었는데 거부됐다면 REJECTED의
  detail=ALL).

---

## 3. 조종기 전진 차단 (신규 — UART가 아니라 물리 릴레이 개입)

이 기능은 UART 프로토콜이 아니라, FPGA GPIO 1핀으로 조종기 보드의 전진 버튼
라인에 물리적으로 개입하는 하드웨어 확장이다.

### 3.1 왜 릴레이인가

CD4066 같은 반도체 아날로그 스위치는 전원이 없으면 기본이 OFF(끊김)다.
"FPGA가 죽으면 원래대로(전진 가능) 동작해야 한다"는 요구사항을 만족하려면,
**전원 없이도 기계적으로 한쪽 상태(도통)를 유지하는 부품**이 필요하다 —
소형 신호용 릴레이의 NC(Normally Closed) 접점을 쓴다.

```
FPGA GPIO(fwd_cutoff_relay_en) ── 저항 ── NPN 트랜지스터 베이스
                                                │
릴레이 코일(+3.3V) ─────────────────────────────┘ 콜렉터
                (코일과 병렬로 플라이백 다이오드 필수)
트랜지스터 베이스 ── 풀다운 저항(10kΩ) ── GND
```

릴레이의 NC 접점은 조종기의 전진 버튼-인코더 핀 사이에 직렬로 연결한다.

| 상태 | fwd_cutoff_relay_en | 코일 | 접점 | 전진 |
|---|---|---|---|---|
| 평상시 / FPGA 전원 없음 | 0 (또는 신호 없음) | 비여자 | NC 닫힘 | 정상 동작 |
| CRITICAL 또는 ESTOP | 1 | 여자 | NC 열림(NO로 전환) | 차단 |

베이스의 풀다운 저항은 FPGA 설정 로딩 중처럼 GPIO가 순간적으로 뜬(floating)
상태에서도 "전진 가능" 쪽으로 수렴하게 하기 위한 것이다.

### 3.2 트리거와 해제 (팀 결정 사항)

```
cutoff_trigger = estop_active || (effective_risk == CRITICAL)
```

- **COMM_ERROR는 트리거에서 제외했다** (팀 결정). Pi 응답이 없다는 것 자체가
  위험도를 모르는 상태이긴 하지만, CRITICAL/ESTOP만큼 확실한 위험은 아니라고
  판단. 시뮬레이션으로 comm_error가 500ms 이상 지속돼도 이미 걸려있던
  cutoff 래치가 풀리지도, 새로 걸리지도 않는 것을 확인했다.
- **후진/좌우회전/상승하강은 차단하지 않는다** — 전진 한 라인만 릴레이로
  끊는다. 위험 상황에서 조종자가 후진 등으로 빠져나갈 수 있는 퇴로를
  남겨두기 위함.
- **해제는 ESTOP과 동일한 물리 리셋 버튼(`manual_reset_n`)을 공유한다.**
  즉 ESTOP과 전진 차단은 항상 한 번의 물리 조작으로 같이 풀린다. 위험
  상태(effective_risk가 다시 SAFE 등으로 내려가는 것)만으로는 절대
  자동으로 풀리지 않는다 — ESTOP 래치와 완전히 같은 패턴.

### 3.3 관련 이벤트/상태

- `event_code = 0x0F/0x10` (2.3절 표) — 래치가 걸리고/풀리는 순간 각각 1회 push
- 모든 메시지의 4번째 바이트 bit6 — 지금 이 순간 래치가 걸려있는지 (2.1절)

## 4. 7세그먼트 디스플레이 (신규 — `warning_state` 실물 표시)

이 기능도 3장과 마찬가지로 UART 프로토콜이 아니라 FPGA 출력 핀을 통한 하드웨어
확장이다. `warning_state`(0장 표) 값을 Pi를 거치지 않고 FPGA가 직접, 항상
실시간으로 보여주는 세 번째 물리 출력 채널이다 (기존 LED/부저에 추가).

### 4.1 신호 경로

```
warning_state[2:0] (조합논리, 0~5)
        │  {1'b0, warning_state} 로 zero-extend
        ▼
warning_state_bcd[3:0]  ── FPGA 출력 핀 4개 (BCD)
        │
        ▼
SN74LS47N (BCD → 7세그먼트 디코더)
        │  액티브로우 오픈컬렉터 출력, 세그먼트당 직렬 저항 필수
        ▼
공통양극(common-anode) 7세그먼트 디스플레이
```

### 4.2 핀 배치

| 신호 | 핀 | 뱅크/전압 |
|---|---|---|
| `warning_state_bcd[0]` (weight 1) | 35 | 2.5V |
| `warning_state_bcd[1]` (weight 2) | 34 | 2.5V |
| `warning_state_bcd[2]` (weight 4) | 32 | 2.5V |
| `warning_state_bcd[3]` (weight 8) | 31 | 2.5V |

원래는 41/42/43/44 (3.3V 뱅크)로 계획했으나, PCB 실배선을 따라
uart_tx_o/fwd_cutoff_relay_en/manual_reset_n이 그 핀들로 옮겨오면서
충돌해 새 점퍼선으로 J4의 미사용 헤더 자리(35/34/32/31)로 재배정했다
(`uart_rx.cst` 참고). 이 뱅크는 2.5V라 74LS47의 VIH_min(약 2.0V) 대비
여유가 약 0.5V로, 원래 의도했던 3.3V 뱅크(여유 약 1.3V)보다 줄어든다 —
노이즈 여유가 빠듯한 편이니 배선을 짧게 유지할 것. LT\/RBI\는 VCC에
고정해 항상 정상 디코드 모드로 둔다.

### 4.3 표시값과 warning_state의 관계

`warning_state_bcd`는 `warning_state`를 그대로 zero-extend만 한 값이라, 0장의
표(0=SAFE ~ 5=ESTOP)가 그대로 세그먼트 숫자로 뜬다. 이 신호는 heartbeat나
이벤트 push와 무관한 **순수 조합논리**라서, UART가 아예 죽어있어도(예:
comm_error 상태) "4"가 계속 표시된다 — LED/부저와 마찬가지로 Pi 개입 없이
FPGA 단독으로 항상 최신 상태를 보여준다는 원칙(문서 맨 위 설계 원칙)을 세
번째 채널로 한 번 더 만족시킨다.

### 4.4 배선 시 주의

- 세그먼트 출력(a~g, dp)은 오픈컬렉터이므로 각 라인에 직렬 저항(공통양극
  5V 기준 보통 220~330Ω) 없이 디스플레이에 바로 연결하면 안 된다.
- BCD 4비트 중 한 줄이라도 접촉불량이면 특정 숫자만 골라서 안 보이는
  증상으로 나타난다 (예: bit1 단선 시 2/3이 각각 0/1로 보임) — 실제로
  겪었던 문제이므로 브레드보드 단계에서는 특히 주의.

## 5. Pi 쪽 구현 시 주의할 점 (권장)

- 아직 구현되지 않은 것: **heartbeat 자체가 안 오는 상황(FPGA 전원 꺼짐/케이블
  분리)에 대한 타임아웃 감지.** seq 불연속(패킷 유실)은 감지할 수 있지만,
  아예 아무 메시지도 안 오면 그건 seq 비교로는 못 잡는다. 마지막 수신 시각을
  기록해두고 600ms(heartbeat 3주기) 이상 아무 것도 못 받으면 "FPGA 응답 없음"을
  별도로 판단하는 로직을 추가로 넣는 걸 권장한다.
- 표준입력(콘솔)이 없는 환경(systemd 서비스 등)에서 상시 모니터링 데몬으로
  띄울 경우, stdin EOF로 프로그램이 즉시 종료되지 않도록 별도 처리가 필요하다.
