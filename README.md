# 지게차 사각지대 충돌 방지 통합 안전 시스템

한화비전 VEDA 프로그램 3조 팀 프로젝트 — CCTV 영상 분석 및 센서 융합을 통해 지게차 사각지대의 보행자를 감지하고 충돌을 방지하는 시스템입니다.

---

## 🌐 1. 물리 장비 배포 아키텍처

각 소프트웨어는 물리적으로 분리된 4개의 독립 하드웨어에서 구동되며 네트워크로 연동됩니다:

```text
       ┌────────────────────────────────┐
       │   📹 CCTV (한화비전 네트워크)     │
       │   - 4채널 영상 및 AI 검출 메타데이터  │
       │   - IP: 192.168.0.3            │
       └──────────────┬─────────────────┘
                      │ RTSP (포트 554)
                      ▼
       ┌────────────────────────────────┐
       │  🍓 중앙 안전 서버 (라즈베리파이 #1)  │
       │  - server/ (C++ 안전 판정 서버)   │
       │  - IP: 192.168.0.13 (MQTT TLS 8883)│
       └──────┬─────────────────┬───────┘
              │ MQTT (위험 이벤트) │ MQTT (관제 데이터)
              ▼                 ▼
┌───────────────────────────┐  ┌───────────────────────────┐
│ 🚜 지게차 운전자 단말     │  │ 🖥️ 중앙 관제 센터        │
│ (라즈베리파이 #2 + FPGA)   │  │ (관제 PC / Windows)       │
│ - forklift-device/        │  │ - operator-device/        │
│ - 운전자 터치 UI & 경광등  │  │ - 3단계 계층 관제 화면    │
└───────────────────────────┘  └───────────────────────────┘
```

---

## 🏛️ 2. 하드웨어별 역할 및 배포 대상

| 디렉터리 | 배포 대상 기기 | 담당 역할 | 기술 스택 | 세부 가이드 |
|---|---|---|---|:---:|
| [`forklift-device/`](forklift-device/README.md) | **지게차 단말** (라즈베리파이 #2 + FPGA) | 운전자 터치 UI, 4채널 영상, 위험 경고 배너, FPGA 경광등/부저 제어 | Qt 6 / QML, C++17, Verilog | [바로가기](forklift-device/README.md) |
| [`operator-device/`](operator-device/README.md) | **관제실 PC** (Windows 10/11) | 3단계 계층 탐색(구역→카메라→4채널), 실시간 모니터링, 일일 안전일지 CSV 관리 | Qt 6 / QML, C++17 | [바로가기](operator-device/README.md) |
| [`server/`](server/README.md) | **중앙 서버** (라즈베리파이 #1) | CCTV/센서 메타데이터 수신, 호모그래피 좌표 변환, 위험도 계산, MQTT 브로드캐스트 | C++17, Python, SQLite | [바로가기](server/README.md) |
| [`cctv/`](cctv/ArUCoDetector/README.md) | **현장 CCTV** (카메라 내부) | ArUco 마커 검출, 렌즈 왜곡 보정 및 메타데이터 전송 | C++, OpenCV, OpenSDK | [바로가기](cctv/ArUCoDetector/README.md) |

---

## 🚀 3. 장비별 빠른 실행 가이드 (Quick Start)

### 🚜 1) 지게차 운전자 단말 (라즈베리파이 환경 / Linux)
- **작업 경로**: `~/VEDA_Final_project` (저장소 루트)
```bash
# 원클릭 빌드
./build_forklift.sh

# 데모 실행 (Mock 영상 및 이벤트)
./run_forklift.sh --demo --camera CAM_01_CH_01
```
> 실행 후 키보드 `Ctrl + Shift + D`로 데모 조작 패널을 열 수 있습니다. (산출물: `forklift-device/qt/build/operator_terminal`)

---

### 🖥️ 2) 중앙 관제 센터 (관제실 PC / Windows MinGW)
- **작업 경로**: `C:\VEDA_Final_project` (저장소 루트)
```powershell
# 원클릭 빌드
.\build_operator.bat

# 데모 실행
.\run_operator.bat
```
> 로그인: ID `hanwha` / PIN `5hanwha!` (로그인 후 `Ctrl + Shift + D`로 데모 패널 열기)  
> (산출물: `operator-device\qt\build\windows-mingw\control_center.exe`)

---

### 💻 3) 지게차 단말 Windows PC 개발/테스트용 실행
- **작업 경로**: `C:\VEDA_Final_project`
```powershell
.\build_forklift.bat
.\run_forklift.bat
```

> 💡 **PowerShell 단축어**: `. .\setup_aliases.ps1` 실행 시 `bo`(관제 빌드), `ro`(관제 실행), `bf`(단말 빌드), `rf`(단말 실행) 사용 가능

---

## 📖 트랙별 메인 가이드
- **지게차 운전자 단말**: [`forklift-device/README.md`](forklift-device/README.md)
- **중앙 관제 센터**: [`operator-device/README.md`](operator-device/README.md)
- **중앙 안전 서버**: [`server/README.md`](server/README.md)
- **현장 CCTV (ArUco)**: [`cctv/ArUCoDetector/README.md`](cctv/ArUCoDetector/README.md)

