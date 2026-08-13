# 공통 안전 서버 설정

운영 서버는 단말별 설정을 만들지 않고 `safety_server_config.json` 하나를 공유한다.
`TERMINAL_ID`는 공통 안전 규칙이 아니므로 JSON에 넣지 않고 실행 인자로 받는다.

```text
forklift_safety_server <RTSP_URL> <TERMINAL_ID> [CONFIG_PATH]
```

`CONFIG_PATH`를 생략하면 실행 위치를 기준으로
`config/safety_server_config.json` 또는 `server/01_main/config/safety_server_config.json`을 찾는다.
필수 항목이 빠지거나 단위가 `mm`가 아니면 기본값으로 대체하지 않고 기동을 중단한다.

## 설정 구조

- `danger_judgment`: 주의·위험·비상 거리, ToF, IMU, 지게차 충돌 반경
- `forklift_detection`: 지게차를 식별할 ArUco 마커 ID
- `homography`: 채널별 H 파일과 보정 해상도
- `handover`: 카메라 전환 확정 프레임과 마커 유실 유예 시간
- `sensor`: Stub ToF 거리와 MQTT 센서 신선도 제한
- `network`: MQTT, 카메라 할당 서버, 판정 하트비트
- `stream`: RTSP 지연, appsink 버퍼, EOS·연결 타임아웃, 재시도 정책

모든 월드 좌표와 거리는 mm다. 코드에서 `*10` 또는 `/1000` 변환을 추가하지 않는다.

## 호모그래피 파일

`homography.files` 값은 이 설정 파일이 있는 디렉터리를 기준으로 해석한다.
예를 들어 `homography/homography_channel_1_mm.json`은
`server/01_main/config/homography/homography_channel_1_mm.json`을 가리킨다.

런타임 로더는 다음을 검증한다.

- `world_unit == "mm"`
- `H_pixel_to_world`가 유한한 숫자로 이루어진 3×3 행렬인지
- `image_size`가 공통 설정의 보정 해상도와 같은지

검증에 실패한 채널은 항등행렬이나 더미 좌표로 대체하지 않고 위치 미확정으로 처리한다.

## TLS/mTLS

기존 TLS 연결을 사용할 때는 `network`에 `tls_enabled`, `ca_cert_path`,
`client_cert_path`, `client_key_path`를 추가한다. `tls_enabled`가 없거나 `false`면
기존 평문 MQTT 연결을 유지한다.
