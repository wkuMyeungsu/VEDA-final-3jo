# server/judgment

지게차-사람 위험 판정 엔진 + 단말(Qt) 결과 송신

## 파일 구성

| 파일 | 내용 |
|---|---|
| `danger_judgment_engine.h` | 데이터 구조·enum·`DangerJudgmentEngine` 선언 + 출력/직렬화 헬퍼 선언 (표준 헤더만 의존) |
| `danger_judgment_engine.cpp` | 엔진 구현 + 헬퍼 구현 (`nowIso8601Ms()`가 `gmtime_r`을 쓰므로 POSIX 전용) |
| `danger_judgment_engine_main.cpp` | 실행파일 `danger_engine`의 `main()` — 더미 시나리오 9종 + TCP 송신 |
| `judgment_pipeline.h` | 상류(`nearest_person_selector`) 결과 → `CameraInput` 배선 glue 선언 + `ISensorReader` 인터페이스 |
| `judgment_pipeline.cpp` | glue 구현 (매핑 + `evaluate()` 호출만, 판정 로직 없음) + IMU/ToF 스텁 리더 |
| `ResultPublisher.h` | 헤더 온리 TCP 송신기 — 서버(listen/accept) (POSIX 소켓 + `std::thread`) |
| `ResultDispatcher.h` | 헤더 온리 전송 정책 — 변화 시 즉시 + 200ms 하트비트 (소켓 의존성 없음) |
| `test_exception_trigger.cpp` | 예외처리 트리거 검증 테스트 (헤더만 include, 엔진 구현은 링크) |
| `test_judgment_pipeline.cpp` | 배선 glue 검증 테스트 (매핑 규칙 + `processFrame()` end-to-end + 상류 구현체 통합) |
| `test_result_publisher.cpp` | 송신 큐 스트레스 + 서버 재대기 + 전송 정책 테스트 |

이 프로젝트의 CMake는 `add_subdirectory`로 `../tracking`(`nearest_person_selector` 라이브러리)을 함께 가져온다. `judgment_pipeline`이 상류 결과 타입(`NearestPersonResult`)을 그쪽 헤더에서 직접 include하기 때문이다.

`../tracking/nearest_person_selector.h`는 `WorldPoint`를 자체 정의하지 않고 `danger_judgment_engine.h` 쪽 정의를 재사용한다. 두 헤더를 같은 TU에서 include하는 `judgment_pipeline`에서 같은 이름의 구조체가 두 번 정의되는 걸 피하기 위한 것 (원래도 두 정의의 필드 구성은 동일했음). 계획대로 `server/common/types.hpp`가 생기면 `WorldPoint`를 거기로 옮기고 두 헤더가 모두 그걸 include하는 형태로 정리하면 된다.

## 빌드

```bash
g++ -std=c++17 danger_judgment_engine.cpp danger_judgment_engine_main.cpp \
    -o danger_engine -pthread
```

또는 (테스트 실행파일까지 함께 빌드):

```bash
cmake -S . -B build && cmake --build build
```

배선 glue와 그 테스트만 빌드·실행할 때 (`ResultPublisher` 미포함이라 소켓 의존성 없음):

```bash
cmake -S . -B build && cmake --build build --target test_judgment_pipeline
./build/test_judgment_pipeline     # 종료코드 0=성공
```

송신기·전송 정책 테스트만 빌드·실행할 때 (루프백 TCP를 쓰므로 POSIX 필요):

```bash
cmake -S . -B build && cmake --build build --target test_result_publisher
./build/test_result_publisher      # 종료코드 0=성공
```

`ResultPublisher.h`는 POSIX 소켓 + `std::thread` 기반이라 Windows/MSVC 네이티브 빌드는 지원하지 않음 (RPi4/Linux 전용). `ResultDispatcher.h`는 표준 라이브러리만 쓰므로 소켓 의존성이 없다.

## 출력 JSON (단말 전송, TCP 개행 구분)

```json
{
  "utc_time": "2026-07-29T05:32:33.346Z",
  "camera_id": null,
  "zone": null,
  "exception_state": "NONE",
  "distance_m": 14.14,
  "risk_level": 2
}
```

| 필드 | 타입 | 비고 |
|---|---|---|
| `utc_time` | string | ISO8601 + 밀리초 |
| `camera_id` | string \| null | `judgment_pipeline`의 활성 camera_id를 `std::to_string()`으로 변환, 미확정(음수)이면 null |
| `zone` | string \| null | 김진석 zone↔camera_id 매핑 대기, 값 없으면 null |
| `exception_state` | string | `NONE` \| `SENSOR_FAULT` \| `DEAD_RECKONING` \| `EMERGENCY_IMPACT` \| `UNCONFIRMED_PROXIMITY` |
| `distance_m` | number \| null | 판정 불가 상태(폐색/미검출)면 null |
| `risk_level` | number (int) | `0`=SAFE \| `1`=CAUTION \| `2`=DANGER \| `3`=EMERGENCY — 단말이 `toInt()`로 읽으므로 문자열 금지 |

