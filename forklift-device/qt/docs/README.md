# 지게차 운전자 단말 (forklift-device/qt)

Qt 6 / QML 기반의 운전자용 사용자 단말 프로그램(`operator_terminal`)임.  
현재는 실제 카메라/서버 연결 전 상태로 모든 영상과 위험 이벤트가 Mock 소스로 생성되며, `--demo` 옵션을 통해 위험 단계·연결 상태·카메라 전환 등을 즉석에서 시연할 수 있음.

> 💡 실제 RTSP 카메라, 중앙 서버, 물리 경고 장치 연동 시 수정 위치 및 방법은 [INTEGRATION.md](INTEGRATION.md) 파일 참조.

---

## 프로젝트 구조

```text
CMakeLists.txt / CMakePresets.json   최상위 빌드 설정
common/                              C++ 백엔드 + QML 테마/컴포넌트 (Safety.Common 모듈)
                                     (operator-device/qt/common과는 독립된 사본)
  models/     RiskMetadata, BBox, CameraInfo, 공용 enum(RiskTypes)
  video/      IVideoSource 및 Mock/LocalFile/Rtsp 구현, VideoStream, DetectionOverlay
  network/    IMetadataSource 및 Mock 구현, IWarningDevice 및 Noop 구현
  config/     ConfigLoader (JSON 설정 파싱)
  services/   MetadataDistributor, ServerConnectionService, 각종 QML 모델, DemoController
apps/
  operator_terminal/  운전자 단말 실행 파일 (main.cpp, ActiveCameraController, OperatorDemoController)
qml/
  theme/              Theme.qml (색상·간격·타이포그래피 싱글톤)
  components/         공유 QML 컴포넌트
  operator_terminal/  운전자 단말 전용 QML
config/               cameras.json, terminal.json 샘플
tests/                QtTest 기반 단위 테스트
```

---

## 빌드 방법

### Windows 빌드 (Qt 6.11.0 MinGW)

- **환경 구성**: `C:/Qt/6.11.0/mingw_64` + `C:/Qt/Tools/mingw1310_64`(g++) + `C:/Qt/Tools/Ninja`
- **CMake 프리셋**: `windows-mingw` 제공 (설치 경로가 다를 경우 `CMakePresets.json` 수정 필요)
- **빌드 명령어**:
  ```powershell
  cmake --preset windows-mingw
  cmake --build --preset windows-mingw
  ctest --preset windows-mingw --output-on-failure
  ```
- **산출물 위치**: `build/windows-mingw/operator_terminal.exe` (실행 파일 옆 `config/` 디렉터리 자동 복사)
- **실행 전 PATH 설정** (Qt DLL 로드용):
  ```powershell
  $env:PATH = "C:/Qt/6.11.0/mingw_64/bin;$env:PATH"
  ```

### Linux / Raspberry Pi 빌드

- **요구 사항**: Qt 6.5 이상 (Core, Gui, Qml, Quick, QuickControls2, Multimedia, Network 모듈)
- **빌드 명령어**:
  ```bash
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```
- **구동 특성**: Raspberry Pi 4 전체 화면 구동 지원 (`OperatorWindow.qml` 실행 시 `Window.FullScreen` 자동 적용)

---

## 실행 방법

- **기본 실행 명령어**:
  ```powershell
  .\operator_terminal.exe --demo --camera CAM_01
  ```

- **실행 옵션**:
  - `--demo`: 데모 패널 활성화 (`Ctrl+Shift+D` 단축키 제공, 지정 미해당 시 단축키 비활성화)
  - `--config <dir>`: `cameras.json` / `terminal.json` 읽기 디렉터리 지정 (기본값: 실행 파일 옆 `config/`)
  - `--camera <id>`: 시작 시 표시할 `camera_id` 강제 지정

- **데모 패널 제공 기능**:
  - 카메라별 위험 단계(SAFE/CAUTION/DANGER/EMERGENCY) 및 예외 상태 강제 설정
  - Mock 이벤트 자동 재생 시작/정지 제어
  - 서버 및 카메라 연결/끊김 상태 토글
  - 사람/지게차 영역 상자(BBox) 위치 조절
  - 표출 `camera_id` 전환 기능 제공 (핸드오버 재현)

