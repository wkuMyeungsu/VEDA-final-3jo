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
#include "services/MetadataDistributor.h"
#include "services/ServerConnectionService.h"
#include "video/VideoSourceManager.h"


#include "ActiveCameraController.h"
#include "OperatorDemoController.h"

int main(int argc, char *argv[])
{
    // RtspVideoSource가 GStreamer API를 쓰기 전에 반드시 한 번 필요
    gst_init(&argc, &argv);

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("ForkliftSafetyOperatorTerminal"));
    QGuiApplication::setOrganizationName(QStringLiteral("ForkliftSafety"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Forklift blind-spot safety - operator terminal"));
    parser.addHelpOption();
    const QCommandLineOption demoOption(QStringLiteral("demo"),
                                         QStringLiteral("Enable the demo control panel (Ctrl+Shift+D)."));
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Override the config directory."),
                                           QStringLiteral("dir"));
    const QCommandLineOption cameraOption(QStringLiteral("camera"), QStringLiteral("Override the initial camera_id."),
                                           QStringLiteral("id"));
    parser.addOption(demoOption);
    parser.addOption(configOption);
    parser.addOption(cameraOption);
    parser.process(app);

    QString configDir = parser.value(configOption);
    if (configDir.isEmpty())
        configDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config"));

    const ConfigLoader configLoader(configDir);
    const QVector<CameraInfo> cameras = configLoader.loadCameras();
    const TerminalConfig appConfig = configLoader.loadTerminalConfig();

    VideoSourceManager videoManager;
    videoManager.setCameras(cameras);

    MetadataDistributor metadataDistributor(cameras, 200);
    MockMetadataSource metadataSource(cameras);
    metadataDistributor.setSource(&metadataSource);

    ServerConnectionService serverConnection;
    // Stand-in for the physical warning device (buzzer/light/vibration).
    // Swap for a real SerialWarningDevice once UART hardware is connected
    // -- see docs/INTEGRATION.md.
    HandoverClient handoverClient;
    QObject::connect(&handoverClient, &HandoverClient::connectionStateChanged, &serverConnection,
                     &ServerConnectionService::setConnectionState);
    
    NoopWarningDevice warningDevice;

    ActiveCameraController activeCamera(cameras, &metadataDistributor, &videoManager, &warningDevice);
    QObject::connect(&handoverClient, &HandoverClient::cameraHandoverRequested, &activeCamera,
                  &ActiveCameraController::setActiveCameraId);

    OperatorDemoController demoController(&metadataSource, &videoManager, &serverConnection, &activeCamera);
    demoController.setDemoModeEnabled(parser.isSet(demoOption));

    videoManager.startAll();
    handoverClient.connectToServer(appConfig.serverHost, appConfig.serverPort);
    metadataDistributor.start();

    QString initialCameraId = parser.value(cameraOption);
    if (initialCameraId.isEmpty())
        initialCameraId = appConfig.defaultCameraId;
    if (initialCameraId.isEmpty() && !cameras.isEmpty())
        initialCameraId = cameras.first().cameraId;
    activeCamera.setActiveCameraId(initialCameraId);

    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("serverConnection"), &serverConnection);
    ctx->setContextProperty(QStringLiteral("metadataDistributor"), &metadataDistributor);
    ctx->setContextProperty(QStringLiteral("activeCamera"), &activeCamera);
    ctx->setContextProperty(QStringLiteral("cameraListModel"), metadataDistributor.cameraListModel());
    ctx->setContextProperty(QStringLiteral("demoController"), &demoController);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Safety.OperatorTerminal", "OperatorWindow");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
