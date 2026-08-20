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

#include "config/ConfigLoader.h"
#include "network/MockMetadataSource.h"
#include "network/RiskEventSource.h"
#include "services/AuthService.h"
#include "services/DemoController.h"
#include "services/MetadataDistributor.h"
#include "services/ServerConnectionService.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"
#include <cstdio>
#include <QDateTime>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QQuickWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

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

    FILE *f = std::fopen("C:/VEDA_Final_project/control_center_debug.log", "a");
    if (f) {
        std::fputs(output.toUtf8().constData(), f);
        std::fclose(f);
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
    qInstallMessageHandler(unifiedConsoleLogHandler);
    qDebug() << "=== Control Center Starting ===";
    
    // RtspVideoSource가 GStreamer API를 쓰기 전에 반드시 한 번 필요
    gst_init(&argc, &argv); 



    // Qt GUI 앱 초기화
    QGuiApplication app(argc, argv);
    QNetworkProxyFactory::setUseSystemConfiguration(false);                      // - 시스템 프록시 자동 탐색 끄기

    // Pretendard 모던 고딕 폰트 로드 및 전역 적용
    QString fontDir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("fonts"));
    if (!QDir(fontDir).exists()) {
        fontDir = QStringLiteral("c:/VEDA_Final_project/operator-device/qt/fonts");
    }
    QFontDatabase::addApplicationFont(fontDir + QStringLiteral("/Pretendard-Regular.otf"));
    QFontDatabase::addApplicationFont(fontDir + QStringLiteral("/Pretendard-Medium.otf"));
    QFontDatabase::addApplicationFont(fontDir + QStringLiteral("/Pretendard-SemiBold.otf"));
    QFontDatabase::addApplicationFont(fontDir + QStringLiteral("/Pretendard-Bold.otf"));

    QFont appFont(QStringLiteral("Pretendard"), 10);
    appFont.setFamilies({QStringLiteral("Pretendard"), QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI"), QStringLiteral("Malgun Gothic"), QStringLiteral("sans-serif")});
    appFont.setStyleStrategy(QFont::PreferAntialias);
    appFont.setHintingPreference(QFont::PreferFullHinting);
    QGuiApplication::setFont(appFont);

    // Qt 앱 이름/조직명 설정 (설정 저장 시 사용됨)
    QGuiApplication::setApplicationName(QStringLiteral("ForkliftSafetyControlCenter"));
    QGuiApplication::setOrganizationName(QStringLiteral("ForkliftSafety"));

    // QtQuick Controls는 기본적으로 플랫폼 스타일(Windows에서는 밝은 배경)을 쓰기 때문에,
    // 다크 테마 화면 위에서 Switch/ComboBox/Button만 밝게 붕 떠 보임. 팔레트를 실제로
    // 반영하는 "Basic" 스타일로 고정하고, qml/theme/Theme.qml과 같은 색으로 QPalette를
    // 채워서 QML에서 개별 재정의를 안 한 컨트롤도 기본값부터 다크 톤을 따르게 함
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor("#0c0e12"));          // - Theme.colorBackground
    darkPalette.setColor(QPalette::WindowText, QColor("#ffffff"));      // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Base, QColor("#16191f"));            // - Theme.colorSurface
    darkPalette.setColor(QPalette::AlternateBase, QColor("#20252e"));   // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::Text, QColor("#ffffff"));            // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Button, QColor("#20252e"));          // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::ButtonText, QColor("#ffffff"));      // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::Light, QColor("#4b5563"));           // - Theme.colorBorderStrong
    darkPalette.setColor(QPalette::Midlight, QColor("#20252e"));        // - Theme.colorSurfaceElevated
    darkPalette.setColor(QPalette::Dark, QColor("#374151"));            // - Theme.colorBorderStrong
    darkPalette.setColor(QPalette::Mid, QColor("#1f2937"));             // - Theme.colorBorder
    darkPalette.setColor(QPalette::Highlight, QColor("#F37321"));       // - Theme.colorAccent (Hanwha Orange)
    darkPalette.setColor(QPalette::HighlightedText, QColor("#ffffff")); // - Theme.colorTextPrimary
    darkPalette.setColor(QPalette::PlaceholderText, QColor("#9ca3af")); // - Theme.colorTextMuted
    QGuiApplication::setPalette(darkPalette);

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
    const QVector<OperatorAccount> operators = configLoader.loadOperators();

    AuthService authService(operators);

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
        const QString targetId = info.effectiveId();
        if (IVideoSource *source = videoManager.sourceFor(targetId)) {
            QObject::connect(source, &IVideoSource::connectionStateChanged, cameraListModel,
                              [targetId, cameraListModel](RiskTypes::ConnectionState state) {
                                  cameraListModel->updateVideoConnectionState(targetId, state);
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
    ctx->setContextProperty(QStringLiteral("authService"), &authService);

    QObject::connect(
        &engine, &QQmlApplicationEngine::warnings, &app,
        [](const QList<QQmlError> &warnings) {
            for (const auto &w : warnings) {
                qCritical() << "QML Error/Warning:" << w.toString();
            }
        });

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

#ifdef Q_OS_WIN
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        HWND hwnd = reinterpret_cast<HWND>(window->winId());
        if (hwnd) {
            // DWMWA_USE_IMMERSIVE_DARK_MODE (20)
            BOOL darkMode = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));

            // DWMWA_SYSTEMBACKDROP_TYPE (38) -> 3: DWMSBT_TRANSIENTWINDOW (Acrylic)
            DWORD backdropType = 3;
            DwmSetWindowAttribute(hwnd, 38, &backdropType, sizeof(backdropType));
        }
    }
#endif

    qDebug() << "QML application successfully loaded and running.";
    return app.exec();
}
