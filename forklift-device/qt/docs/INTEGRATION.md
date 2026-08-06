# 실제 장비 연동 가이드 (운전자 단말 · forklift-device/qt)

> ⚠️ **주의 사항**
> - 본 문서는 `operator-device/qt/docs/INTEGRATION.md`와 별개의 독립 문서임.
> - 공통 로직(RTSP/서버 연동 등) 수정 시 `operator-device` 쪽 문서도 동시 갱신 필요.

Mock(시뮬레이션) 구현체를 실제 RTSP 카메라, 중앙 서버 메타데이터 스트림, 물리 경고 장치로 교체하기 위한 C++ 연동 가이드임.  
C++ 인터페이스(`IVideoSource`, `IMetadataSource`, `IWarningDevice`) 하위 구현체만 교체하므로 **UI(QML) 코드는 수정 불필요함.**

---

## 1. 카메라 RTSP 전환

- **설정 파일 변경 (`cameras.json`)**
  - 경로: `forklift-device/qt/config/cameras.json`
  - `source_type` 값을 `"mock"`에서 `"rtsp"`로 변경
  - `rtsp_url` 항목에 실제 스트림 주소 입력 (재빌드 불필요, 실행 파일 옆 `config/` 수정)

- **RTSP 영상 수신 모듈 (`RtspVideoSource.cpp`)**
  - 경로: `forklift-device/qt/common/video/RtspVideoSource.cpp` (구현 완료 상태)
  - 파이프라인: `rtspsrc ! rtph264depay ! h264parse ! avdec_h264 ! videoconvert ! appsink`
  - 자동 재연결: 버스 ERROR/EOS 감지 시 자동 재시도 동작
  - `operator-device`와 동일 구현체 공유

- **소스 관리자 (`VideoSourceManager.cpp`)**
  - 경로: `forklift-device/qt/common/video/VideoSourceManager.cpp`
  - `source_type == "rtsp"` 설정 시 `RtspVideoSource` 자동 생성 (코드 수정 불필요)

- **UI 연동 (`VideoStream`, `DetectionOverlay`)**
  - `IVideoSource` 추상 인터페이스만 참조하므로 QML 및 UI 코드 수정 불필요

---

## 2. 중앙 서버 메타데이터 스트림 연동

- **와이어 프로토콜 파싱 구현 (`RiskEventSource.cpp`)**
  - 경로: `forklift-device/qt/common/network/RiskEventSource.cpp`
  - `handleReadyRead()` 내에 메시지 수신/파싱 로직 구현
  - 수신 데이터를 `RiskMetadata::fromJson()` 규격에 맞춘 JSON 구조로 전달

- **메인 코드 소스 교체 (`main.cpp`)**
  - 경로: `forklift-device/qt/apps/operator_terminal/main.cpp`
  - 기존 코드:
    ```cpp
    MockMetadataSource metadataSource(cameras);
    metadataDistributor.setSource(&metadataSource);
    ```
  - 변경 코드:
    ```cpp
    RiskEventSource metadataSource(appConfig.serverHost, appConfig.riskPort);
    metadataDistributor.setSource(&metadataSource);
    ```
  - `MetadataDistributor`, 모델류, QML은 `IMetadataSource` 인터페이스만 참조하므로 추가 수정 불필요

- **WebSocket 방식 연동 시 (선택 사항)**
  - `Qt6::WebSockets` 모듈 활용
  - `IMetadataSource`를 상속받는 `WebSocketMetadataSource` 신규 클래스 구현 및 적용

---

## 3. 서버 제어 채널 (핸드오버 등)

- **제어 클라이언트 스켈레톤 (`HandoverClient`)**
  - 경로: `forklift-device/qt/common/network/HandoverClient.h/.cpp`
  - 서버 프로토콜 확정 시 메시지 송수신 메서드 추가 구현

- **메인 코드 연동 (`main.cpp`)**
  - `HandoverClient` 인스턴스 생성
  - 서버로부터 신규 `camera_id` 수신 시 `activeCamera.setActiveCameraId(newCameraId)` 호출 신호 연결

---

## 4. 물리 경고 장치 (부저 / 경광등 / 진동, UART)

- **시리얼 경고 장치 구현 (`SerialWarningDevice`)**
  - `forklift-device/qt/common/network/IWarningDevice.h` 상속 클래스 구현
  - `QSerialPort` 모듈 활용 (Qt 6.11 mingw_64 kit 포함)
  - `forklift-device/qt/common/CMakeLists.txt` 내 `Qt6::SerialPort` 모듈 추가 (`find_package`, `target_link_libraries`)

- **메인 코드 경고 장치 교체 (`main.cpp`)**
  - 기존 코드:
    ```cpp
    NoopWarningDevice warningDevice;
    ```
  - 변경 코드:
    ```cpp
    SerialWarningDevice warningDevice(portName, baudRate);
    ```
  - `ActiveCameraController`에서 위험 단계 변경 시 `IWarningDevice::setRiskLevel()` 자동 호출 처리됨

---

## 5. 새 카메라 추가

- **설정 파일 항목 추가 (`cameras.json`)**
  - `cameras.json` 파일에 카메라 정보 추가만으로 자동 반영
  - `CameraListModel`, `VideoSourceManager`, `MockMetadataSource` 모두 설정 파일 기반 동적 동작 (코드 수정 불필요)
  - ⚠️ `forklift-device/qt/config/` 및 `operator-device/qt/config/` 양쪽 `cameras.json`에 동시 반영 필요
