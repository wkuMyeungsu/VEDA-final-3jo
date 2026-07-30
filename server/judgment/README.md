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
| `ResultPublisher.h` | 헤더 온리 TCP 송신기 (POSIX 소켓 + `std::thread`) |
| `test_exception_trigger.cpp` | 예외처리 트리거 검증 테스트 (헤더만 include, 엔진 구현은 링크) |
| `test_judgment_pipeline.cpp` | 배선 glue 검증 테스트 (매핑 규칙 + `processFrame()` end-to-end + 상류 구현체 통합) |
| `test_result_publisher.cpp` | 송신 큐 스트레스 테스트 |

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

`ResultPublisher.h`는 POSIX 소켓 + `std::thread` 기반이라 Windows/MSVC 네이티브 빌드는 지원하지 않음 (RPi4/Linux 전용).

## 출력 JSON (단말 전송, TCP 개행 구분)

```json
{
  "utc_time": "2026-07-29T05:32:33.346Z",
  "camera_id": null,
  "zone": null,
  "exception_state": "NONE",
  "distance_m": 14.14,
  "risk_level": "DANGER"
}
```

| 필드 | 타입 | 비고 |
|---|---|---|
| `utc_time` | string | ISO8601 + 밀리초 |
| `camera_id` | string \| null | `judgment_pipeline`의 활성 camera_id를 `std::to_string()`으로 변환, 미확정(음수)이면 null |
| `zone` | string \| null | 김진석 zone↔camera_id 매핑 대기, 값 없으면 null |
| `exception_state` | string | `NONE` \| `SENSOR_FAULT` \| `DEAD_RECKONING` \| `EMERGENCY_IMPACT` \| `UNCONFIRMED_PROXIMITY` |
| `distance_m` | number \| null | 판정 불가 상태(폐색/미검출)면 null |
| `risk_level` | string | `SAFE` \| `CAUTION` \| `DANGER` |

bbox·world 좌표(person/forklift)는 팀 협의로 제외 확정 (2026-07-29) — Qt가 지게차 좌표를 직접 쓰지 않고, 서버가 계산한 거리·위험도만 사용하는 구조로 정리됨.

## 미해결 (이정석 확인 대기)

- **포트**: 임시 9000 (`main()` 하드코딩), 실제 배포 포트 확정 필요
- **프레이밍 방식**: 임시 개행(`\n`) 구분, Qt 수신부가 길이-prefix 방식을 원할 수 있음
- **전송 주기**: 현재 값 변화 시 즉시 송신만 지원, 주기적 heartbeat 필요 여부 미정
- ~~**ResultPublisher 드랍 이슈**~~: **해결됨 (2026-07-29)** — 단일 슬롯을 최대 100건 FIFO 큐로 교체. 9개 시나리오 전부 전송 확인(기존 3/9 → 9/9). 큐 초과 시에만 가장 오래된 항목부터 드랍하며 `droppedCount()`로 추적 가능. 검증: `test_result_publisher.cpp`
- **드랍 로그 한계**: 잔여 드랍은 `droppedCount()`로 프로세스가 살아있는 동안만 정확히 조회 가능하며, 비정상 종료(SIGKILL 등)로 `stop()`/소멸자가 안 불리면 로그에 안 남을 수 있음
- ~~**camera_id 통합 지점 부재**~~: **배선됨 (2026-07-30)** — `judgment_pipeline.h/.cpp`가 `NearestPersonResult` → `CameraInput` 매핑과 `evaluate()` 호출을 담당. 단, 아래 세 항목이 남아 있음
- **핸드오버 구간 미처리**: `judgment_pipeline`은 활성 카메라 1대만 처리함. 여러 `camera_id`가 같은 지게차/사람을 동시에 보는 구간은 카메라별 판정 후 worst-case 채택 방식으로 구현 예정 (`judgment_pipeline.h`의 `[TODO]`). 현재는 다른 카메라에서 온 최근접 사람이 들어오면 `PipelineOutput.camera_id_mismatch`로만 표시하고 판정은 그대로 진행함 (사람을 드랍하는 게 더 위험하므로)
- **IMU/ToF 스텁**: 드라이버 연동 전이라 `StubSensorReader`가 "센서 정상 + ToF 원거리(5m)" 고정값을 돌려줌 → 현재 파이프라인 경로에서는 ToF 근접 경보·충돌 감지가 동작하지 않음. 교체를 잊고 넘어가지 않도록 첫 `read()` 호출 시 `std::cerr`로 1회 경고를 남김. `ISensorReader`만 구현해 교체하면 됨 (**배포 전 필수 교체**)
- ~~**selector 헤더 부재**~~: **해결됨 (2026-07-30)** — `nearest_person_selector`를 헤더/구현/`main()`으로 분리하고 정적 라이브러리로 묶었다. `judgment_pipeline.h`의 미러 정의는 삭제되고 `#include "nearest_person_selector.h"`로 교체됨 → 손으로 동기화할 필요 없고, 테스트도 실제 구현체와 링크된다
- **`cross_camera_reid.cpp` 미통합**: 그 파일은 `Track`(필드 구성이 selector 쪽보다 많음)과 전역 `euclideanDistance()`를 자체 정의하고 있어, 지금 상태로 같은 실행파일에 링크하면 ODR 위반 + 중복 정의가 된다. 어떤 CMake 타깃에도 안 들어가 있어 당장 문제는 없지만, 빌드에 넣을 때 `nearest_person_selector.h` 쪽 정의로 통일해야 함

## 참고

전체 실험 설계·인터페이스 명세: Confluence (pageId 13139978)
