# 🚜 지게차 운전자 단말 (forklift-device)

지게차 운전석 터치스크린에 장착되는 안전 보조 단말 프로그램(`operator_terminal`) 및 FPGA 하드웨어 안전 제어기(`gpio-control`)입니다.  
CCTV/AI 서버로부터 수신된 위험도와 보행자 BBox를 화면에 표출하고, FPGA 하드웨어(경광등/부저/E-Stop 물리 정지)를 제어합니다.

---

## 📂 프로젝트 및 소스 구조

```text
forklift-device/
├── qt/                          # Qt 6 / QML 기반 운전자 단말 소프트웨어
│   ├── apps/operator_terminal/  # 실행 엔트리포인트 (main.cpp, ActiveCameraController)
│   ├── common/                  # C++ 백엔드 및 서비스 레이어 (Safety.Common 모듈)
│   │   ├── config/              # JSON 설정 파서 (ConfigLoader)
│   │   ├── models/              # 위험도/BBox/카메라 모델 (RiskMetadata, RiskTypes, CameraInfo)
│   │   ├── network/             # 통신 계층 (RiskEventSource[MQTT], HandoverClient[MQTT], SerialWarningDevice[FPGA UART])
│   │   ├── services/            # 데이터 분배 (MetadataDistributor, ServerConnectionService)
│   │   └── video/               # 영상 스트리밍 (RtspVideoSource[GStreamer], MockVideoSource, VideoStream)
│   ├── qml/                     # QML UI 계층 (Theme.qml, DetectionOverlay, StatusStrip, DemoPanel)
│   └── config/                  # 런타임 설정 (cameras.json, terminal.json)
└── gpio-control/                # Gowin FPGA 하드웨어 안전 제어기 (Verilog HDL)
    ├── src/                     # FSM, UART TX/RX, Watchdog(100ms), E-Stop 래치(2000ms), PROTOCOL.md
    └── FPGA_control.gprj        # Gowin EDA 프로젝트 파일
```
> 💡 `forklift-device/qt/common`은 `operator-device/qt/common`과 독립된 사본으로 유지됩니다.

---

## 🍓 1. 라즈베리파이 (실기기 / Linux) 빌드 및 실행

### [Step 0] 필수 패키지 설치 (최초 1회)
```bash
sudo apt update
sudo apt install -y qt6-base-dev qt6-declarative-dev qt6-multimedia-dev libqt6serialport6-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libmosquitto-dev cmake ninja-build g++
```

### [Step 1] 빌드 (Compile)
- **실행 경로**: `~/VEDA_Final_project` (프로젝트 루트)
```bash
./build_forklift.sh
```
> 또는 `cd ~/VEDA_Final_project/forklift-device/qt` 이동 후 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)` 실행  
> 빌드 산출물 위치: `forklift-device/qt/build/operator_terminal`

### [Step 2] 실행 (Run)
- **단독 테스트(데모) 모드 (Mock 영상 및 이벤트)**:
  - **실행 경로**: `~/VEDA_Final_project`
  ```bash
  ./run_forklift.sh --demo --camera CAM_01_CH_01
  ```
  - **시연 조작**: 키보드에서 **`Ctrl + Shift + D`** 를 눌러 데모 패널 호출 (위험 단계 강제 변경, Mock 이벤트 On/Off)
- **실제 현장 연동 모드 (실카메라 RTSP & 실서버 MQTT)**:
  ```bash
  ./run_forklift.sh --camera CAM_01_CH_01
  ```

---

## 💻 2. Windows PC 개발/테스트 가이드
- **실행 경로**: `C:\VEDA_Final_project` (프로젝트 루트)
```powershell
# 빌드
.\build_forklift.bat

# 데모 실행
.\run_forklift.bat
```
> 빌드 산출물 위치: `forklift-device\qt\build\windows-mingw\operator_terminal.exe`

---

## ⚙️ 3. 실제 런타임 설정 파일 (`config/`)

### 1) 단말 및 통신 설정 (`config/terminal.json`)
```json
{
  "default_camera_id": "CAM_01_CH_01",
  "_comment_mqtt_broker_host": "RiskEventSource 및 HandoverClient가 구독할 MQTT 브로커 주소",
  "mqtt_broker_host": "192.168.0.13",
  "_comment_mqtt_broker_port": "MQTT 브로커 포트 (기본 1883)",
  "mqtt_broker_port": 1883,
  "terminal_id": "TERM_01",
  "_comment_metadata": "mock → mqtt로 전환 (RiskEventSource MQTT 구현 완료 시)",
  "metadata_source_type": "mqtt",
  "_comment_fpga_serial_port": "FPGA 경고 보드 UART 포트. Pi GPIO 8/10번 핀(TXD/RXD) 직결 기준 기본값 (raspi-config에서 serial hardware 활성화 필요, PROTOCOL.md 참고)",
  "fpga_serial_port": "/dev/serial0",
  "_comment_fpga_baud_rate": "gpio-control/src/top.v 기본값과 일치",
  "fpga_baud_rate": 115200,
  "_comment_warning_device": "noop → serial로 전환 (FPGA 실장치 연결 완료 시)",
  "warning_device_type": "serial"
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
1. **위험도 비례 실시간 시각 경고**: SAFE / CAUTION / DANGER / EMERGENCY 단계별 화면 테두리 색상 및 고시인성 배너 표출, BBox 및 실거리(m) 환산 오버레이
2. **검은 화면 깜빡임 없는 매끄러운 카메라 전환**: 핸드오버 시 새 스트림의 첫 프레임 도착 시까지 이전 프레임을 유지하여 블랙아웃 방지
3. **FPGA 하드웨어 세이프가드**: 단말 OS 다운 시에도 100ms Watchdog 펄스를 감시하여 FPGA가 독립적으로 비상정지 및 경광등 구동 보장

---

## 📖 심화 기술 참고 문서
- **FPGA UART 통신 프로토콜 규격서**: [`gpio-control/src/PROTOCOL.md`](gpio-control/src/PROTOCOL.md)
- **결함 주입(Fail-Safe) 시험 절차 및 분석**: [`qt/docs/FAULT_INJECTION.md`](qt/docs/FAULT_INJECTION.md)
- **라즈베리파이 systemd 자동 실행 가이드**: [`qt/deploy/systemd/README.md`](qt/deploy/systemd/README.md)
- **Qt 내부 아키텍처 가이드**: [`qt/docs/README.md`](qt/docs/README.md)

