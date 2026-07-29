# 지게차 운전자 단말 (forklift-device/qt)

Qt 6 / QML 기반 운전자용 사용자 단말 프로그램(`operator_terminal`)입니다.
현재는 실제 카메라/서버가 없으므로 모든 영상과 위험 이벤트는 Mock 소스로
생성되며, `--demo` 옵션으로 데모 패널을 열어 위험 단계·연결 상태·카메라
전환 등을 즉석에서 시연할 수 있습니다.

실제 RTSP 카메라/중앙 서버/물리 경고 장치를 연동할 때 수정해야 할 위치는
[INTEGRATION.md](INTEGRATION.md)에 정리되어 있습니다.

## 프로젝트 구조

```
CMakeLists.txt / CMakePresets.json   최상위 빌드 설정
common/                              C++ 백엔드 + QML 테마/컴포넌트 (Safety.Common 모듈)
                                     operator-device/qt/common과는 독립된 사본
  models/     RiskMetadata, BBox, CameraInfo, 공용 enum(RiskTypes)
  video/      IVideoSource 및 Mock/LocalFile/Rtsp 구현, VideoStream, DetectionOverlay
  network/    IMetadataSource 및 Mock 구현, IWarningDevice 및 Noop 구현
  config/     ConfigLoader (JSON 설정 파싱)
  services/   MetadataService, ServerConnectionService, 각종 QML 모델, DemoController
apps/
  operator_terminal/  운전자 단말 실행 파일 (main.cpp, ActiveCameraController, OperatorDemoController)
qml/
  theme/              Theme.qml (색상·간격·타이포그래피 싱글톤)
  components/         공유 QML 컴포넌트
  operator_terminal/  운전자 단말 전용 QML
config/               cameras.json, terminal.json 샘플
tests/                QtTest 기반 단위 테스트
```

## 빌드 (Windows, Qt 6.11.0 MinGW)

이 저장소는 `C:/Qt/6.11.0/mingw_64` + `C:/Qt/Tools/mingw1310_64`(g++) +
`C:/Qt/Tools/Ninja`로 구성된 CMake 프리셋 `windows-mingw`를 포함합니다. 다른
경로에 Qt가 설치되어 있다면 `CMakePresets.json`의 경로를 맞게 수정하세요.

```powershell
cmake --preset windows-mingw
cmake --build --preset windows-mingw
ctest --preset windows-mingw --output-on-failure
```

빌드 산출물은 `build/windows-mingw/operator_terminal.exe`에 생성되며,
실행 파일 옆에 `config/` 디렉터리가 자동으로 복사됩니다(빌드 후 재실행 시
JSON을 직접 수정해도 반영됩니다).

Qt DLL을 찾을 수 있도록 실행 전 `C:/Qt/6.11.0/mingw_64/bin`을 PATH에
추가하세요:

```powershell
$env:PATH = "C:/Qt/6.11.0/mingw_64/bin;$env:PATH"
```

### Linux / Raspberry Pi

동일한 CMakeLists.txt를 사용합니다. Qt 6.5 이상(Core/Gui/Qml/Quick/
QuickControls2/Multimedia/Network)이 설치되어 있으면 됩니다.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

운전자 단말은 Raspberry Pi 4 전체화면 실행을 염두에 두고 작성되었습니다
(`OperatorWindow.qml`이 기동 시 `Window.FullScreen`으로 표시됩니다).

## 실행

```powershell
# 운전자 단말 (기본 카메라: config/terminal.json의 default_camera_id)
.\operator_terminal.exe --demo --camera CAM_01
```

옵션:

- `--demo` : 데모 패널을 `Ctrl+Shift+D`로 열 수 있게 합니다. 지정하지 않으면
  패널에 접근할 방법이 없습니다(단축키가 비활성 상태).
- `--config <dir>` : `cameras.json` / `terminal.json`을 읽어올 디렉터리를
  지정합니다(기본값: 실행 파일 옆 `config/`).
- `--camera <id>` : 시작 시 표시할 camera_id를 강제 지정합니다.

데모 패널에서 할 수 있는 것:

- 카메라별 SAFE/CAUTION/DANGER/EMERGENCY 강제 설정, 예외 상태 강제 설정
- Mock 이벤트 자동 재생 시작/정지
- 서버 연결/끊김, 카메라 연결/끊김 토글
- person/forklift bbox 위치 변경
- 표시 중인 camera_id 전환 — 핸드오버 상황 재현

