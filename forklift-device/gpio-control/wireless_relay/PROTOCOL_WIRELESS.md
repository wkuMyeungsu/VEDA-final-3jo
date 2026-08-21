# 무선 릴레이 링크 — FPGA → 릴레이 (NRF24L01 + NUCLEO-F401RE ×2)

기존 `src/PROTOCOL.md` §3의 전진 차단 릴레이는 FPGA GPIO(`fwd_cutoff_relay_en`,
핀42)가 트랜지스터를 직결 구동했다. 이 문서는 그 배선을 대신하는 무선 구간
하나만 다룬다 — **FPGA/Verilog 쪽은 수정하지 않는다.** `uart_tx_o`(핀41)는
이미 Pi가 받고 있는 브로드캐스트 라인이라, 새 STM32는 여기에 수신 전용으로
탭만 한다 (한 드라이버에 리시버 두 개, 전기적으로 안전).

---

## 0. 왜 새 프로토콜을 안 만들었나

TX노드/RX노드는 FPGA가 이미 만들어 보내는 5바이트 push 프레임(`PROTOCOL.md`
§2.1: 200ms heartbeat + XOR checksum + `movement_cutoff_active` 비트가 매
프레임에 스냅샷됨)을 **그대로** 무선으로 중계한다. 새로 발명한 부분은 없고,
이미 검증된 heartbeat/checksum을 재사용해서 "링크가 살아있는가"까지 덤으로
얻는다.

## 1. 아키텍처

```
FPGA(uart_tx_o, 핀41, 3.3V) ──(탭)──▶ TX노드 USART1_RX(PA10/D2)
                                          │ 헤더+체크섬 검증
                                          ▼
                                    NRF24L01 (PTX)
                                    ‥‥‥ 2.4GHz ‥‥‥
                                    NRF24L01 (PRX)
                                          │ 체크섬 재검증 + 워치독
                                          ▼
                                    RX노드 GPIO(PB10/D6)
                                          │
                                          ▼
                              기존 트랜지스터 베이스 저항
                              (PROTOCOL.md §3.1 회로 그대로,
                               구동원만 FPGA→이 보드로 교체)
```

## 2. 안전 정책 (확정)

| 상황 | RX노드 동작 |
|---|---|
| 정상 수신 (600ms 이내 유효 프레임) | 프레임의 `movement_cutoff_active` 비트를 그대로 미러링 |
| 무선 링크 끊김 (600ms 이상 무수신) | **비트값과 무관하게 강제 컷오프** |
| 부팅 직후, 첫 프레임 수신 전 | 링크 끊김과 동일 취급 — 컷오프로 시작 |
| RX노드 보드 자체가 무전원 | 회로의 베이스 풀다운 저항(기존 §3.1, 그대로 유지)이 릴레이를 비여자 상태로 유지 → 전진 허용 (물리적 페일세이프, 이번 변경과 무관) |

**600ms = FPGA heartbeat 주기(200ms)의 3배** — `PROTOCOL.md` §5에서 Pi 쪽에
권장한 마진(600ms = heartbeat 3주기)과 동일한 비율을 그대로 가져왔다.

이 정책은 `PROTOCOL.md` §3.2의 COMM_ERROR 선례(Pi↔FPGA 통신 두절만으로는
컷오프 트리거 안 함)와 **의도적으로 다르다.** COMM_ERROR는 진단 정보성
링크였지만, 이 무선 구간은 실제 차단 액추에이터를 구동하기 때문에 애매한
상황에서는 안전 쪽으로 수렴하도록 사용자가 명시적으로 결정했다 (RF 간섭으로
인한 불필요한 정지는 감수하되, 진짜 CRITICAL/ESTOP 순간에 링크가 끊겨서
컷오프가 아예 전달 안 되는 상황은 원천 차단).

## 3. 핀 배정 (두 보드 동일)

**주의**: 아래는 커뮤니티 문서(NuttX 보드 문서, components101) 교차 확인
값이다. 실제 배선 전에 STM32CubeMX 핀뷰 또는 보드 실크스크린으로 반드시
재확인할 것 — 이전 Tang Nano 좌/우 헤더 문제처럼 문서와 실물이 다를 수 있다.

| 용도 | Arduino핀 | MCU핀 | 비고 |
|---|---|---|---|
| SPI1 SCK | D13 | PA5 | 온보드 LED(LD2)와 공유 — SPI 중 깜빡이는 건 정상 |
| SPI1 MISO | D12 | PA6 | |
| SPI1 MOSI | D11 | PA7 | |
| NRF24L01 CSN | D10 | PB6 | |
| NRF24L01 CE | D9 | PC7 | |
| (TX노드만) USART1_RX | D2 | PA10 | FPGA 핀41 수신 전용, GND 공통 필수 |
| (RX노드만) 릴레이 인에이블 출력 | D6 | PB10 | 기존 트랜지스터 베이스 저항으로 |

