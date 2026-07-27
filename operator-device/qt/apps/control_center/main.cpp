#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <gst/gst.h>

#include "config/ConfigLoader.h"
#include "network/MockMetadataSource.h"
#include "services/DemoController.h"
#include "services/MetadataService.h"
#include "services/ServerConnectionService.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"



int main(int argc, char *argv[])
{
    // RtspVideoSource가 GStreamer API를 쓰기 전에 반드시 한 번 필요
    gst_init(&argc, &argv); 

    // Qt GUI 앱 초기화
    QGuiApplication app(argc, argv);
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

    MetadataService metadataService(cameras, appConfig.eventLogMaxEntries);
    MockMetadataSource metadataSource(cameras);
    metadataService.setSource(&metadataSource);

    ServerConnectionService serverConnection;
    DemoController demoController(&metadataSource, &videoManager, &serverConnection);
    demoController.setDemoModeEnabled(parser.isSet(demoOption));

    // Video connection state lives on each IVideoSource, independent of the
    // metadata pipeline; wire it into CameraListModel here so the grid and
    // status list reflect the real per-camera video link.
    CameraListModel *cameraListModel = metadataService.cameraListModel();
    for (const CameraInfo &info : cameras) {
        if (IVideoSource *source = videoManager.sourceFor(info.cameraId)) {
            QObject::connect(source, &IVideoSource::connectionStateChanged, cameraListModel,
                              [cameraId = info.cameraId, cameraListModel](RiskTypes::ConnectionState state) {
                                  cameraListModel->updateVideoConnectionState(cameraId, state);
                              });
        }
    }

    videoManager.startAll();
    metadataService.start();

    QQmlApplicationEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("systemName"), appConfig.systemName);
    ctx->setContextProperty(QStringLiteral("serverConnection"), &serverConnection);
    ctx->setContextProperty(QStringLiteral("metadataService"), &metadataService);
    ctx->setContextProperty(QStringLiteral("cameraListModel"), metadataService.cameraListModel());
    ctx->setContextProperty(QStringLiteral("eventLogModel"), metadataService.eventLogModel());
    ctx->setContextProperty(QStringLiteral("alertListModel"), metadataService.alertListModel());
    ctx->setContextProperty(QStringLiteral("demoController"), &demoController);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("Safety.ControlCenter", "ControlCenterWindow");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