---

## 설정 파일

- **카메라 추가 (`config/cameras.json`)**:
  - 신규 카메라 등록 시 재빌드 없이 `--camera` 옵션 및 서버 핸드오버 목록에 반영
  - `source_type`을 `"mock"`에서 `"rtsp"`로 변경하고 `rtsp_url` 입력 시 실제 카메라 스트림으로 전환 (`RtspVideoSource` 구현 완료)
  - 세부 적용 절차는 [INTEGRATION.md](INTEGRATION.md) 참조

---

## 테스트

UI/QML과 독립적으로 동작하는 `common/` 하위 서비스 클래스 대상 QtTest 단위 테스트 실행 (`tests/`):

- **테스트 항목**:
  - `test_config_loader`: `cameras.json` 파싱 및 무효 항목 스킵 검증
  - `test_mock_metadata_source`: 위험 단계 제어 및 오버라이드 동작 검증
  - `test_bbox_aspect_fit`: 비율 유지 좌표 변환 계산 검증

- **테스트 실행 명령어**:
  ```powershell
  ctest --preset windows-mingw --output-on-failure
  ```

---

## 코딩 컨벤션

### 주석 작성 규칙
- 한글 작성 원칙
- 명사형 개조식 종결 적용 (예: ~발생, ~해제, ~예약 / 서술형 문장 지양)
- 단순 코드 설명 지양 및 작성 이유/의도 중심 기재
- 파일 내 기존 주석 스타일 유지

### 네이밍 규칙
- 짧고 직관적인 영어 사용
- 기존 코드베이스 내 단어 조합 우선 적용
- 모호하고 추상적인 전문용어 지양 (예: `Coordinator`, `Hub` 등 모호한 접미어)
- 전송 수단이 아닌 수송 데이터 기준 명명 (예: `RiskEventSource`)

### Windows 빌드 및 런타임 오류 진단 순서
1. **PATH 빌드 도구 확인**: 빌드 도구 및 Qt DLL 경로 동시 적용
   ```powershell
   $env:PATH = "C:/Qt/Tools/CMake_64/bin;C:/Qt/Tools/Ninja;C:/Qt/Tools/mingw1310_64/bin;C:/Qt/6.11.0/mingw_64/bin;$env:PATH"
   ```
2. **`build/` 내 `config/` 디렉터리 최신 여부 확인**: 원본 미반영 시 사본 확인 필요
3. **기실행 중인 프로세스 여부 확인**: `Permission denied` 발생 시 파일 잠금 해제 필요
4. **CMake 실행 디렉터리 위치 확인**: `CMakePresets.json`이 위치한 디렉터리에서 실행
5. **Git 명령 경로 확인**: 리포지토리 최상위 디렉터리 기준으로 명령 수행

---

## 향후 연동용 Claude Code 프롬프트

- **RTSP 카메라 연동**: **완료** (`RtspVideoSource` 구현 및 실카메라 검증 완료)
- **중앙 서버 JSON 메타데이터 연동**: **미완료** (아래 2번 프롬프트 활용 진행)

> 💡 작업 전 신규 Git 브랜치 생성 필수, 기존 Mock 구현을 유지한 상태에서 실제 구현체 추가 진행.

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
- RiskEventSource 또는 HandoverClient 스켈레톤
- MetadataDistributor
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

단말 -> 서버 hello 메시지 예시 (위 두 메시지와 반대 방향):
{
  "type": "hello",
  "terminal_id": "TERM_01"
}

단말이 핸드오버 채널(9001)에 접속하는 즉시 1회 보낸다. 서버는 이 값으로 그 소켓이
어느 terminal_id인지 저장해두고, 이후 그 소켓으로 보내는 camera_assignment의
terminal_id를 채운다. 서버 설정과 단말 설정(terminal.json의 terminal_id)을 사람이
손으로 맞춰야 하는 방식(둘이 어긋나면 조용히 무시됨) 대신, 접속마다 단말이 스스로
알려주는 방식으로 2026-08-06 forklift-device/server 협의.

