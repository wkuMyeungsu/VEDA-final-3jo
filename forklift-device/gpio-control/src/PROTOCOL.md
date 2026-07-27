# UART 프로토콜 v2 (지게차 FPGA 안전 모듈)

## 설계 원칙

FPGA는 "Pi가 죽거나 통신이 끊겨도 하드웨어로 보장되어야 하는 안전 최종
방어선" 역할만 담당한다. 상태 조회(polling)는 없다 — FPGA가 상태 변화와
현재 상태를 스스로 push한다. 이력 로그의 저장/조회 책임은 Pi로 이관됐다
(Pi는 자신이 보낸 SET_RISK 값과 FPGA가 push하는 이벤트를 그대로 자체
DB/로그 파일에 적재하면 된다).

---

## 1. Pi → FPGA (요청, 4바이트 고정)

```
[0] 0xAA        헤더
[1] command
[2] data
[3] checksum = 0xAA ^ command ^ data
```

| command | 이름         | data                                  |
|--------:|--------------|----------------------------------------|
| 0x01    | SET_RISK     | 0~3 (SAFE/CAUTION/DANGER/CRITICAL)     |
| 0x02    | CLEAR_ERROR  | 0x00 고정                              |
| 0x03    | SELF_TEST    | 0=STOP, 1=LED, 2=BUZZER, 3=ALL         |

- **SET_RISK만 FPGA의 Pi 생존 감시(watchdog) 타이머를 리셋한다.**
  500ms 이상 SET_RISK가 없으면 FPGA는 COMM_ERROR 상태로 전환된다.
  CLEAR_ERROR/SELF_TEST를 반복 전송해도 watchdog은 리셋되지 않는다
  (진짜 위험도 평가가 살아있는지만 확인하기 위함).
- 바이트 간 10ms 이상 공백이 있으면 해당 패킷은 폐기된다(inter-byte timeout).
- comm_error 상태에서는 SELF_TEST 명령이 거부된다(REJECTED 이벤트 발생).

---

## 2. FPGA → Pi (push, 5바이트 고정, 요청 없이 자동 전송)

```
[0] 0x55                          헤더
[1] event_code
[2] event_detail
[3] {overflow(1bit), 0000, seq(3bit)}
[4] checksum = XOR([0]..[3])
```

- `seq`(3비트, 0~7 반복): 메시지가 순서대로 오는지 확인용. 하나라도 건너뛰면
  중간에 FIFO에서 이벤트가 밀렸다는 뜻이다(정상 상황에서는 거의 발생하지
  않는다. 8-depth 전송 FIFO가 UART가 잠깐 바쁠 때의 버퍼링을 흡수한다).
- `overflow`: 1이면 FIFO가 가득 차 일부 이벤트가 유실됐다는 뜻. Pi는 이
  비트를 보면 "무언가 놓쳤다"는 사실을 로그에 남겨야 한다(정상 운영 중에는
  발생하지 않아야 하며, 발생한다면 이벤트 폭주 원인을 조사해야 한다).

### event_code 목록

| code | 이름                     | event_detail                               |
|-----:|--------------------------|---------------------------------------------|
| 0x01 | CHECKSUM_ERROR           | 0x00                                        |
| 0x02 | PROTOCOL_ERROR           | 0x00                                        |
| 0x03 | INTERBYTE_TIMEOUT        | 0x00                                        |
| 0x04 | FRAMING_ERROR            | 0x00                                        |
| 0x05 | WATCHDOG_TIMEOUT         | 0x00 (comm_error 진입)                      |
| 0x06 | COMM_RECOVERED           | 0x00 (comm_error 해제)                      |
| 0x07 | ESTOP_ACTIVE             | 0x00                                        |
| 0x08 | ESTOP_CLEARED            | 0x00                                        |
| 0x09 | RISK_CHANGED             | {prev_risk[1:0], risk[1:0]} (하위 4비트)    |
| 0x0A | SELF_TEST_START          | self_test_mode (Pi 명령 기준, POST는 0x00*) |
| 0x0B | SELF_TEST_DONE           | 0x00                                        |
| 0x0C | ERROR_CLEARED            | 0x00                                        |
| 0x0D | SELF_TEST_REJECTED       | self_test_mode                              |
| 0x0E | **HEARTBEAT** (신규)     | 상태 요약 바이트 (아래 참고)                 |

\* 알려진 제한사항: 전원 인가 POST가 자동으로 시작한 SELF_TEST_START
이벤트는 event_detail이 요청 mode(TEST_ALL=3)가 아니라 0x00으로 찍힌다
(packet_parser의 self_test_mode 레지스터를 그대로 재사용해서 생기는
사소한 표시 오차이며, 실제 동작에는 영향 없다 — 부팅 직후 5초 이내에
Pi 명령 없이 온 SELF_TEST_START라면 POST로 간주하면 된다).

### HEARTBEAT (0x0E) — 200ms 주기 자동 전송

FPGA의 생존 신호이자 상태 요약 방송. Pi는 이 메시지가 600ms(3주기) 이상
끊기면 FPGA 자체 이상(hang/미장착/전원 이상)으로 판단할 수 있다 —
FPGA→Pi 방향의 감시로, Pi→FPGA watchdog(500ms)과 대칭을 이룬다.

event_detail 비트 구성:

```
[7] timeout_error_latched
[6] protocol_error_latched
[5] checksum_error_latched
[4] latch_active       (위험도 하락 유지 중)
[3] self_test_active
[2:0] warning_state    (0=SAFE 1=CAUTION 2=DANGER 3=CRITICAL 4=COMM_ERROR 5=ESTOP)
```

Pi가 재접속 직후 FPGA의 현재 상태를 알고 싶을 때도 별도 조회 명령 없이
다음 heartbeat(최대 200ms 대기)를 기다리면 된다.

---

## 3. 이전 버전(v1)과의 차이

| 항목 | v1 | v2 |
|---|---|---|
| Pi→FPGA 명령 | 7개 (READ_STATUS/READ_LOG_INFO/READ_LOG/CLEAR_LOG 포함) | 3개 |
| FPGA→Pi | 요청-응답 | push 전용 |
| 이력 로그 | FPGA 내부 16-entry 휘발성 버퍼 | Pi가 수신 이벤트를 자체 저장 |
| 생존 감시 | Pi→FPGA만 | Pi→FPGA + **FPGA→Pi(신규)** |
| 이벤트 유실 감지 | 없음 | seq + overflow 비트(신규) |
| 전원 인가 자가진단 | 없음 | POST 자동 실행(신규) |

**Pi 쪽 드라이버는 이 변경사항에 맞춰 다시 작성해야 한다** (기존
READ_STATUS/READ_LOG 관련 코드는 전부 제거하고, 수신 루프를 push 이벤트
파싱 방식으로 바꿔야 함).