## 설정 파일

`config/cameras.json`에 카메라를 추가하면(예: `CAM_05`) 재빌드 없이
`--camera` 옵션이나 서버발 핸드오버로 선택 가능한 카메라 목록에 추가됩니다.
`source_type`을 `"mock"` → `"rtsp"`로 바꾸고 `rtsp_url`을 채우면 실카메라로
전환됩니다(`RtspVideoSource` 구현 완료, 실카메라 검증됨). 자세한 절차는
[INTEGRATION.md](INTEGRATION.md) 참고.

## 테스트

`common/` 아래의 서비스 클래스는 UI/QML과 독립적으로 동작하도록 작성되어
있어 QtTest로 직접 테스트합니다 (`tests/`):

- `test_config_loader` — cameras.json 파싱, 잘못된 항목 스킵
- `test_mock_metadata_source` — 위험 단계 강제 설정/해제, override 동작
- `test_bbox_aspect_fit` — letterbox/pillarbox 좌표 변환

```powershell
ctest --preset windows-mingw --output-on-failure
```


## 향후 연동용 Claude Code 프롬프트

- RTSP 카메라 연동: **완료** (`RtspVideoSource`, 실카메라 검증됨). 아래 1번
  프롬프트는 완료된 작업의 기록으로 남겨둠.
- 중앙 서버 JSON 메타데이터 연동: 미완료. 아래 2번 프롬프트로 진행.

> 작업 전에는 반드시 새 Git 브랜치를 만들고, 기존 Mock 구현은 삭제하지 않은
> 상태에서 실제 구현을 추가하세요.

<details>
<summary><strong>1. 실제 카메라 준비 후 RTSP 연동 프롬프트 (완료됨)</strong></summary>