별도의 heartbeat 메시지는 두지 않는다 (2026-08-03 서버 담당자 협의).
서버가 판정 결과를 주기적으로 계속 publish하므로(상태 변화 시 즉시 송신 +
그 시점부터 주기 타이머 리셋), 정상 동작 중에는 risk_event 자체가 생존 신호
역할을 한다. 단말은 메시지 종류를 구분하지 않고 "무엇이든 수신했는가"만으로
연결 상태를 판단한다.
송신 주기는 초기 200ms, 실측 후 1000~1500ms로 조정 예정.

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
- 서버 메시지가 일정 시간 없으면 연결 이상 상태로 판단할 수 있도록 timeout 설정을 추가한다.
  타임아웃은 서버 송신 주기의 3배를 기준으로 한다 (FPGA<->Pi 채널의 기존 관례와 동일 --
  gpio-control/src/PROTOCOL.md의 heartbeat 200ms / 판정 600ms 참고).

설정 예시:
{
  "metadata_source": "tcp",
  "server": {
    "host": "192.168.0.10",
    "risk_port": 9000,
    "handover_port": 9001,
    "terminal_id": "TERM_01",
    "recv_timeout_ms": 600,
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

---

### 연동 작업 전 체크리스트

- **Git 브랜치 규칙**: `feature/<위치>/<작업>` 형식을 준수하여 브랜치 분리 진행 (도체 기준)
  - `common/` 모듈은 `forklift-device`와 `operator-device`에 각각 독립 사본으로 존재하므로 두 프로젝트 개별 구현 필요
- **세부 모듈별 진행 상황**:
  - **RTSP 카메라 연동** (`forklift-device/qt/common/video/RtspVideoSource.cpp`): **완료** (양쪽 프로젝트 구현 및 실카메라 검증 완료)
  - **서버 메타데이터 연동** (`forklift-device/qt/common/network/RiskEventSource.cpp`): 브랜치 생성 및 작업 진행 필요 (가칭 `feature/forklift-device/metadata-integration`)
  - **핸드오버 제어 채널** (`HandoverClient` 연결): `feature/forklift-device/handover-control` 브랜치 생성 진행
  - **물리 경고 장치 연동** (`SerialWarningDevice` 연동): `feature/forklift-device/warning-device-serial` 브랜치 생성 진행
    - UART 경고 패킷 규격: `0xAA` + `risk_level` + `checksum` (watchdog 주기 포함)
    - 구현 전 FPGA 담당자와 패킷 포맷 및 watchdog 주기 최종 확정 필요
- **보안**: `config/*.json` 내 실제 IP, 계정, 비밀번호 커밋 금지
- **유지보수성**: 실제 장비 장애 대응을 위해 Mock 모드는 항상 유지
- **사전 검증**: 실제 카메라 연결 전 VLC 또는 `gst-launch-1.0`을 통한 RTSP 주소 및 코덱 검증
- **스키마 동기화**: 서버 JSON 스키마(`exception_state` enum 값 포함: `NONE`, `SENSOR_FAULT`, `DEAD_RECKONING`, `EMERGENCY_IMPACT`) 사전 대조
- **camera_id 동기화**: `cameras.json`의 각 카메라 `camera_id` 목록과
  `terminal.json`의 `default_camera_id`가 서로 다른 파일에서 독립 관리됨.
  어긋나면 실패로 드러나지 않고 조용히 무시/기본 화면 표시로 넘어갈 수 있으니,
  배포 전 두 파일을 육안으로 대조할 것. 서버가 내려주는 `camera_assignment`
  응답의 camera_id도 이 목록에 실존해야 하므로, 카메라 추가/제거 시
  `cameras.json` 갱신을 빠뜨리지 않았는지 함께 확인.
- **크로스 플랫폼 검증**: 연동 완료 후 Windows 및 Raspberry Pi 환경에서 각각 빌드/실행 검증 수행