USART2(D0/D1)는 ST-Link 가상COM과 공유라 비워둠 (디버그 로그용으로 남겨둘
것을 권장).

## 4. NRF24L01 무선 설정

- 정적 페이로드 5바이트, 파이프0 하나만 사용
- Enhanced ShockBurst 오토ACK + 오토재전송 (ARD=750us, ARC=15회) — ACK
  실패는 TX노드 쪽 진단용일 뿐, 안전 판단은 전적으로 RX노드 워치독이 담당
- 250kbps (속도보다 사거리/안정성 우선 — 5바이트 페이로드라 대역폭은 전혀
  병목 아님)
- 채널 76 (2.476GHz, Wi-Fi 1~11채널 위쪽) — 현장에서 링크가 자꾸 끊기면
  `common/wireless_link_config.h`의 `WLINK_CHANNEL`부터 바꿔볼 것
- 주소(양쪽 동일 필수): `52 4C 59 30 31` ("RLY01")

## 5. 빌드 환경

보드 2대 각각 별도 프로젝트로 만든다 (tx_node, rx_node). 아래 두 방법 중
하나를 쓰면 된다 — 둘 다 HAL/CMSIS 보일러플레이트를 CubeMX가 생성해준다는
점은 같고, 그 결과를 여는 IDE만 다르다.

### 5-1. STM32CubeIDE

1. STM32CubeIDE → New STM32 Project → Board Selector에서 `NUCLEO-F401RE`
   선택
2. Pinout & Configuration 화면에서:
   - `SPI1` → Mode: **Full-Duplex Master**
   - (TX노드만) `USART1` → Mode: **Asynchronous**
   - GPIO/클럭은 손대지 않는다 — 아래 4번에서 통째로 덮어씀
3. Generate Code (Mode만 켜두는 이유: `stm32f4xx_hal_conf.h`의
   `HAL_SPI_MODULE_ENABLED`/`HAL_UART_MODULE_ENABLED` 매크로와,
   `stm32f4xx_it.c`의 `USART1_IRQHandler` 스텁이 자동으로 생기게 하기
   위함 — 실제 GPIO/SPI/USART 초기화는 이 저장소가 주는 `main.c`가 직접
   다시 하므로 CubeMX GUI에서의 세부 핀 설정은 무의미하다)
4. 생성된 `Core/Src/main.c`를 이 저장소의 `tx_node/Core/Src/main.c` (또는
   `rx_node/...`) 내용으로 통째로 교체
5. `common/nrf24l01.c`, `common/fpga_frame.c`를 프로젝트의 `Core/Src`에,
   `common/nrf24l01.h`, `common/fpga_frame.h`, `common/wireless_link_config.h`를
   `Core/Inc`에 복사
6. Build → Nucleo에 플래시

### 5-2. Keil uVision5 (MDK-ARM)

Keil은 HAL 소스를 자동 생성해주지 않으므로, **CubeMX로 HAL/CMSIS +
`.uvprojx` 골격만 만들고 main.c는 여기 파일로 교체**하는 방식이 가장
안정적이다 (Keil 안에서 개별 컴포넌트를 손으로 골라 조립하는 것보다 훨씬
덜 번거로움).

**사전 준비**
- Keil MDK uVision5 설치
- Pack Installer(요술봉 아이콘 또는 Project → Manage → Pack Installer)에서
  `Keil::STM32F4xx_DFP` 설치 — 이게 없으면 디바이스 선택 자체가 안 됨
- STM32CubeMX 설치 (ST 무료 배포) — HAL 골격 생성용으로만 씀
- Nucleo 보드는 온보드 ST-Link/V2-1이 있어서 USB 케이블 하나로 플래시/
  디버그가 다 됨. Keil이 ST-Link를 못 찾으면 ST 홈페이지의 ST-Link
  드라이버(STSW-LINK009 등)를 따로 설치할 것

**절차**

1. STM32CubeMX에서 5-1과 동일하게 진행 (Board Selector `NUCLEO-F401RE`
   → SPI1 Full-Duplex Master, TX노드만 USART1 Asynchronous 활성화)
2. Project Manager 탭:
   - Project Name: `tx_node` / `rx_node`
   - **Toolchain / IDE: `MDK-ARM V5`** ← 여기가 CubeIDE 절차와 다른
     유일한 지점
3. GENERATE CODE → `MDK-ARM/<프로젝트명>.uvprojx`가 생성됨
4. `.uvprojx`를 더블클릭해서 Keil uVision5로 열기
5. Project 창에서 `Application/User/Core` 그룹의 `main.c`를 더블클릭 →
   전체 선택 후 삭제 → 이 저장소의 `tx_node/Core/Src/main.c` (또는
   `rx_node/...`) 내용을 그대로 붙여넣기