```text
현재 MockVideoSource로 동작하는 Qt 프로젝트에 실제 한화비전 카메라 RTSP 수신 기능을 추가해줘.

작업을 시작하기 전에 현재 저장소 구조와 다음 파일을 먼저 분석해줘.
- forklift-device/qt/common/video/
- forklift-device/qt/common/config/
- forklift-device/qt/apps/operator_terminal/
- forklift-device/qt/config/cameras.json
- forklift-device/qt/docs/INTEGRATION.md
- forklift-device/qt/CMakeLists.txt 및 하위 CMakeLists.txt

중요 조건:
- 기존 MockVideoSource와 LocalFileVideoSource는 삭제하지 않는다.
- 기존 QML UI와 DetectionOverlay 컴포넌트의 수정 범위를 최소화한다.
- MockVideoSource와 동일한 인터페이스를 구현하는 RtspVideoSource를 추가한다.
- 영상 소스는 config/cameras.json의 source_type 값으로 선택한다.
- source_type이 "mock"이면 MockVideoSource, "file"이면 LocalFileVideoSource,
  "rtsp"이면 RtspVideoSource를 생성하도록 VideoSourceManager를 수정한다.
- RTSP 주소와 인증정보를 QML 또는 소스 코드에 하드코딩하지 않는다.
- rtsp_url, latency, transport, decoder, reconnect 설정은 JSON 설정으로 분리한다.
- 연결 실패 시 프로그램이 종료되거나 UI가 멈추지 않아야 한다.
- 연결 실패 및 스트림 종료 시 exponential backoff 방식으로 자동 재연결한다.
- 재연결 중에는 마지막 정상 프레임을 유지한다.
- 핸드오버 시 새 스트림이 PLAYING 또는 프레임 수신 가능 상태가 된 이후 화면을 전환한다.
- 프레임 수신 시간, FPS, 해상도, 연결 상태, 마지막 오류 메시지를
  VideoSourceManager 또는 상태 모델을 통해 QML에 제공한다.
- GStreamer 콜백 스레드에서 QML 객체를 직접 조작하지 않는다.
- Qt 메인 스레드로 signal/slot 또는 안전한 큐를 통해 상태와 프레임을 전달한다.
- RTSP 인증정보는 로그에 그대로 출력하지 않는다.
- 카메라별 파이프라인 시작/정지/재연결이 독립적으로 동작해야 한다.
- 한 카메라의 연결 실패가 다른 카메라 스트림에 영향을 주지 않아야 한다.

GStreamer 요구사항:
- Linux/Raspberry Pi에서는 우선 다음 계열의 파이프라인을 검토한다.
  rtspsrc → rtph264depay 또는 rtph265depay → h264parse/h265parse
  → V4L2 하드웨어 디코더 → appsink
- 실제 카메라 코덱에 따라 H.264와 H.265를 모두 고려한다.
- Raspberry Pi에서 사용 가능한 V4L2 하드웨어 디코더를 런타임에 확인하고 우선 사용한다.
- 하드웨어 디코더 사용이 불가능하면 소프트웨어 디코더 fallback을 제공한다.
- Windows에서는 설치된 GStreamer 플러그인에 맞는 디코더를 설정으로 선택할 수 있게 한다.
- rtspsrc latency, protocols(TCP/UDP), drop-on-latency 등을 설정 파일에서 조절 가능하게 한다.
- appsink는 프레임이 밀려 무한 누적되지 않도록 max-buffers와 drop 설정을 적용한다.
- GStreamer bus의 ERROR, EOS, STATE_CHANGED 메시지를 처리한다.
- 파이프라인 생성, 시작, 정지, 오류 처리, 재연결 로직을 각각 함수로 분리한다.

설정 예시:
{
  "camera_id": "CAM_01",
  "name": "창고 입구",
  "zone": "ZONE_A",
  "source_type": "rtsp",
  "rtsp_url": "rtsp://<camera-ip>:554/profile2/media.smp",
  "rtsp": {
    "latency_ms": 150,
    "transport": "tcp",
    "codec": "auto",
    "decoder": "auto",
    "reconnect_initial_ms": 1000,
    "reconnect_max_ms": 30000
  }
}

한화비전 RTSP 주소 규칙도 설정 예시에 반영해줘.
- PNM 멀티센서:
  rtsp://<IP>:554/<sensor-index>/profile2/media.smp
- PNO 단일센서:
  rtsp://<IP>:554/profile2/media.smp

검증 항목:
1. source_type이 mock일 때 기존 Mock 화면이 정상 동작하는지 확인
2. 잘못된 RTSP 주소에서 DISCONNECTED 상태와 재연결 동작 확인
3. 실제 RTSP 연결 성공 시 영상, FPS, 해상도 표시 확인
4. 네트워크를 강제로 끊었다가 복구했을 때 자동 재연결 확인
5. 운전자 단말 camera_id 변경 시 마지막 프레임 유지와 무검은화면 전환 확인
6. 여러 카메라 중 하나가 끊겨도 다른 카메라가 정상 재생되는지 확인
7. Raspberry Pi와 Windows의 빌드 차이를 forklift-device/qt/docs/README.md와
   forklift-device/qt/docs/INTEGRATION.md에 기록

진행 순서:
1. 현재 구조에서 RtspVideoSource를 삽입할 위치 설명
2. 생성·수정할 파일 목록 제시
3. 인터페이스 및 설정 스키마 확정
4. 구현
5. CMake 및 GStreamer 의존성 연결
6. 단위 테스트 또는 Mock 기반 상태 전이 테스트 추가
7. 빌드 오류 수정
8. forklift-device/qt/docs/README.md와 forklift-device/qt/docs/INTEGRATION.md 갱신

기존 파일을 임의로 삭제하지 말고, 먼저 분석 결과와 구현 계획을 보여준 뒤 작업해줘.
```

</details>

<details>
<summary><strong>2. 중앙 서버 JSON 메타데이터 연동 프롬프트</strong></summary>