bbox·world 좌표(person/forklift)는 팀 협의로 제외 확정 (2026-07-29) — Qt가 지게차 좌표를 직접 쓰지 않고, 서버가 계산한 거리·위험도만 사용하는 구조로 정리됨.

## 위험 단계 4단계 (2026-08-03 확장)

`RiskLevel`에 `EMERGENCY`(3)를 추가했다. FPGA(`gpio-control/PROTOCOL.md`)와 단말 Qt(`RiskTypes::RiskLevel`)는 이미 `risk_level` 0~3을 전제로 설계돼 있었는데 서버 enum이 3단계뿐이라 3에 도달할 방법이 없었다. 이름은 Qt 쪽(`Emergency`)을 따랐고, FPGA 문서는 같은 값 3을 `CRITICAL`로 부른다 (전진 차단 릴레이가 이 값에서 걸린다).

| 단계 | 값 | 카메라 기준 거리 조건 |
|---|---|---|
| SAFE | 0 | > 3.0m |
| CAUTION | 1 | ≤ `caution_threshold_m` (3.0m) |
| DANGER | 2 | ≤ `danger_threshold_m` (1.5m) |
| EMERGENCY | 3 | ≤ `emergency_threshold_m` (**목업 0.4m**) — 충돌 임박 |

> ⚠️ `emergency_threshold_m = 0.4`와 `emergency_release_margin_m = 0.1`은 **근거 있는 실측값이 아니라 자리표시용 목업 상수**다. 좌표정합 PoC + 제동거리 실측(확정된 실험 4종)이 끝나면 교체해야 한다. 교체 시 `emergency_threshold_m`은 `danger_threshold_m`(1.5)보다 작게 유지해야 한다 — 같거나 크면 EMERGENCY가 DANGER 구간을 통째로 삼킨다.

### DANGER ↔ EMERGENCY 히스테리시스

- **진입**: 거리 ≤ 0.4m
- **해제**: 거리 > 0.4 + 0.1 = 0.5m
- 사이 구간(0.4~0.5m)은 직전에 EMERGENCY였는지에 따라 갈린다.

위험도 상승은 지연 없이 즉시, 하강만 신중하게 — FPGA `warning_latch`(PROTOCOL.md 1.1절)가 시간 축에서 쓰는 원칙을 거리 축에 적용한 것이다. 임계값 근처에서 좌표가 미세하게 흔들릴 때 3↔2가 프레임마다 진동하면 그대로 LED/부저/전진차단으로 나가기 때문에 필요하다.

주의할 점 세 가지:

1. **다른 경계(SAFE↔CAUTION↔DANGER)에는 히스테리시스가 없다** — 예전부터 없었고 이번에도 넣지 않았다. 같은 방식을 넓히려면 `classifyByDistance()`의 진입/해제 구조를 경계별로 반복하면 되고, 그때는 마지막 단계 하나만 기억하는 `in_emergency_` 대신 직전 `RiskLevel` 전체를 들고 있어야 한다.
2. **거리 판정이 불가능한 프레임(마커 폐색/사람 미검출)에서는 래치가 풀린다.** 유지하는 쪽이 더 안전해 보이지만 한번 걸리면 사람이 다시 잡힐 때까지 풀 방법이 없어 EMERGENCY가 무기한 고착된다(전진 차단이 걸린 채로 남는다). 그 구간은 기존대로 예외 상태(`DEAD_RECKONING`/`SENSOR_FAULT`)의 최소 CAUTION 보정에 맡긴다.
3. **`evaluate()`가 프레임 간 상태를 갖게 됐다.** 시그니처는 `const`지만 래치를 `mutable`로 들고 있으므로, 같은 엔진 인스턴스를 여러 스레드에서 동시에 호출하면 안 된다. 판정 대상이 바뀔 때는 `resetHysteresis()`로 초기화한다.

### EMERGENCY(위험도)와 EMERGENCY_IMPACT(예외)는 별개 축

이름만 비슷하고 서로 다른 필드다.

| | 축 | 입력 | 나가는 필드 |
|---|---|---|---|
| `RiskLevel::EMERGENCY` | 위험도 | 카메라 기준 거리 | `risk_level: 3` |
| `ExceptionState::EMERGENCY_IMPACT` | 예외 상태 | IMU 급가속도 | `exception_state: "EMERGENCY_IMPACT"` |

