#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QNetworkProxyFactory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <gst/gst.h>

#include "config/ConfigLoader.h"
#include "network/MockMetadataSource.h"
#include "network/RiskEventSource.h"
#include "services/DemoController.h"
#include "services/MetadataDistributor.h"
#include "services/ServerConnectionService.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"



int main(int argc, char *argv[])
{
    // RtspVideoSource가 GStreamer API를 쓰기 전에 반드시 한 번 필요
    gst_init(&argc, &argv); 

    // Qt GUI 앱 초기화
    QGuiApplication app(argc, argv);
    QNetworkProxyFactory::setUseSystemConfiguration(false);                      // - 시스템 프록시 자동 탐색 끄기
    // Qt 앱 이름/조직명 설정 (설정 저장 시 사용됨)
    QGuiApplication::setApplicationName(QStringLiteral("ForkliftSafetyControlCenter"));
    QGuiApplication::setOrganizationName(QStringLiteral("ForkliftSafety"));

    //
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Forklift blind-spot safety - control center"));
    parser.addHelpOption();
    const QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("Enable the demo control panel (Ctrl+Shift+D)."));
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Override the config directory."),
                                           QStringLiteral("dir"));
    parser.addOption(demoOption);
    parser.addOption(configOption);
    parser.process(app);

    QString configDir = parser.value(configOption);
    if (configDir.isEmpty())
        configDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config"));

    const ConfigLoader configLoader(configDir);
    const QVector<CameraInfo> cameras = configLoader.loadCameras();
    const ControlCenterConfig appConfig = configLoader.loadControlCenterConfig();

    VideoSourceManager videoManager;
    videoManager.setCameras(cameras);

    MetadataDistributor metadataDistributor(cameras, appConfig.eventLogMaxEntries);
    MockMetadataSource metadataSource(cameras);
    RiskEventSource riskEventSource(appConfig.mqttBrokerHost, appConfig.mqttBrokerPort, appConfig.terminalId); // - 실제 데이터 소스 생성
    IMetadataSource *activeMetadataSource = &metadataSource;                    // - 활성 소스 기본값: mock
    if (appConfig.metadataSourceType == QStringLiteral("mqtt"))                 // - mqtt 설정 시 실제 소스로 전환
        activeMetadataSource = &riskEventSource;
    metadataDistributor.setSource(activeMetadataSource);

    ServerConnectionService serverConnection;
    // - 상단 바 서버 상태 표시를 실제 수신 상태에 연결
    if (activeMetadataSource == &riskEventSource)                               // - 실통신 모드에서만 연결
        QObject::connect(&riskEventSource, &IMetadataSource::connectionStateChanged,
                         &serverConnection, &ServerConnectionService::setConnectionState);
    DemoController demoController(&metadataSource, &videoManager, &serverConnection);
    demoController.setDemoModeEnabled(parser.isSet(demoOption));

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

    videoManager.startAll();
    metadataDistributor.start();

    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("systemName"), appConfig.systemName);
    ctx->setContextProperty(QStringLiteral("serverConnection"), &serverConnection);
    ctx->setContextProperty(QStringLiteral("metadataDistributor"), &metadataDistributor);
    ctx->setContextProperty(QStringLiteral("cameraListModel"), metadataDistributor.cameraListModel());
    ctx->setContextProperty(QStringLiteral("eventLogModel"), metadataDistributor.eventLogModel());
    ctx->setContextProperty(QStringLiteral("alertListModel"), metadataDistributor.alertListModel());
    ctx->setContextProperty(QStringLiteral("demoController"), &demoController);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Safety.ControlCenter", "ControlCenterWindow");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