```text
현재 MockMetadataSource로 동작하는 Qt 프로젝트에 실제 중앙 서버 JSON 메타데이터 수신 기능을 추가해줘.

작업을 시작하기 전에 현재 저장소 구조와 다음 항목을 먼저 분석해줘.
- forklift-device/qt/common/models/RiskMetadata 관련 코드
- forklift-device/qt/common/network/IMetadataSource
- MockMetadataSource
- TcpMetadataSource 또는 NetworkClient 스켈레톤
- MetadataService
- ServerConnectionService
- ActiveCameraController
- DetectionOverlay
- forklift-device/qt/config/terminal.json
- forklift-device/qt/docs/INTEGRATION.md

중요 조건:
- MockMetadataSource는 삭제하지 않는다.
- UI와 QML은 MockMetadataSource와 실제 MetadataSource의 차이를 알지 못해야 한다.
- metadata_source 설정값으로 mock 또는 tcp를 선택할 수 있게 한다.
- WebSocket 구조가 현재 코드와 더 자연스럽지 않다면 TCP newline-delimited JSON을 우선 구현한다.
- TCP를 사용할 경우 JSON 객체 하나를 한 줄로 전송하는 NDJSON 형식을 기본으로 한다.
- 수신 버퍼에 JSON이 부분적으로 도착하거나 여러 메시지가 한 번에 도착하는 상황을 처리한다.
- 네트워크 스레드에서 QML 객체를 직접 조작하지 않는다.
- JSON 파싱 실패나 잘못된 메시지가 프로그램 종료로 이어지지 않아야 한다.
- 연결 상태, 마지막 정상 수신 시각, 마지막 오류 메시지, 재연결 횟수를 UI에 제공한다.
- 연결 실패 시 exponential backoff로 자동 재연결한다.
- 연결이 끊기면 NETWORK_DISCONNECTED 상태를 표시하되 마지막 영상은 유지한다.
- 재연결 성공 시 Mock 상태로 자동 전환하지 말고 실제 서버 수신을 계속 사용한다.
- TLS/mTLS는 이번 단계에서 인터페이스와 설정 항목을 준비하되,
  기존 보안 모듈이 없다면 평문 TCP를 먼저 완성한다.
- 실제 TLS 구현 전 인증서 검증을 비활성화하는 임시 우회 코드를 넣지 않는다.

서버에서 수신할 위험 이벤트 예시:
{
  "type": "risk_event",
  "camera_id": "CAM_01",
  "zone": "ZONE_A",
  "risk_level": 2,
  "distance_m": 0.82,
  "person_bbox": {
    "x": 0.52,
    "y": 0.27,
    "width": 0.14,
    "height": 0.38
  },
  "forklift_bbox": {
    "x": 0.18,
    "y": 0.42,
    "width": 0.28,
    "height": 0.31
  },
  "exception_state": "NONE",
  "utc_time": "2026-07-15T10:30:00.000Z"
}

운전자 단말 카메라 할당 메시지 예시:
{
  "type": "camera_assignment",
  "terminal_id": "TERM_01",
  "camera_id": "CAM_02",
  "zone": "ZONE_B",
  "utc_time": "2026-07-15T10:30:00.000Z"
}

선택적으로 heartbeat 메시지도 지원한다.
{
  "type": "heartbeat",
  "server_time": "2026-07-15T10:30:00.000Z"
}

검증 규칙:
- camera_id가 설정 파일에 없는 경우 경고 로그를 남기고 해당 이벤트는 무시한다.
- risk_level은 0~3 범위만 허용한다.
- bbox의 x, y, width, height는 0.0~1.0 범위로 검증한다.
- width와 height는 0보다 커야 한다.
- distance_m은 음수를 허용하지 않는다.
- exception_state는 정의된 enum 값만 허용한다.
- utc_time은 ISO 8601 형식으로 파싱한다.
- 같은 camera_id에 대해 현재 표시 중인 이벤트보다 오래된 utc_time의 이벤트는 무시한다.
- camera_assignment는 terminal_id가 현재 단말 ID와 일치할 때만 적용한다.
- 누락된 bbox는 전체 메시지를 폐기하지 말고, bbox 없는 이벤트로 처리 가능한 구조를 검토한다.
- 알 수 없는 type은 오류가 아니라 경고로 기록하고 무시한다.

연동 동작:
- risk_event 수신 시 RiskMetadata 모델을 갱신한다.
- person_bbox와 forklift_bbox는 DetectionOverlay에 전달한다.
- camera_assignment 수신 시 ActiveCameraController를 통해 운전자 단말의 camera_id를 변경한다.
- 새 camera_id의 영상 준비가 완료된 후 화면을 전환한다.
- 위험 메타데이터 수신 시각부터 QML 오버레이 갱신까지 지연시간을 측정해 로그로 남긴다.
- 서버 heartbeat가 일정 시간 없으면 연결 이상 상태로 판단할 수 있도록 timeout 설정을 추가한다.

설정 예시:
{
  "metadata_source": "tcp",
  "server": {
    "host": "192.168.0.10",
    "port": 9000,
    "terminal_id": "TERM_01",
    "heartbeat_timeout_ms": 3000,
    "reconnect_initial_ms": 1000,
    "reconnect_max_ms": 30000,
    "tls_enabled": false
  }
}

테스트 요구사항:
1. 정상 risk_event 파싱 테스트
2. 부분 수신 및 여러 NDJSON 메시지 동시 수신 테스트
3. 잘못된 JSON 무시 테스트
4. 범위를 벗어난 risk_level과 bbox 거부 테스트
5. 오래된 utc_time 이벤트 무시 테스트
6. terminal_id가 다른 camera_assignment 무시 테스트
7. 서버 강제 종료 후 재연결 상태 전이 테스트
8. Mock/Real 설정 전환 테스트
9. 메타데이터 수신부터 모델 갱신까지 지연 측정 로그 확인

진행 순서:
1. 현재 구조 분석
2. TCP와 WebSocket 중 현재 코드에 더 적합한 방식을 판단하고 근거 설명
3. 메시지 프레이밍 방식과 JSON 스키마 확정
4. 생성·수정할 파일 목록 제시
5. 구현
6. 단위 테스트 추가
7. 빌드 및 테스트 실행
8. forklift-device/qt/docs/README.md와 forklift-device/qt/docs/INTEGRATION.md 갱신

기존 Mock 및 데모 기능을 유지하고, 기존 파일을 임의로 삭제하지 말아줘.
먼저 분석 결과와 구현 계획을 보여준 뒤 작업해줘.
```

