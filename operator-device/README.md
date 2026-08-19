# 🖥️ 중앙 관제 센터 (operator-device)

관제실 PC에서 공장 전체 구역(Zone)과 4채널 멀티센서 CCTV 영상을 실시간 모니터링하고 일일 안전일지를 관리하는 데스크톱 프로그램(`control_center`)입니다.

---

## 📂 프로젝트 및 소스 구조

```text
operator-device/
└── qt/                          # Qt 6 / QML 기반 관제 센터 소프트웨어
    ├── apps/control_center/     # 실행 엔트리포인트 (main.cpp)
    ├── common/                  # C++ 백엔드 및 서비스 레이어 (Safety.Common 모듈)
    │   ├── config/              # JSON 설정 파서 (ConfigLoader, ConfigTypes)
    │   ├── models/              # 위험도/계정/카메라 모델 (RiskMetadata, OperatorAccount)
    │   ├── network/             # 통신 계층 (RiskEventSource[MQTT], MockMetadataSource)
    │   ├── services/            # 비즈니스 로직 (AuthService, EventLogModel, MetadataDistributor)
    │   └── video/               # 다채널 영상 관리 (VideoSourceManager, RtspVideoSource)
    ├── qml/                     # QML UI 계층
    │   ├── control_center/      # 관제 화면 (ControlCenterWindow, ZoneListView, CameraOverviewView, CameraGrid, ZoneHierarchyView, LoginOverlay)
    │   ├── components/          # 공용 UI 컴포넌트 (CameraVideoView, RiskBanner)
    │   └── theme/               # 다크 테마 싱글톤 (Theme.qml)
    └── config/                  # 런타임 설정 (cameras.json, control_center.json, operators.json)
```
> 💡 `operator-device/qt/common`은 `forklift-device/qt/common`과 독립된 사본으로 유지됩니다.

---

## 🛠️ 1. 빌드 방법 (Windows MinGW)

- **방법 1 (원클릭 빌드)**:
  - **실행 경로**: `C:\VEDA_Final_project` (프로젝트 루트)
  ```powershell
  .\build_operator.bat
  ```
- **방법 2 (직접 CMake 빌드)**:
  - **실행 경로**: `C:\VEDA_Final_project\operator-device\qt`
  ```powershell
  $env:PATH = "C:/Qt/Tools/CMake_64/bin;C:/Qt/Tools/Ninja;C:/Qt/Tools/mingw1310_64/bin;C:/Qt/6.11.0/mingw_64/bin;$env:PATH"
  cmake --preset windows-mingw
  cmake --build --preset windows-mingw
  ```
  - **빌드 산출물 위치**: `operator-device\qt\build\windows-mingw\control_center.exe`

---

## 🚀 2. 실행 및 시연 방법

- **실행 경로**: `C:\VEDA_Final_project` (프로젝트 루트)
```powershell
.\run_operator.bat
```

### 🎮 화면 조작 및 3단계 드릴다운 탐색
1. **로그인**: 운영자 ID `hanwha` / PIN `5hanwha!` 입력
2. **1단계 (구역 선택)**: 화면의 **`ZONE_A` (창고 입구)** 구역 카드 클릭
3. **2단계 (카메라 선택)**: 해당 구역에 속한 **`CAM_01`** 카메라 카드 클릭
4. **3단계 (4채널 감시)**: `CH1` ~ `CH4` 실시간 화면 동시 표출 (채널 클릭 시 대화면 확대, 상단 뒤로가기 또는 `Esc`로 복귀)
5. **데모 조작 패널**: **`Ctrl + Shift + D`** 로 위험도 조작창 호출

---

## ⚙️ 3. 실제 런타임 설정 파일 (`config/`)

### 1) 관제 센터 설정 (`config/control_center.json`)
```json
{
  "system_name": "지게차 사각지대 충돌 방지 - 관제 센터",
  "_comment_mqtt_broker_host": "RiskEventSource가 구독할 MQTT 브로커 주소. forklift-device의 terminal.json과 동일해야 함",
  "mqtt_broker_host": "192.168.0.13",
  "_comment_mqtt_broker_port": "RiskEventSource가 구독할 MQTT 브로커 포트 (기본 1883)",
  "mqtt_broker_port": 1883,
  "_comment_terminal_id": "구독 토픽(forklift/risk/<terminal_id>) 구성용. 관제할 단말 ID를 서버 설정과 맞출 것",
  "terminal_id": "TERM_01",
  "_comment_metadata": "mock → mqtt로 전환 (RiskEventSource MQTT 구현 완료 시)",
  "metadata_source_type": "mqtt",
  "event_log_max_entries": 200
}
```

### 2) 4채널 멀티센서 카메라 설정 (`config/cameras.json`)
```json
{
  "cameras": [
    {
      "stream_id": "CAM_01_CH_01",
      "camera_id": "CAM_01",
      "channel": 0,
      "name": "CH 1 (창고 입구)",
      "zone": "ZONE_A",
      "source_type": "rtsp",
      "rtsp_url": "rtsp://USERNAME:PASSWORD@CAMERA_IP:554/0/onvif/profile2/media.smp",
      "local_file_path": ""
    },
    {
      "stream_id": "CAM_01_CH_02",
      "camera_id": "CAM_01",
      "channel": 1,
      "name": "CH 2 (적재 구역)",
      "zone": "ZONE_A",
      "source_type": "rtsp",
      "rtsp_url": "rtsp://USERNAME:PASSWORD@CAMERA_IP:554/1/onvif/profile2/media.smp",
      "local_file_path": ""
    },
    {
      "stream_id": "CAM_01_CH_03",
      "camera_id": "CAM_01",
      "channel": 2,
      "name": "CH 3 (출하 게이트)",
      "zone": "ZONE_A",
      "source_type": "rtsp",
      "rtsp_url": "rtsp://USERNAME:PASSWORD@CAMERA_IP:554/2/onvif/profile2/media.smp",
      "local_file_path": ""
    },
    {
      "stream_id": "CAM_01_CH_04",
      "camera_id": "CAM_01",
      "channel": 3,
      "name": "CH 4 (통로 교차로)",
      "zone": "ZONE_A",
      "source_type": "rtsp",
      "rtsp_url": "rtsp://USERNAME:PASSWORD@CAMERA_IP:554/3/onvif/profile2/media.smp",
      "local_file_path": ""
    }
  ]
}
```

---

## 🌟 4. 핵심 기능 요약
1. **3단계 계층 드릴다운 탐색**: `구역` → `카메라` → `4채널 그리드` → `단일 채널 확대`
2. **우측 구역 계층 트리 패널 (ZoneHierarchyView)**: 전체 장비 상태 모니터링 및 클릭 시 해당 채널로 즉시 포커스
3. **안전한 운영자 로그인**: PIN 해시 검증 및 5회 연속 실패 시 30초 자동 잠금
4. **일일 안전일지 CSV 내보내기**: 실시간 위험 이벤트 기록 및 파일 저장
