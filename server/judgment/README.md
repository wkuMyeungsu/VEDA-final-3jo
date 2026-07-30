# server/judgment

지게차-사람 위험 판정 엔진 + 단말(Qt) 결과 송신

## 파일 구성

| 파일 | 내용 |
|---|---|
| `danger_judgment_engine.h` | 데이터 구조·enum·`DangerJudgmentEngine` 선언 + 출력/직렬화 헬퍼 선언 (표준 헤더만 의존) |
| `danger_judgment_engine.cpp` | 엔진 구현 + 헬퍼 구현 (`nowIso8601Ms()`가 `gmtime_r`을 쓰므로 POSIX 전용) |
| `danger_judgment_engine_main.cpp` | 실행파일 `danger_engine`의 `main()` — 더미 시나리오 9종 + TCP 송신 |
| `ResultPublisher.h` | 헤더 온리 TCP 송신기 (POSIX 소켓 + `std::thread`) |
| `test_exception_trigger.cpp` | 예외처리 트리거 검증 테스트 (헤더만 include, 엔진 구현은 링크) |
| `test_result_publisher.cpp` | 송신 큐 스트레스 테스트 |

## 빌드

```bash
g++ -std=c++17 danger_judgment_engine.cpp danger_judgment_engine_main.cpp \
    -o danger_engine -pthread
```

또는 (테스트 실행파일까지 함께 빌드):

```bash
cmake -S . -B build && cmake --build build
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
| `camera_id` | string \| null | 상류(nearest_person_selector) 미배선, 값 없으면 null |
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
- **camera_id 통합 지점 부재**: `nearest_person_selector.cpp`가 `int camera_id`를 노출하긴 하나, 이 파일의 `CameraInput`까지 실제로 이어붙이는 호출부가 아직 리포에 없음 (두 파일 모두 독립 `main()`으로만 테스트 중)

## 참고

전체 실험 설계·인터페이스 명세: Confluence (pageId 13139978)
