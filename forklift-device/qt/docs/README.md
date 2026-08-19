# 지게차 단말 내부 개발자 가이드 (forklift-device/qt)

Qt 6 / QML 기반 지게차 운전자 단말 프로그램(`operator_terminal`)의 내부 아키텍처 및 C++ 백엔드 모듈 상세 문서입니다.

---

## 📂 프로젝트 내부 구조

```text
CMakeLists.txt / CMakePresets.json   최상위 빌드 설정
common/                              C++ 백엔드 + QML 테마/컴포넌트 (Safety.Common 모듈)
                                     operator-device/qt/common과는 독립된 사본으로 유지
  models/     RiskMetadata, BBox, CameraInfo, 공용 enum(RiskTypes)
  video/      IVideoSource 및 Mock/LocalFile/Rtsp 구현, VideoStream, DetectionOverlay(C++)
  network/    RiskEventSource(MQTT), HandoverClient(MQTT), SerialWarningDevice(FPGA UART)
  config/     ConfigLoader (JSON 설정 파싱)
  services/   MetadataDistributor, ServerConnectionService, DemoController
apps/
  operator_terminal/  운전자 단말 실행 파일 (main.cpp, ActiveCameraController)
qml/
  theme/              Theme.qml (색상·간격·타이포그래피 싱글톤)
  components/         공유 QML 컴포넌트 (RiskBanner, CameraVideoView 등)
  operator_terminal/  운전자 단말 전용 QML (OperatorWindow, StatusStrip, DemoPanel 등)
config/               cameras.json, terminal.json
```

---

## 🏗️ 모듈 연동 현황

- **RTSP 영상 수신 (`RtspVideoSource`)**: GStreamer 파이프라인(`rtspsrc ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! appsink`) 기반 다채널 영상 수신 및 프레임 누적 방지(`max-buffers=2, drop=true`)
- **중앙 서버 MQTT 수신 (`RiskEventSource`)**: MQTT 브로커(`192.168.0.13:1883`) 구독(`forklift/risk/TERM_01`), 4개 채널 식별 및 실거리(mm) 환산
- **카메라 자동 핸드오버 (`HandoverClient`)**: `forklift/assignment/TERM_01` 토픽 구독, 새 영상 준비 시까지 이전 프레임 유지(블랙아웃 방지)
- **FPGA 하드웨어 세이프가드 (`SerialWarningDevice`)**: `/dev/serial0` (115200bps) UART 통신, 100ms 주기 Watchdog 생존 펄스 및 비상정지 래치 감시
