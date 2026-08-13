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

## mqtt (선택 절, 2026-08-13 추가)

ResultPublisher(판정 결과 송신)와 SensorUplinkReceiver(센서 업링크 수신)가 붙는 mosquitto
브로커 설정. **절 자체를 생략할 수 있다** - 생략하면 `tls_enabled=false`, 평문
`localhost:1883`로 기존과 동일하게 동작한다(회귀 없음).

```json
{
  "forklift": {"marker_id": 0, "forklift_id": "FL_01", "terminal_id": "TERM_01"},
  "handover": {"confirm_frames": 3, "lost_grace_ms": 500},
  "mqtt": {
    "tls_enabled": false,
    "broker_host": "127.0.0.1",
    "broker_port": 1883,
    "ca_cert_path": "/home/veda3/mqtt-certs/ca.crt",
    "client_cert_path": "/home/veda3/mqtt-certs/client-server.crt",
    "client_key_path": "/home/veda3/mqtt-certs/client-server.key"
  }
}
```

| 필드 | 뜻 |
| --- | --- |
| `mqtt.tls_enabled` | true면 연결 전 `mosquitto_tls_set()`을 호출해 TLS/mTLS로 붙는다. 기본값 false(평문) |
| `mqtt.broker_host` | 브로커 주소. 기본값 `127.0.0.1`(IPv4 리터럴 - `localhost`는 이 환경에서 IPv6(`::1`)로 먼저 해석될 수 있는데, 1883 리스너가 IPv4 전용이라 SensorUplinkReceiver 쪽 연결이 거부될 수 있음. 실측 확인된 문제라 리터럴 IP를 기본값으로 씀) |
| `mqtt.broker_port` | 생략(또는 0)하면 `tls_enabled`에 따라 1883(평문)/8883(TLS)을 자동으로 채운다 |
| `mqtt.ca_cert_path` | 브로커 인증서를 검증할 CA 인증서 경로. `tls_enabled=false`면 무시됨 |
| `mqtt.client_cert_path` | 이 서버 프로세스의 mTLS 클라이언트 인증서 경로 |
| `mqtt.client_key_path` | 위 인증서의 개인키 경로 |

- 인증서 기본 경로(`/home/veda3/mqtt-certs/`)는 2026-08-11 veda3에 자체 CA로 발급해 둔
  파일들이다(`ca.crt`/`ca.key`, `server.crt`/`server.key`는 브로커용, `client-server.*`는
  이 서버 프로세스용, `client-term01.*`는 단말(forklift-device)용). CLI 레벨(mosquitto_pub/sub,
  `openssl s_client -connect ... -showcerts`)로는 검증 완료된 인증서다.
- `tls_enabled=true`로 실제 켜서 mTLS 연결 자체를 검증하는 것은 아직 안 했다(다음 단계).