</details>

### 연동 작업 전 체크리스트

- 실제 장비 연동은 팀 Git 규칙(`feature/<위치>/<작업>`, 위치는 담당자가 아니라
  코드가 도는 기기 기준)에 맞춰 브랜치를 나눠 진행한다. `common/`이
  forklift-device/operator-device에 각각 독립 사본으로 존재하므로(의도적
  중복), 아래 RTSP·서버 메타데이터 연동도 두 프로젝트에서 각각 별도로
  구현해야 한다(한쪽만 고치고 끝나지 않음).
  - RTSP 카메라 연동 (`forklift-device/qt/common/video/RtspVideoSource.cpp`):
    **완료**. forklift-device·operator-device 양쪽 다 구현 완료, 실카메라
    검증됨.
  - 서버 메타데이터 연동
    (`forklift-device/qt/common/network/TcpMetadataSource.cpp`): 위와 동일한
    사유로 브랜치 하나, 트랙명은 팀 확인 필요 (가칭
    `feature/forklift-device/metadata-integration`). operator-device 쪽은
    별도 브랜치로 동일 작업 필요.
  - 핸드오버 제어채널
    (`forklift-device/qt/apps/operator_terminal/main.cpp`의 `NetworkClient`
    연결 — 단말 전용): `feature/forklift-device/handover-control`
  - 물리 경고 장치 연동
    (`forklift-device/qt/apps/operator_terminal/main.cpp`의
    `SerialWarningDevice` — 단말 전용): `feature/forklift-device/warning-device-serial`
    (파트별 작업가이드의 공통 인터페이스 규약에 명시된 "UART 경고 패킷(0xAA + risk_level
    + checksum, watchdog 주기 포함) — 네트워크·단말 ↔ FPGA 보드" 접점에 해당. 즉 차현민님의
    FPGA 경고 보드와 UART로 통신하는 것이 맞는 설계다. 패킷 포맷·watchdog 주기는 구현 전
    차현민님과 최종 확정 필요. 별도로 BSP 파트가 담당하는 "/dev 노드 인터페이스(LED·부저·
    진동 개별 구동)"가 이것과 같은 경로인지 다른 경로인지는 아직 불명확 — 확인 필요)
- `config/*.json`의 실제 IP, 계정, 비밀번호는 Git에 커밋하지 않기
- Mock 모드는 실제 장비 장애 시에도 UI를 검증할 수 있도록 계속 유지
- 실제 카메라 연결 전에 VLC 또는 `gst-launch-1.0`으로 RTSP 주소와 코덱 확인
- 서버 JSON 스키마(`risk_event`의 `exception_state` 값 목록 포함)는 서버·검출 담당자와
  먼저 확정한 뒤 코드에 반영 — 특히 `exception_state`는 위험 판정 엔진 쪽에서 정의한
  값(`SENSOR_FAULT`, `DEAD_RECKONING`, `EMERGENCY_IMPACT`, 정상 시 `NONE`)과 정확히
  일치하는지 대조
- 연동 완료 후 Windows와 Raspberry Pi에서 각각 빌드 및 실행 검증
