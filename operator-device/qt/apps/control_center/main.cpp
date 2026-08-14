#include <QCommandLineParser>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QNetworkProxyFactory>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <gst/gst.h>

#include "analysis/TtcExperiment.h"
#include "config/ConfigLoader.h"
#include "network/MockMetadataSource.h"
#include "network/RiskEventSource.h"
#include "scenario/ScenarioPlayer.h"
#include "services/AuthService.h"
#include "services/DemoController.h"
#include "services/MetadataDistributor.h"
#include "services/ServerConnectionService.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"

#include <QDateTime>
#include <QFile>
#include <QTextStream>

void customLogHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    static QFile logFile("app_debug.log");
    if (!logFile.isOpen()) {
        logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    QTextStream ts(&logFile);
    ts << "[" << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << "] "
       << (type == QtCriticalMsg ? "CRITICAL: " : (type == QtWarningMsg ? "WARNING: " : "INFO: "))
       << msg << " (" << context.file << ":" << context.line << ")\n";
    ts.flush();
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(customLogHandler);
    
    // RtspVideoSource가 GStreamer API를 쓰기 전에 반드시 한 번 필요
    gst_init(&argc, &argv); 

    // Qt GUI 앱 초기화
    QGuiApplication app(argc, argv);
    QNetworkProxyFactory::setUseSystemConfiguration(false);                      // - 시스템 프록시 자동 탐색 끄기
    // Qt 앱 이름/조직명 설정 (설정 저장 시 사용됨)
    QGuiApplication::setApplicationName(QStringLiteral("ForkliftSafetyControlCenter"));
    QGuiApplication::setOrganizationName(QStringLiteral("ForkliftSafety"));

    // QtQuick Controls는 기본적으로 플랫폼 스타일(Windows에서는 밝은 배경)을 쓰기 때문에,
    // 다크 테마 화면 위에서 Switch/ComboBox/Button만 밝게 붕 떠 보임. 팔레트를 실제로
    // 반영하는 "Basic" 스타일로 고정하고, qml/theme/Theme.qml과 같은 색으로 QPalette를
    // 채워서 QML에서 개별 재정의를 안 한 컨트롤도 기본값부터 다크 톤을 따르게 함
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor("#0b0f1a"));          // - Theme.colorBackground
    darkPalette.setColor(QPalette::WindowText, QColor("#eef1f7"));      // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Base, QColor("#141a29"));            // - Theme.colorSurface
    darkPalette.setColor(QPalette::AlternateBase, QColor("#1b2233"));   // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::Text, QColor("#eef1f7"));            // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Button, QColor("#1b2233"));          // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::ButtonText, QColor("#eef1f7"));      // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Light, QColor("#3a445c"));           // - Theme.colorBorderStrong
    darkPalette.setColor(QPalette::Midlight, QColor("#1b2233"));        // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::Dark, QColor("#3a445c"));            // - Theme.colorBorderStrong
    darkPalette.setColor(QPalette::Mid, QColor("#2a3245"));             // - Theme.colorBorder
    darkPalette.setColor(QPalette::Highlight, QColor("#4f8cf7"));       // - Theme.colorAccent
    darkPalette.setColor(QPalette::HighlightedText, QColor("#eef1f7")); // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::PlaceholderText, QColor("#6b7690")); // - Theme.colorTextMuted
    QGuiApplication::setPalette(darkPalette);

    //
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Forklift blind-spot safety - control center"));
    parser.addHelpOption();
    const QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("Enable the demo control panel (Ctrl+Shift+D)."));
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Override the config directory."),
                                           QStringLiteral("dir"));
    // - 재현 가능한 데모/회귀 테스트용: JSON 시나리오 파일을 재생하는 소스로 전환 (mock/mqtt와 상호배타적, 성공 시에만 전환)
    const QCommandLineOption scenarioOption(QStringLiteral("scenario"),
                                             QStringLiteral("Play a deterministic JSON scenario file instead of mock/mqtt data."),
                                             QStringLiteral("file"));
    parser.addOption(demoOption);
    parser.addOption(configOption);
    parser.addOption(scenarioOption);
    parser.process(app);

    QString configDir = parser.value(configOption);
    if (configDir.isEmpty())
        configDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config"));

    const ConfigLoader configLoader(configDir);
    const QVector<CameraInfo> cameras = configLoader.loadCameras();
    const ControlCenterConfig appConfig = configLoader.loadControlCenterConfig();
    const QVector<OperatorAccount> operators = configLoader.loadOperators();

    AuthService authService(operators);

    VideoSourceManager videoManager;
    videoManager.setCameras(cameras);

    MetadataDistributor metadataDistributor(cameras, appConfig.eventLogMaxEntries);
    MockMetadataSource metadataSource(cameras);
    RiskEventSource riskEventSource(appConfig.mqttBrokerHost, appConfig.mqttBrokerPort, appConfig.terminalId); // - 실제 데이터 소스 생성
    ScenarioPlayer scenarioPlayer(cameras);                                     // - 결정적 시나리오 재생 소스 (--scenario로만 활성화)
    IMetadataSource *activeMetadataSource = &metadataSource;                    // - 활성 소스 기본값: mock
    if (appConfig.metadataSourceType == QStringLiteral("mqtt"))                 // - mqtt 설정 시 실제 소스로 전환
        activeMetadataSource = &riskEventSource;

    // - --scenario는 mock/mqtt 선택과 별개로 항상 최우선 -- 파일 로드에 성공했을 때만 전환하고,
    //   실패하면 위에서 이미 정한 mock/mqtt 소스를 그대로 씀 (조용히 반쪽짜리로 재생하지 않음)
    if (parser.isSet(scenarioOption)) {
        if (scenarioPlayer.loadScenario(parser.value(scenarioOption))) {
            activeMetadataSource = &scenarioPlayer;
        } else {
            qWarning() << "failed to load scenario" << parser.value(scenarioOption) << "-"
                       << scenarioPlayer.lastErrorMessage() << "- falling back to" << appConfig.metadataSourceType;
        }
    }
    metadataDistributor.setSource(activeMetadataSource);

    ServerConnectionService serverConnection;
    // - 상단 바 서버 상태 표시를 실제 수신 상태에 연결 (mqtt 실통신 또는 시나리오 재생 모드에서만 -- mock은 항상 Connected 고정이라 배지가 의미 없음)
    if (activeMetadataSource == &riskEventSource || activeMetadataSource == &scenarioPlayer)
        QObject::connect(activeMetadataSource, &IMetadataSource::connectionStateChanged,
                         &serverConnection, &ServerConnectionService::setConnectionState);
    DemoController demoController(&metadataSource, &videoManager, &serverConnection);
    demoController.setDemoModeEnabled(parser.isSet(demoOption));
    // 데모 패널은 항상 MockMetadataSource에 값을 쓰는데, mqtt 모드에선 그 Mock이 distributor에
    // 안 붙어 있어 버튼이 조용히 무동작이었음 -- 보조 입력으로 함께 연결해서 해결
    if (parser.isSet(demoOption) && activeMetadataSource != &metadataSource) {
        metadataDistributor.setDemoSource(&metadataSource);                    // - 보조 입력 연결: 데모 값도 모델에 반영
        demoController.setAutoPlay(false);                                     // - 자동 재생 끄기: 실데이터에 랜덤 이벤트가 섞이지 않게 (start() 전에 꺼야 첫 tick도 막힘)
        metadataSource.start();                                                // - 타이머 가동: 버튼으로 지정한 데모 상태가 계속 유지되도록
    }

    // Video connection state lives on each IVideoSource, independent of the
    // metadata pipeline; wire it into CameraListModel here so the grid and
    // status list reflect the real per-camera video link.
    CameraListModel *cameraListModel = metadataDistributor.cameraListModel();
    for (const CameraInfo &info : cameras) {
        if (IVideoSource *source = videoManager.sourceFor(info.cameraId)) {
            QObject::connect(source, &IVideoSource::connectionStateChanged, cameraListModel,
                              [cameraId = info.cameraId, cameraListModel](RiskTypes::ConnectionState state) {
                                  cameraListModel->updateVideoConnectionState(cameraId, state);
                              });
        }
    }

    // TTC 예측 실험 -- metadataUpdated의 "순수 추가 구경꾼"으로만 붙임. 판정 로직
    // (handleMetadata 내부)에는 절대 안 끼어들고, 여기서 되먹임도 전혀 없음 (읽기 전용)
    TtcExperiment ttcExperiment;
    QObject::connect(&metadataDistributor, &MetadataDistributor::metadataUpdated, &ttcExperiment,
                      &TtcExperiment::handleMetadata);

    videoManager.startAll();
    metadataDistributor.start();

    const QString systemNameForDisplay = appConfig.systemName;

    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("systemName"), systemNameForDisplay);
    ctx->setContextProperty(QStringLiteral("serverConnection"), &serverConnection);
    ctx->setContextProperty(QStringLiteral("metadataDistributor"), &metadataDistributor);
    ctx->setContextProperty(QStringLiteral("cameraListModel"), metadataDistributor.cameraListModel());
    ctx->setContextProperty(QStringLiteral("eventLogModel"), metadataDistributor.eventLogModel());
    ctx->setContextProperty(QStringLiteral("alertListModel"), metadataDistributor.alertListModel());
    ctx->setContextProperty(QStringLiteral("demoController"), &demoController);
    ctx->setContextProperty(QStringLiteral("ttcExperiment"), &ttcExperiment);
    ctx->setContextProperty(QStringLiteral("authService"), &authService);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [](const QUrl &url) {
            qCritical() << "QML Object creation failed for:" << url;
            QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    engine.loadFromModule("Safety.ControlCenter", "ControlCenterWindow");

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Engine rootObjects is EMPTY!";
        return -1;
    }

    qDebug() << "QML application successfully loaded and running.";
    return app.exec();
}
