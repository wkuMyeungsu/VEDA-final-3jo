#include <QtTest>

#include "config/ConfigLoader.h"

class TestConfigLoader : public QObject
{
    Q_OBJECT

private slots:
    void parsesCamerasJson();
    void skipsEntriesWithoutCameraId();
};

void TestConfigLoader::parsesCamerasJson()
{
    const QByteArray json = R"({
        "cameras": [
            { "camera_id": "CAM_01", "name": "Test Cam", "zone": "ZONE_A",
              "source_type": "mock", "rtsp_url": "", "local_file_path": "" }
        ]
    })";

    const QVector<CameraInfo> cameras = ConfigLoader::parseCamerasJson(json);
    QCOMPARE(cameras.size(), 1);
    QCOMPARE(cameras.first().cameraId, QStringLiteral("CAM_01"));
    QCOMPARE(cameras.first().name, QStringLiteral("Test Cam"));
    QCOMPARE(cameras.first().zone, QStringLiteral("ZONE_A"));
    QCOMPARE(cameras.first().sourceType, VideoSourceType::Mock);
}

void TestConfigLoader::skipsEntriesWithoutCameraId()
{
    const QByteArray json = R"({
        "cameras": [
            { "name": "No ID" },
            { "camera_id": "CAM_02", "name": "Valid", "zone": "ZONE_B",
              "source_type": "rtsp", "rtsp_url": "rtsp://example.invalid/stream" }
        ]
    })";

    const QVector<CameraInfo> cameras = ConfigLoader::parseCamerasJson(json);
    QCOMPARE(cameras.size(), 1);
    QCOMPARE(cameras.first().cameraId, QStringLiteral("CAM_02"));
    QCOMPARE(cameras.first().sourceType, VideoSourceType::Rtsp);
}

QTEST_MAIN(TestConfigLoader)
#include "test_config_loader.moc"
