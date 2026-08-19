# 관제 센터 내부 개발자 가이드 (operator-device/qt)

Qt 6 / QML 기반 중앙 관제 센터 프로그램(`control_center`)의 내부 아키텍처 및 C++ 백엔드 모듈 상세 문서입니다.

---

## 📂 프로젝트 내부 구조

```text
CMakeLists.txt / CMakePresets.json   최상위 빌드 설정
common/                              C++ 백엔드 + QML 테마/컴포넌트 (Safety.Common 모듈)
                                     forklift-device/qt/common과는 독립된 사본으로 유지
  models/     RiskMetadata, BBox, CameraInfo, 공용 enum(RiskTypes), OperatorAccount
  video/      IVideoSource 및 Mock/LocalFile/Rtsp 구현, VideoStream, DetectionOverlay
  network/    RiskEventSource(MQTT), MockMetadataSource
  config/     ConfigLoader (JSON 설정 파싱)
  services/   MetadataDistributor, ServerConnectionService, EventLogModel, AuthService, DemoController
apps/
  control_center/     관제 센터 실행 파일 (main.cpp)
qml/
  theme/              Theme.qml (색상·간격·타이포그래피 싱글톤)
  components/         공유 QML 컴포넌트 (CameraVideoView, RiskBanner)
  control_center/     관제 센터 전용 QML (ControlCenterWindow, ZoneListView, CameraOverviewView, CameraGrid, ZoneHierarchyView, LoginOverlay, EventLogPanel, DemoPanel)
config/               cameras.json, control_center.json, operators.json
```

---

## 🏗️ 모듈 연동 현황

- **RTSP 영상 수신 (`RtspVideoSource`)**: GStreamer 파이프라인 기반 다채널 영상 동시 디코딩
- **중앙 서버 MQTT 수신 (`RiskEventSource`)**: MQTT 브로커(`192.168.0.13:1883`) 구독(`forklift/risk/TERM_01`), 4개 채널 식별 및 실시간 위험도 수신
- **3단계 계층 드릴다운 탐색**: StackView 기반 `ZoneListView` → `CameraOverviewView` → `CameraGrid` → `ExpandedCameraView`
- **운영자 인증 (`AuthService`)**: PIN 해시 대조 및 5회 실패 시 30초 자동 잠금
- **안전일지 관리 (`EventLogModel`)**: 실시간 이벤트 로깅 및 CSV 파일 내보내기 지원