`EMERGENCY_IMPACT` 보정은 예전엔 `final_risk = DANGER`로 못박았는데, 이 대입은 EMERGENCY(3)가 생긴 뒤로 "카메라상 충돌 임박인데 충돌까지 의심되는" 최악 상황의 위험도를 오히려 3 → 2로 **낮춘다**. 위험도는 worst-case로만 움직여야 하므로 `atLeast(fused, DANGER)`로 바꿨다. 카메라가 EMERGENCY가 아닐 때의 동작은 예전과 동일하고, 예외 태깅 자체는 건드리지 않았다. 검증: `test_exception_trigger.cpp` 테스트 3

ToF 경로(`classifyByTof`)는 **아직 3단계 그대로**다. ToF는 사람/벽/적재물을 구분하지 못해(`UNCONFIRMED_PROXIMITY`) 단독으로 EMERGENCY까지 올리는 게 타당한지 미확정이라 이번 확장은 카메라 거리 경로에만 적용했다.

## 송신 구조 (2026-08-03 확정)

### 연결 방향: 서버가 대기, 단말이 접속

단말이 현장에서 이동하며 IP가 바뀌기 때문에 서버가 단말 주소를 알 수 없다. → `ResultPublisher`가 고정 포트(임시 9000)에서 `bind`/`listen`하고, 단말이 `connect`해 온다 (기존 `connect()` 구조에서 전환).

- `ResultPublisher(bind_host, port)`의 첫 인자는 **접속할 상대 주소가 아니라 대기할 로컬 주소**다. `""`/`"0.0.0.0"`이면 모든 인터페이스.
- 단말 미접속 구간에도 서버는 `LISTENING` 상태로 살아 있고, 그 사이의 판정 결과는 큐(최대 100건)에 쌓였다가 접속 시점에 전송된다.
- 단말이 끊으면(전송 실패 또는 무전송 구간의 EOF 감지) 연결만 닫고 곧바로 재대기(re-listen)로 돌아간다. `bind` 실패(포트 점유 등)도 프로세스를 죽이지 않고 1초 간격으로 재시도한다.
- 동시 접속 단말은 1대 전제. 연결 중 들어온 추가 접속은 accept하지 않고 백로그에 남는다.

링크 상태는 `DISCONNECTED`(대기 소켓 없음) / `LISTENING`(단말 대기) / `CONNECTED` 3가지다 (`CONNECTING`은 client 구조 전용이라 제거됨).

### 전송 주기: 하이브리드 (변화 즉시 + 200ms 재전송)

`ResultDispatcher`가 담당한다. "무조건 200ms 주기 전송"은 위험 상태 전이가 최대 200ms 밀려 **판정 지연 100ms 목표와 충돌**하므로 채택하지 않았다.

| 상황 | 동작 |
|---|---|
| 판정 상태가 직전과 다름 | 즉시 전송 + 200ms 타이머 리셋 |
| 200ms 동안 상태 변화 없음 | 마지막 판정 결과를 재전송 (하트비트 겸용) |

- "상태가 같다"의 기준은 `risk_level` / `exception_state` / `camera_id` / `zone` — 단말이 경보 동작을 결정할 때 쓰는 필드들이다. `distance_m`은 사람이 조금만 움직여도 매 프레임 흔들려서 포함하면 사실상 프레임마다 전송이 되므로 제외했다. 거리가 임계값을 넘으면 `risk_level`이 바뀌므로 **위험 전이는 그대로 즉시 전송**되고, 표시용 거리 값은 다음 하트비트에 최신 값이 실려 나간다.
- 재전송은 `ResultDispatcher` 내부 스레드가 하므로 판정 루프는 `submit()`에서 블로킹되지 않는다.
- 하트비트도 `toJson()`을 다시 호출하므로 `utc_time`은 전송 시점으로 갱신된다. 판정 내용은 그대로이고 시각만 새로 찍히므로 단말이 링크 생존과 "언제 기준 상태인지"를 함께 판단할 수 있다.

## 미해결 (이정석 확인 대기)

