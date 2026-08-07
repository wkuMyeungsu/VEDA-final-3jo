#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <gst/gst.h>

#include "config/ConfigLoader.h"
#include "network/MockMetadataSource.h"
#include "network/NoopWarningDevice.h"
#include "network/HandoverClient.h"
#include "network/RiskEventSource.h" 
#include "services/MetadataDistributor.h"
#include "services/ServerConnectionService.h"
#include "video/VideoSourceManager.h"

#include "ActiveCameraController.h"
#include "OperatorDemoController.h"

int main(int argc, char *argv[])
{
    gst_init(&argc, &argv);                                                       // - GStreamer 초기화: RTSP 영상 수신 라이브러리 초기화

    QGuiApplication app(argc, argv);                                             // - 애플리케이션 생성: Qt GUI 애플리케이션 객체 생성
    QGuiApplication::setApplicationName(QStringLiteral("ForkliftSafetyOperatorTerminal")); // - 앱 이름 설정: 시스템 식별용 이름 지정
    QGuiApplication::setOrganizationName(QStringLiteral("ForkliftSafety"));       // - 조직 이름 설정: 시스템 조직명 지정

    QCommandLineParser parser;                                                   // - 명령어 파서 생성: 실행 옵션 해석 객체 생성
    parser.setApplicationDescription(QStringLiteral("Forklift blind-spot safety - operator terminal")); // - 앱 설명 설정: 터미널 프로그램 설명 등록
    parser.addHelpOption();                                                      // - 도움말 옵션 추가: -h/--help 옵션 등록
    const QCommandLineOption demoOption(QStringLiteral("demo"),
                                         QStringLiteral("Enable the demo control panel (Ctrl+Shift+D).")); // - 데모 옵션 생성: 데모 모드 활성화 파라미터 지정
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Override the config directory."),
                                           QStringLiteral("dir"));               // - 설정 디렉토리 옵션 생성: 설정 경로 변경 파라미터 지정
    const QCommandLineOption cameraOption(QStringLiteral("camera"), QStringLiteral("Override the initial camera_id."),
                                           QStringLiteral("id"));                // - 초기 카메라 옵션 생성: 기본 카메라 ID 변경 파라미터 지정
    parser.addOption(demoOption);                                                // - 데모 옵션 등록: 파서에 옵션 추가
    parser.addOption(configOption);                                              // - 설정 경로 옵션 등록: 파서에 옵션 추가
    parser.addOption(cameraOption);                                              // - 초기 카메라 옵션 등록: 파서에 옵션 추가
    parser.process(app);                                                         // - 명령줄 해석: 전달된 실행 옵션 분석

    QString configDir = parser.value(configOption);                              // - 설정 경로 추출: 지정된 config 옵션 값 획득
    if (configDir.isEmpty())                                                     // - 경로 검증: 설정 경로 미지정 시 기본 경로 설정
        configDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config"));

    const ConfigLoader configLoader(configDir);                                  // - 설정 로더 생성: 지정 디렉토리 기준 로더 초기화
    const QVector<CameraInfo> cameras = configLoader.loadCameras();              // - 카메라 정보 로드: 카메라 설정 파일 읽기
    const TerminalConfig appConfig = configLoader.loadTerminalConfig();           // - 단말 설정 로드: 터미널 설정 파일 읽기

    VideoSourceManager videoManager;                                             // - 영상 관리자 생성: 카메라 영상 소스 제어 객체 생성
    videoManager.setCameras(cameras);                                            // - 카메라 정보 전달: 영상 관리자에 정보 설정

    MetadataDistributor metadataDistributor(cameras, 200);                        // - 메타데이터 분배기 생성: 주기적 데이터 분배 객체 초기화
    MockMetadataSource mockMetadataSource(cameras);                              // - 가상 데이터 소스 생성: 테스트용 메타데이터 생성기
    RiskEventSource riskEventSource(appConfig.serverHost, appConfig.riskPort);   // - 실제 서버 데이터 소스 생성: TCP 통신 수신 객체
    IMetadataSource *activeMetadataSource = &mockMetadataSource;                 // - 활성 데이터 소스 지정: 기본값으로 가상 소스 설정
    if (appConfig.metadataSourceType == QStringLiteral("tcp"))                   // - 소스 유형 확인: 설정값이 tcp인 경우 실제 소스로 변경
        activeMetadataSource = &riskEventSource;

    metadataDistributor.setSource(activeMetadataSource);                          // - 데이터 소스 연결: 분배기에 활성 소스 지정

    ServerConnectionService serverConnection;                                    // - 서버 연결 서비스 생성: UI 연동용 상태 관리 객체
    HandoverClient handoverClient;                                               // - 핸도버 클라이언트 생성: 서버 제어 채널 통신 객체
    QObject::connect(&handoverClient, &HandoverClient::connectionStateChanged, &serverConnection,
                     &ServerConnectionService::setConnectionState);              // - 상태 변경 신호 연결: 제어 채널 상태를 서버 연결 서비스에 전달
    
    NoopWarningDevice warningDevice;                                             // - 경고 장치 생성: 테스트용 비활성 경고 장치 객체

    ActiveCameraController activeCamera(cameras, &metadataDistributor, &videoManager, &warningDevice); // - 활성 카메라 제어기 생성: 화면 제어 객체 초기화
    QObject::connect(&handoverClient, &HandoverClient::cameraHandoverRequested, &activeCamera,
                  &ActiveCameraController::setActiveCameraId);                   // - 핸도버 신호 연결: 카메라 전환 요청 시 활성 카메라 ID 변경

    OperatorDemoController demoController(&mockMetadataSource, &videoManager, &serverConnection, &activeCamera); // - 데모 제어기 생성: 데모 패널 동작 객체 초기화
    demoController.setDemoModeEnabled(parser.isSet(demoOption));                // - 데모 모드 설정: 실행 옵션에 따른 데모 모드 활성화

    videoManager.startAll();                                                     // - 영상 소스 구동: 모든 카메라 영상 수신 시작
    handoverClient.setTerminalId(appConfig.terminalId);                          // - 단말 ID 설정: 핸도버 클라이언트에 식별자 지정
    handoverClient.connectToServer(appConfig.serverHost, appConfig.handoverPort);// - 제어 채널 접속: 서버 핸도버 포트로 연결 시도
    metadataDistributor.start();                                                 // - 데이터 분배 시작: 메타데이터 수신 및 분배 개시
    QString initialCameraId = parser.value(cameraOption);                       // - 초기 카메라 ID 추출: 실행 옵션값 확인
    if (initialCameraId.isEmpty())                                               // - 기본값 확인: 옵션 없을 경우 설정 파일의 기본 ID 사용
        initialCameraId = appConfig.defaultCameraId;
    if (initialCameraId.isEmpty() && !cameras.isEmpty())                         // - 예외 처리: 기본 ID도 없을 경우 첫 번째 카메라 ID 사용
        initialCameraId = cameras.first().cameraId;
    activeCamera.setActiveCameraId(initialCameraId);                             // - 초기 카메라 설정: 첫 표시 카메라 지정 및 활성화

    QQmlApplicationEngine engine;                                                // - QML 엔진 생성: UI 렌더링 엔진 초기화
    QQmlContext *ctx = engine.rootContext();                                     // - QML 컨텍스트 추출: C++ 객체 노출용 루트 컨텍스트 획득
    ctx->setContextProperty(QStringLiteral("serverConnection"), &serverConnection); // - C++ 객체 QML 노출: 서버 연결 상태 객체 전달
    ctx->setContextProperty(QStringLiteral("metadataDistributor"), &metadataDistributor); // - C++ 객체 QML 노출: 데이터 분배기 전달
    ctx->setContextProperty(QStringLiteral("activeCamera"), &activeCamera);      // - C++ 객체 QML 노출: 활성 카메라 제어기 전달
    ctx->setContextProperty(QStringLiteral("cameraListModel"), metadataDistributor.cameraListModel()); // - C++ 객체 QML 노출: 카메라 목록 모델 전달
    ctx->setContextProperty(QStringLiteral("demoController"), &demoController);  // - C++ 객체 QML 노출: 데모 제어기 전달

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);                // - UI 생성 실패 처리: QML 객체 생성 실패 시 앱 종료

    engine.loadFromModule("Safety.OperatorTerminal", "OperatorWindow");         // - QML 화면 로드: 메인 화면 모듈 불러오기

    if (engine.rootObjects().isEmpty())                                          // - 화면 로드 검증: 루트 객체 생성 실패 시 종료
        return -1;

    return app.exec();                                                           // - 앱 이벤트 루프 실행: 메인 이벤트 루프 개시 및 실행
}
