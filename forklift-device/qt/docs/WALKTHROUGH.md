# 지게차/관제 단말 검증 작업 완료 보고서 (Walkthrough)

**기준 일자**: 2026-08-21  
**대상 리포지토리**: `forklift-device`, `operator-device`

---

## 🚀 수행 작업 및 성과 요약

본 작업은 Notion 트래커 및 Jira KAN 프로젝트를 분석하여 도출된 **"단말 검증 작업지시서"**의 8대 실행 과제를 완벽히 이행한 결과입니다.

| 단계 | 항목 | 주요 구현 및 조치 내용 | 검증 결과 |
|:---:|:---|:---|:---:|
| **Step 1** | 자원 모니터링 스크립트 제작 | Windows PowerShell (`monitor_resources.ps1`) 및 Linux Bash (`monitor_resources.sh`) 1초 주기 CPU / RSS 메모리 샘플러 제작 | 정상 로깅 검증 완료 |
| **Step 2** | Mock 위험도 서버 완성 | TCP 9000 잔재 제거 및 .NET pure MQTT 3.1.1 기반 4대 시나리오 (`cycle`, `silence`, `stale`, `manual`) 구현. localhost 브로커 안전 검사 추가 | `192.168.0.13` 주입 차단 및 로컬 정상 발행 확인 |
| **Step 3** | RTSP 파이프라인 설정화 & 자격증명 마스킹 | `CameraInfo` / `ConfigLoader`에 `rtsp_latency_ms`, `rtsp_protocols`, `rtsp_decoder` 설정 JSON 파싱 연동. `forklift-device` 및 `operator-device` 양쪽의 GStreamer 파이프라인 생성 로그 및 파싱 오류 로그에 URL 패스워드 정규식 마스킹 (`***:***@`) 적용 | MinGW 빌드 성공 및 마스킹 검증 |
| **Step 4** | 내부 지연 계측점 (T1→T2→T3) 및 양자화 지연 분리 | `LatencyTracker` 단조 시계 기반 T1(위험 이벤트 수신), T2(`QQuickWindow::frameSwapped` 렌더링), T3(첫 100ms 송신) 계측 및 CSV 출력 구현. 100ms 주기 송신에 따른 구조적 양자화 지연(0~100ms, 평균 50ms) 분리 계측 | 클린 빌드 및 동작 연동 완료 |
| **Step 5** | 결함 주입 절차서 & FPGA 프로토콜 현행화 | `docs/FAULT_INJECTION.md` (워치독 1000ms, 부팅 유예 5초, MQTT 핸드오버, 하드웨어 래치 명시) 및 `gpio-control/src/PROTOCOL.md` (600ms heartbeat 감지 구현 상태) 현행화 | 문서 최신화 완료 |
| **Step 6** | 오류 해제 & 자가진단 UI 배선 | `ActiveCameraController`에 `clearFpgaError()`, `runFpgaSelfTest(mode)` 추가 및 데모/점검 패널(`DemoPanel.qml`)에 오류 플래그 리셋, 자가진단 4종 모드 제어, 하드웨어 비상정지 래치 안내 경고문 배선 | UI 배선 및 QML 로드 검증 |
| **Step 7** | 설계 상수 기반 경계값 단위시험 작성 | `tests/test_risk_event_source.cpp`에 Stale(999/1000/1001ms), 워치독(999/1000/1001ms), 부팅 유예(4999/5000/5001ms), 위험도 정수 변환, 예외 문자열 변환, 거리 우선순위 fallback 테스트 구현 | **CTest 6개 테스트 100% PASS** |
| **Step 8** | 포트폴리오 산출물 회고록 작성 | `docs/TROUBLESHOOTING_RETROSPECTIVE.md`에 OOM 방어, 타이머 폭주 방지, FPGA 자율 Fail-Safe, 시계 스큐 트러블슈팅, 양자화 지연 분석 등 5대 기술 심층 회고 문서 작성 | 산출물 문서화 완료 |

---

## 🔍 세부 검증 결과

### 1. 단위 테스트 (CTest) 실행 결과
```text
Test project C:/VEDA_Final_project/forklift-device/qt/build/windows-mingw
    Start 1: test_config_loader
1/6 Test #1: test_config_loader ...............   Passed    0.17 sec
    Start 2: test_mock_metadata_source
2/6 Test #2: test_mock_metadata_source ........   Passed    0.18 sec
    Start 3: test_bbox_aspect_fit
3/6 Test #3: test_bbox_aspect_fit .............   Passed    0.19 sec
    Start 4: test_metadata_distributor
4/6 Test #4: test_metadata_distributor ........   Passed    0.22 sec
    Start 5: test_serial_warning_device
5/6 Test #5: test_serial_warning_device .......   Passed    0.22 sec
    Start 6: test_risk_event_source
6/6 Test #6: test_risk_event_source ...........   Passed    0.21 sec

100% tests passed, 0 tests failed out of 6
Total Test time (real) = 1.21 sec
```

---

## 📋 사용자 수행 필요 항목 (현장/물리 기기 작업 가이드)

제미나이가 수행한 소프트웨어 구현 및 단위 테스트 완료 후, **사용자님께서 실기기(라즈베리파이/FPGA/실제 브로커) 환경에서 수행하실 잔여 작업**입니다:

1. **24시간 Soak 시험 실행 (Step 1 도구 활용)**:
   * 라즈베리파이 터미널에서 `./tools/monitor_resources.sh operator_terminal soak_24h.csv 1` 실행 후 24시간 동안 안정적인 RSS 메모리 곡선 확인.
2. **실물 결함 주입 시험 3건 실시 (Step 5 절차서 활용)**:
   * **M-01 (브로커 중단)**: mosquitto 브로커 프로세스 강제 종료 후 1초 이내 `NETWORK_DISCONNECTED` 표출 확인.
   * **M-02 (과거 Retained)**: `mock_risk_event_server.ps1 -Scenario stale` 실행 후 단말 기동 시 과거 메시지 폐기 로그 확인.
   * **M-03 (발행 정지)**: `mock_risk_event_server.ps1 -Scenario silence` 실행 후 1000ms 워치독 타임아웃 전환 확인.
3. **240fps 슬로모션 카메라 실측 (G2G 지연)**:
   * 스마트폰 240fps 슬로모션으로 화면 위험도 변경 프레임과 FPGA LED 점등 프레임 간격 촬영 및 실측값 기록.
4. **Jira 티켓 상태 업데이트**:
   * KAN-26(영상 핸드오버): 완료(Done) 처리
   * KAN-32, KAN-44, KAN-60: 실측 진행 중(In Progress) 유지