- **EMERGENCY 임계값이 목업**: `emergency_threshold_m = 0.4` / `emergency_release_margin_m = 0.1`은 자리표시용 상수다. 제동거리·좌표정합 오차 실측 후 교체 필요 (**배포 전 필수 교체**). 해제 마진은 좌표정합 오차보다 크게 잡아야 진동이 실제로 막힌다
- **ToF 경로 EMERGENCY 미적용**: ToF는 사람/벽/적재물 구분이 안 돼 단독으로 위험도 3까지 올리는 게 타당한지 미확정. 적용하려면 `tof_emergency_m`을 추가하고 `classifyByTof()`를 4단계로 넓혀야 함
- **다른 경계 히스테리시스 부재**: SAFE↔CAUTION↔DANGER 경계는 여전히 단순 임계값 비교라 경계 근처에서 단계가 진동할 수 있음. 필요하면 EMERGENCY와 같은 방식으로 확장 (위 "위험 단계 4단계" 절 참고)
- **포트**: 임시 9000 (`main()` 하드코딩), 실제 배포 포트 확정 필요 (연결 방향은 서버 listen으로 확정됨)
- **프레이밍 방식**: 임시 개행(`\n`) 구분, Qt 수신부가 길이-prefix 방식을 원할 수 있음
- ~~**전송 주기**~~: **확정됨 (2026-08-03)** — 하이브리드(변화 시 즉시 + 무변화 시 200ms 재전송). 위 "송신 구조" 참고. 검증: `test_result_publisher.cpp` 테스트 7~9
- ~~**연결 방향**~~: **확정됨 (2026-08-03)** — 단말 IP 가변으로 서버가 `listen`, 단말이 `connect`. 재대기·재접속 검증: `test_result_publisher.cpp` 테스트 5~6
- **변화 감지 기준 재확인 필요**: 상태 비교에서 `distance_m`을 제외했다(위 "송신 구조" 참고). 단말이 거리 값을 부드럽게 표시해야 한다면 200ms 갱신으로 충분한지 확인 필요 (김진석/이정석)
- **접속 직전 결과 재생**: 단말 미접속 구간에 쌓인 큐(최대 100건)가 접속 직후 한꺼번에 나간다. 각 줄에 `utc_time`이 있어 단말이 오래된 줄을 구분할 수 있지만, 단말이 이를 어떻게 처리할지는 미정 (버릴지, 마지막 1건만 반영할지)
- ~~**ResultPublisher 드랍 이슈**~~: **해결됨 (2026-07-29)** — 단일 슬롯을 최대 100건 FIFO 큐로 교체. 9개 시나리오 전부 전송 확인(기존 3/9 → 9/9). 큐 초과 시에만 가장 오래된 항목부터 드랍하며 `droppedCount()`로 추적 가능. 검증: `test_result_publisher.cpp`
- **드랍 로그 한계**: 잔여 드랍은 `droppedCount()`로 프로세스가 살아있는 동안만 정확히 조회 가능하며, 비정상 종료(SIGKILL 등)로 `stop()`/소멸자가 안 불리면 로그에 안 남을 수 있음
- ~~**camera_id 통합 지점 부재**~~: **배선됨 (2026-07-30)** — `judgment_pipeline.h/.cpp`가 `NearestPersonResult` → `CameraInput` 매핑과 `evaluate()` 호출을 담당. 단, 아래 세 항목이 남아 있음
- **핸드오버 구간 미처리**: `judgment_pipeline`은 활성 카메라 1대만 처리함. 여러 `camera_id`가 같은 지게차/사람을 동시에 보는 구간은 카메라별 판정 후 worst-case 채택 방식으로 구현 예정 (`judgment_pipeline.h`의 `[TODO]`). 현재는 다른 카메라에서 온 최근접 사람이 들어오면 `PipelineOutput.camera_id_mismatch`로만 표시하고 판정은 그대로 진행함 (사람을 드랍하는 게 더 위험하므로)
- **IMU/ToF 스텁**: 드라이버 연동 전이라 `StubSensorReader`가 "센서 정상 + ToF 원거리(5m)" 고정값을 돌려줌 → 현재 파이프라인 경로에서는 ToF 근접 경보·충돌 감지가 동작하지 않음. 교체를 잊고 넘어가지 않도록 첫 `read()` 호출 시 `std::cerr`로 1회 경고를 남김. `ISensorReader`만 구현해 교체하면 됨 (**배포 전 필수 교체**)
- ~~**selector 헤더 부재**~~: **해결됨 (2026-07-30)** — `nearest_person_selector`를 헤더/구현/`main()`으로 분리하고 정적 라이브러리로 묶었다. `judgment_pipeline.h`의 미러 정의는 삭제되고 `#include "nearest_person_selector.h"`로 교체됨 → 손으로 동기화할 필요 없고, 테스트도 실제 구현체와 링크된다
- **`cross_camera_reid.cpp` 미통합**: 그 파일은 `Track`(필드 구성이 selector 쪽보다 많음)과 전역 `euclideanDistance()`를 자체 정의하고 있어, 지금 상태로 같은 실행파일에 링크하면 ODR 위반 + 중복 정의가 된다. 어떤 CMake 타깃에도 안 들어가 있어 당장 문제는 없지만, 빌드에 넣을 때 `nearest_person_selector.h` 쪽 정의로 통일해야 함

## 참고

전체 실험 설계·인터페이스 명세: Confluence (pageId 13139978)
