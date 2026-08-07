# config

카메라·채널 정보, 채널별 호모그래피 행렬, 동기화 값과 위험 임계값 설정을 둔다.
현장 확정 전 보정값은 샘플임을 명시한다.

## terminal_<TERMINAL_ID>.json

단말(운전석) 1대당 파일 1개. 로더는 `src/config/terminal_config.*`이고, 서버는 시작할 때
두 번째 인자로 받은 경로(없으면 `terminal_TERM_01.json`)를 읽는다. 읽기에 실패하면
기본값으로 때우지 않고 그 자리에서 종료한다(종료코드 2).

```json
{
  "forklift": {"marker_id": 0, "forklift_id": "FL_01", "terminal_id": "TERM_01"},
  "handover": {"confirm_frames": 3, "lost_grace_ms": 500}
}
```

| 필드 | 뜻 |
| --- | --- |
| `forklift.marker_id` | 이 지게차에 붙은 ArUco 마커 ID. 이 마커가 보이는 카메라 = 지게차가 있는 카메라 |
| `forklift.forklift_id` | 지게차 식별자. 현재는 로그/디버깅용이고 하류 JSON 계약에는 없다 |
| `forklift.terminal_id` | 운전석 단말 식별자. 9000 판정 결과와 9001 camera_assignment가 이 값으로 단말을 지목한다 |
| `handover.confirm_frames` | 카메라 전환 후보로 인정하기까지 필요한 연속 검출 프레임 수 (1 이상) |
| `handover.lost_grace_ms` | 액티브 카메라에서 마커가 안 보여도 유지해주는 시간(ms) |

- 단말을 추가할 때는 `terminal_TERM_02.json` 식으로 파일을 늘리고 서버를 그 경로로 띄운다.
  파일명 규칙은 `terminalConfigFileName()` 한 곳에 있다.
- `confirm_frames` / `lost_grace_ms`는 현장 실측 전 잠정값이다. 낮추면 전환이 빨라지는
  대신 오검출 한두 프레임에 운전석 화면이 튄다. 판정 동작은
  `tests/test_marker_channel_tracker.cpp`에 케이스별로 고정해 뒀다.
- 필수 키 누락 / 타입 불일치 / 범위 위반(`marker_id < 0`, `confirm_frames < 1`,
  `lost_grace_ms < 0`)은 전부 로드 단계에서 걸린다.