6. `common/nrf24l01.c`, `common/fpga_frame.c` 추가: Project 창에서
   `Application/User/Core` 그룹 우클릭 → **Add Existing Files to Group...**
   → 두 파일 선택
7. 헤더 3개(`nrf24l01.h`, `fpga_frame.h`, `wireless_link_config.h`)는
   프로젝트의 `Core/Inc` 폴더에 파일 탐색기로 직접 복사 (별도 include
   경로 설정 없이 바로 인식됨 — CubeMX가 이미 `Core/Inc`를 include
   경로에 넣어뒀기 때문)
8. **C 표준 확인** (중요, 안 하면 컴파일 에러 남): Project → Options for
   Target (Alt+F7) → C/C++(AC6) 탭 → *Language C* 드롭다운이 `c99` 이상인지
   확인. main.c에서 `for (uint8_t i = 0; ...)` 처럼 for문 안에서 변수를
   선언하는 C99 문법을 쓰기 때문에 C90으로 잡혀 있으면 빌드가 깨진다.
   (Keil MDK v5는 기본 컴파일러가 Arm Compiler 6이라 보통 문제없지만,
   구버전 프로젝트를 베이스로 했다면 한 번 확인)
9. Build → **Project → Build Target (F7)**. Build Output 창에서
   `0 Error(s)` 확인
10. 플래시: Options for Target → Debug 탭에서 **ST-Link Debugger** 선택 →
    Settings에서 SW(SWD) 모드 확인 → 확인 후 **Flash → Download (F8)**
    (또는 툴바의 Load 아이콘). Options for Target → Utilities 탭에
    Flash 알고리즘이 자동으로 `STM32F4xx 512K Flash`로 잡혀 있는지도
    확인
11. 실시간 디버깅이 필요하면 **Debug → Start/Stop Debug Session
    (Ctrl+F5)** — UART 프레임 파싱이나 NRF24L01 SPI 트랜잭션을 브레이크
    포인트/워치 창으로 들여다볼 때 §6 벤치 검증 단계에서 특히 유용하다

**자주 걸리는 문제**
- "Device not found" / 디바이스 목록에 F401RE가 없음 → `STM32F4xx_DFP`
  팩 미설치
- ST-Link가 Debug 드롭다운에 안 뜸 → ST-Link 드라이버 미설치, 또는
  Nucleo의 ST-Link가 구펌웨어라 인식 안 되는 경우 → ST-Link Utility로
  펌웨어 업데이트
- `for` 루프 초기 선언 관련 컴파일 에러 → 8번의 C99 설정 확인

## 6. 검증 절차 (실제 릴레이에 연결하기 전, 반드시 이 순서로)

1. **TX노드 단독**: FPGA `uart_tx_o`(핀41) + GND을 TX노드 D2/GND에 연결.
   기존 Pi 링크는 그대로 두고, TX노드가 프레임을 정상 파싱하는지(로직
   analyzer나 임시 LED 토글로) 확인. 이 시점에 기존 Pi↔FPGA 통신이 영향
   받지 않는지도 같이 확인.
2. **NRF24L01 링크 단독**: FPGA 없이 책상 위에서 두 보드끼리 직접 테스트.
   RX노드의 릴레이 GPIO(우선 LED로 대체)가 TX노드가 보낸 비트를 정확히
   미러링하는지 확인.
3. **워치독 동작**: TX노드 전원을 끄거나 안테나를 멀리 떨어뜨려 일부러
   링크를 끊고, 600ms 근처에서 RX노드가 확실히 컷오프로 전환되는지
   스톱워치로 확인. 재연결 시 정상 상태로 복귀하는지도 확인.
4. **통합**: 위 3단계를 전부 통과한 뒤에만 기존 트랜지스터/릴레이 회로에
   연결한다 — 지게차 전진 차단용 안전 시스템이므로 벤치 검증을 건너뛰지
   않는다.

## 7. 파일 구성

```
wireless_relay/
├── PROTOCOL_WIRELESS.md          (이 문서)
├── common/
│   ├── nrf24l01.h / .c           레지스터 레벨 최소 드라이버
│   ├── fpga_frame.h / .c         5바이트 프레임 검증 (PROTOCOL.md §2.1과 동일 레이아웃)
│   └── wireless_link_config.h    양쪽 공통 상수 (주소/채널/워치독 타임아웃)
├── tx_node/Core/Src/main.c       FPGA UART 탭 → NRF24L01 송신
└── rx_node/Core/Src/main.c       NRF24L01 수신 → 릴레이 GPIO + 워치독
```

`common/` 파일들은 두 프로젝트 각각에 복사해서 쓴다 (CubeIDE 프로젝트가
서로 독립적이라 공유 라이브러리 설정보다 이게 더 단순함). 원본은
`common/`에 유지하고, 수정 시 두 프로젝트 모두 다시 복사할 것.
