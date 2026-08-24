#include <QCommandLineParser>
#include <QNetworkProxyFactory>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QFontDatabase>
#include <cstdio>
#include <QDateTime>
#include <QDebug>
#include <gst/gst.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif


#include "config/ConfigLoader.h"
#include "network/MockMetadataSource.h"
#include "network/NoopWarningDevice.h"
#include "network/SerialWarningDevice.h"
#include "network/HandoverClient.h"
#include "network/RiskEventSource.h" 
#include "services/MetadataDistributor.h"
#include "services/ServerConnectionService.h"
#include "video/VideoSourceManager.h"

#include "ActiveCameraController.h"
#include "OperatorDemoController.h"

namespace {
void unifiedConsoleLogHandler(QtMsgType type, const QMessageLogContext &/*context*/, const QString &msg)
{
    const char *levelStr = "INFO";
    switch (type) {
    case QtDebugMsg:    levelStr = "DEBUG"; break;
    case QtInfoMsg:     levelStr = "INFO"; break;
    case QtWarningMsg:  levelStr = "WARN"; break;
    case QtCriticalMsg: levelStr = "ERROR"; break;
    case QtFatalMsg:    levelStr = "FATAL"; break;
    }

    const QString timeStr = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString output = QStringLiteral("[%1] [%2] %3\n").arg(timeStr, QString::fromLatin1(levelStr), msg);

    if (type == QtCriticalMsg || type == QtFatalMsg) {
        std::fputs(output.toLocal8Bit().constData(), stderr);
        std::fflush(stderr);
    } else {
        std::fputs(output.toLocal8Bit().constData(), stdout);
        std::fflush(stdout);
    }
}
} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }
#endif
    qInstallMessageHandler(unifiedConsoleLogHandler);                             // - 콘솔 표준 로그 핸들러 설치
    gst_init(&argc, &argv);                                                       // - GStreamer 초기화: RTSP 영상 수신 라이브러리 초기화

   
    QGuiApplication app(argc, argv);                                             // - 애플리케이션 생성: Qt GUI 애플리케이션 객체 생성      
    QNetworkProxyFactory::setUseSystemConfiguration(false);                      // - 시스템 프록시 자동 탐색 끄기: 조회 중 멈추는 문제 회피, 내부망 직접 접속이라 프록시 불필요

    // Pretendard 폰트 동적 로드
    QString fontsDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fonts"));
    if (QDir(fontsDir).exists()) {
        const QStringList fontFiles = {
            QStringLiteral("Pretendard-Regular.otf"),
            QStringLiteral("Pretendard-Medium.otf"),
            QStringLiteral("Pretendard-SemiBold.otf"),
            QStringLiteral("Pretendard-Bold.otf")
        };
        for (const QString &fontFile : fontFiles) {
            QString fontPath = QDir(fontsDir).filePath(fontFile);
            int fontId = QFontDatabase::addApplicationFont(fontPath);
            if (fontId != -1) {
                qInfo() << "Loaded font:" << fontFile;
            }
        }
    }

    QFont appFont(QStringLiteral("Pretendard"), 10);
    appFont.setFamilies({QStringLiteral("Pretendard"), QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI"), QStringLiteral("Malgun Gothic"), QStringLiteral("sans-serif")});
    appFont.setStyleStrategy(QFont::PreferAntialias);
    appFont.setHintingPreference(QFont::PreferFullHinting);
    QGuiApplication::setFont(appFont);

    QGuiApplication::setApplicationName(QStringLiteral("ForkliftSafetyOperatorTerminal")); // - 앱 이름 설정: 시스템 식별용 이름 지정
    QGuiApplication::setOrganizationName(QStringLiteral("ForkliftSafety"));       // - 조직 이름 설정: 시스템 조직명 지정

    // QtQuick Controls는 기본 플랫폼 스타일을 쓰기 때문에 다크 테마 화면 위에서
    // Switch/ComboBox/Button만 밝게 붕 떠 보임 -- 팔레트를 실제로 반영하는 "Basic"
    // 스타일로 고정하고, qml/theme/Theme.qml과 같은 색으로 QPalette를 채워서 QML에서
    // 개별 재정의를 안 한 컨트롤도 기본값부터 다크 톤을 따르게 함
    QQuickStyle::setStyle(QStringLiteral("Basic"));                              // - 스타일 고정: 팔레트 미반영 플랫폼 스타일 방지
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor("#0c0e12"));                   // - Theme.colorBackground
    darkPalette.setColor(QPalette::WindowText, QColor("#ffffff"));               // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Base, QColor("#16191f"));                     // - Theme.colorSurface
    darkPalette.setColor(QPalette::AlternateBase, QColor("#20252e"));            // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::Text, QColor("#ffffff"));                     // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Button, QColor("#20252e"));                   // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::ButtonText, QColor("#ffffff"));               // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Light, QColor("#4b5563"));                    // - Theme.colorBorderStrong
    darkPalette.setColor(QPalette::Midlight, QColor("#20252e"));                 // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::Dark, QColor("#374151"));                     // - Theme.colorBorderStrong
    darkPalette.setColor(QPalette::Mid, QColor("#1f2937"));                      // - Theme.colorBorder
    darkPalette.setColor(QPalette::Highlight, QColor("#F37321"));                // - Theme.colorAccent (Hanwha Orange)
    darkPalette.setColor(QPalette::HighlightedText, QColor("#ffffff"));          // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::PlaceholderText, QColor("#9ca3af"));          // - Theme.colorTextMuted
    QGuiApplication::setPalette(darkPalette);                                    // - 적용: 앱 전역 기본 팔레트로 설정

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
    RiskEventSource riskEventSource(appConfig.mqttBrokerHost, appConfig.mqttBrokerPort, appConfig.terminalId); // - 실제 서버 데이터 소스 생성: MQTT 통신 수신 객체
    IMetadataSource *activeMetadataSource = &mockMetadataSource;                 // - 활성 데이터 소스 지정: 기본값으로 가상 소스 설정
    if (appConfig.metadataSourceType == QStringLiteral("mqtt"))                  // - 소스 유형 확인: 설정값이 mqtt인 경우 실제 소스로 변경
        activeMetadataSource = &riskEventSource;

    metadataDistributor.setSource(activeMetadataSource);                          // - 데이터 소스 연결: 분배기에 활성 소스 지정

    ServerConnectionService serverConnection;                                    // - 서버 연결 서비스 생성: UI 연동용 상태 관리 객체
    HandoverClient handoverClient;                                               // - 핸도버 클라이언트 생성: 서버 제어 채널 통신 객체
    QObject::connect(&handoverClient, &HandoverClient::connectionStateChanged, &serverConnection,
                     &ServerConnectionService::setConnectionState);              // - 상태 변경 신호 연결: 제어 채널 상태를 서버 연결 서비스에 전달
    
    NoopWarningDevice noopWarningDevice;                                         // - 경고 장치 생성: 테스트용 비활성 경고 장치 객체
    SerialWarningDevice serialWarningDevice(appConfig.fpgaSerialPort, appConfig.fpgaBaudRate); // - 경고 장치 생성: FPGA UART 통신 객체
    IWarningDevice *warningDevice = &noopWarningDevice;                          // - 활성 경고 장치 지정: 기본값으로 비활성 장치 설정
    if (appConfig.warningDeviceType == QStringLiteral("serial")) {               // - 장치 유형 확인: 설정값이 serial인 경우 실제 장치로 변경
        warningDevice = &serialWarningDevice;
        serialWarningDevice.start();                                            // - 포트 열기 및 watchdog 송신/무수신 감시 타이머 구동
    }

    ActiveCameraController activeCamera(cameras, &metadataDistributor, &videoManager, warningDevice); // - 활성 카메라 제어기 생성: 화면 제어 객체 초기화
    QObject::connect(&handoverClient, &HandoverClient::cameraHandoverRequested, &activeCamera,
                  &ActiveCameraController::setActiveCameraId);                   // - 핸도버 신호 연결: 카메라 전환 요청 시 활성 카메라 ID 변경

    OperatorDemoController demoController(&mockMetadataSource, &videoManager, &serverConnection, &activeCamera); // - 데모 제어기 생성: 데모 패널 동작 객체 초기화
    demoController.setDemoModeEnabled(parser.isSet(demoOption));                // - 데모 모드 설정: 실행 옵션에 따른 데모 모드 활성화
    if (parser.isSet(demoOption) && activeMetadataSource != &mockMetadataSource) { // - mqtt 모드에서도 데모 버튼이 먹히게 하려는 용도
        metadataDistributor.setDemoSource(&mockMetadataSource);                 // - 보조 입력 연결: 데모 값도 모델에 반영
        demoController.setAutoPlay(false);                                      // - 자동 재생 끄기: start() 전에 꺼야 첫 tick도 막힘
        mockMetadataSource.start();                                             // - 타이머 가동: 버튼으로 지정한 데모 상태가 계속 유지되도록
    }

    handoverClient.setTerminalId(appConfig.terminalId);
    handoverClient.connectToServer(appConfig.mqttBrokerHost, appConfig.mqttBrokerPort);
    metadataDistributor.start();
    QString initialCameraId = parser.value(cameraOption);                        // - 실행 옵션 우선: --camera 로 지정한 ID 사용
    if (initialCameraId.isEmpty())                                               // - 미지정 확인: 옵션이 없으면 설정 파일 값 사용
        initialCameraId = appConfig.defaultCameraId;
    if (!initialCameraId.isEmpty() && !videoManager.sourceFor(initialCameraId)) { // - 존재 확인: cameras.json에 없는 ID면 켤 영상 소스가 아예 없음
        qWarning() << "initial camera" << initialCameraId
                   << "is not in cameras.json - falling back to the first camera"; // - 경고 로그: 설정 불일치를 조용히 넘기지 않고 남김
        initialCameraId.clear();                                                 // - 값 비우기: 아래 대체 규칙을 타게 함
    }
    if (initialCameraId.isEmpty() && !cameras.isEmpty())                         // - 대체 지정: 그래도 비어 있으면 목록 첫 카메라로 시작
        initialCameraId = cameras.first().effectiveId();
    activeCamera.setActiveCameraId(initialCameraId);

    QQmlApplicationEngine engine;                                                // - QML 엔진 생성: UI 렌더링 엔진 초기화
    QQmlContext *ctx = engine.rootContext();                                     // - QML 컨텍스트 추출: C++ 객체 노출용 루트 컨텍스트 획득
    ctx->setContextProperty(QStringLiteral("serverConnection"), &serverConnection); // - C++ 객체 QML 노출: 서버 연결 상태 객체 전달
    ctx->setContextProperty(QStringLiteral("metadataDistributor"), &metadataDistributor); // - C++ 객체 QML 노출: 데이터 분배기 전달
    ctx->setContextProperty(QStringLiteral("activeCamera"), &activeCamera);      // - C++ 객체 QML 노출: 활성 카메라 제어기 전달
    ctx->setContextProperty(QStringLiteral("cameraListModel"), metadataDistributor.cameraListModel()); // - C++ 객체 QML 노출: 카메라 목록 모델 전달
    ctx->setContextProperty(QStringLiteral("demoController"), &demoController);  // - C++ 객체 QML 노출: 데모 제어기 전달

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [](const QUrl &url) {
            qCritical() << "QML Object creation failed for:" << url;
            QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);                // - UI 생성 실패 처리: QML 객체 생성 실패 시 앱 종료

    engine.loadFromModule("Safety.OperatorTerminal", "OperatorWindow");         // - QML 화면 로드: 메인 화면 모듈 불러오기

    if (engine.rootObjects().isEmpty()) {                                        // - 화면 로드 검증: 루트 객체 생성 실패 시 종료
        qCritical() << "Engine rootObjects is EMPTY!";
        return -1;
    }

    qDebug() << "Operator terminal successfully loaded and running.";
    return app.exec();                                                           // - 앱 이벤트 루프 실행: 메인 이벤트 루프 개시 및 실행

}
