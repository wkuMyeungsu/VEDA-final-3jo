# 실제 장비 연동 가이드 (관제 PC · operator-device/qt)

> ⚠️ 이 문서는 `forklift-device/qt/docs/INTEGRATION.md`와 별개의 독립 사본입니다.
> RTSP/서버 연동처럼 두 프로젝트 모두에 적용되는 로직을 고칠 때는
> forklift-device 쪽 문서도 동일하게 갱신하세요.

이 문서는 Mock 구현을 실제 RTSP 카메라 / 중앙 서버 메타데이터 스트림으로
교체할 때 정확히 어떤 파일을 수정해야 하는지 정리합니다. UI (QML)는 어떤
경우에도 수정할 필요가 없습니다 — 모든 교체 지점은 인터페이스 경계
(`IVideoSource`, `IMetadataSource`) 아래의 C++ 구현체에 있습니다.

## 1. 카메라를 RTSP로 전환

1. `operator-device/qt/config/cameras.json`에서 해당 카메라의 `source_type`을
   `"mock"` → `"rtsp"`로, `rtsp_url`을 실제 스트림 주소로 바꿉니다. (재빌드
   불필요 — 실행 파일 옆의 `config/` 디렉터리를 직접 수정하면 됩니다.)
2. `operator-device/qt/common/video/RtspVideoSource.cpp`: 구현 완료.
   - 파이프라인: `rtspsrc ! rtph264depay ! h264parse ! avdec_h264 !
     videoconvert ! appsink`
   - 재연결: 버스 ERROR/EOS 감지 시 자동 재시도
   - 실카메라 검증 완료. forklift-device와 동일 구현체 — 수정 시 양쪽 반영
3. `VideoSourceManager::createSource()`
   (`operator-device/qt/common/video/VideoSourceManager.cpp`)는 이미
   `source_type == "rtsp"`일 때 `RtspVideoSource`를 생성하도록 되어 있으므로
   수정할 필요가 없습니다.
4. `VideoStream`, `DetectionOverlay`, control_center의 QML은
   `IVideoSource` 인터페이스만 알고 있으므로 전혀 수정할 필요가 없습니다.

## 2. 중앙 서버 메타데이터 스트림 연동

1. `operator-device/qt/common/network/TcpMetadataSource.cpp`의
   `handleReadyRead()`에 실제 와이어 프로토콜 파싱을 구현합니다(현재는 소켓
   연결/해제/에러는 실제로 동작하지만 메시지 파싱은 TODO로 남아 있습니다).
   서버가 보내는 각 메시지를 `RiskMetadata::fromJson()`이 기대하는 JSON
   스키마(문서 상단 예시 참고)로 맞추면 별도 파싱 로직 없이 바로 사용할 수
   있습니다.
2. 완성되면 `operator-device/qt/apps/control_center/main.cpp`에서
   ```cpp
   MockMetadataSource metadataSource(cameras);
   metadataDistributor.setSource(&metadataSource);
   ```
   부분을
   ```cpp
   TcpMetadataSource metadataSource(appConfig.serverHost, appConfig.serverPort);
   metadataDistributor.setSource(&metadataSource);
   ```
   로 교체합니다. `MetadataDistributor`, `CameraListModel`, `EventLogModel`,
   `AlertListModel`, QML은 `IMetadataSource` 인터페이스만 사용하므로 수정할
   필요가 없습니다.
3. WebSocket을 선호한다면
   `operator-device/qt/common/network/TcpMetadataSource.cpp`를 참고해 동일한
   `IMetadataSource` 인터페이스를 구현하는 `WebSocketMetadataSource`를
   추가하면 됩니다(Qt6 WebSockets 모듈 사용).

## 3. 새 카메라 추가

`operator-device/qt/config/cameras.json`에 항목을 추가하기만 하면 됩니다.
`CameraListModel`, `VideoSourceManager`, `MockMetadataSource`, 관제 PC
그리드가 모두 설정 파일 기반으로 동작하므로 5대, 10대로 늘려도 코드 수정이
필요 없습니다(그리드 컬럼 수는 `CameraGrid.qml`에서 사용 가능한 폭에 따라
자동으로 늘어납니다).

⚠️ `cameras.json`은 `forklift-device/qt/config/`와 `operator-device/qt/config/`에
각각 독립적으로 존재합니다. 카메라 추가 시 양쪽 다 반영하세요.
