#include <QtTest>

#include "config/ConfigLoader.h"

class TestConfigLoader : public QObject
{
    Q_OBJECT

private slots:
    void parsesCamerasJson();
    void skipsEntriesWithoutCameraId();
    void parsesOperatorsJson();
    void skipsOperatorEntriesWithoutIdOrHash();
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

void TestConfigLoader::parsesOperatorsJson()
{
    const QByteArray json =
        "{\n"
        "  \"operators\": [\n"
        "    {\n"
        "      \"operator_id\": \"OP01\",\n"
        "      \"display_name\": \"Operator One\",\n"
        "      \"role\": \"operator\",\n"
        "      \"pin_hash\": \"311a52147849d7656a571f38abdd5d1d6491cd78b6c38f9ac32f993d9e7517b5\"\n"
        "    },\n"
        "    {\n"
        "      \"operator_id\": \"SV01\",\n"
        "      \"display_name\": \"Supervisor One\",\n"
        "      \"role\": \"supervisor\",\n"
        "      \"pin_hash\": \"245409615fa3436e5c0b07a32208ca6e5cc003846e445c71ca7b3fc709c778de\"\n"
        "    }\n"
        "  ]\n"
        "}";

    const QVector<OperatorAccount> operators = ConfigLoader::parseOperatorsJson(json);
    QCOMPARE(operators.size(), 2);

    QCOMPARE(operators.at(0).operatorId, QStringLiteral("OP01"));
    QCOMPARE(operators.at(0).displayName, QStringLiteral("Operator One"));
    QCOMPARE(operators.at(0).role, OperatorRole::Operator);
    QCOMPARE(operators.at(0).pinHash, QStringLiteral("311a52147849d7656a571f38abdd5d1d6491cd78b6c38f9ac32f993d9e7517b5"));

    QCOMPARE(operators.at(1).operatorId, QStringLiteral("SV01"));
    QCOMPARE(operators.at(1).displayName, QStringLiteral("Supervisor One"));
    QCOMPARE(operators.at(1).role, OperatorRole::Supervisor);
    QCOMPARE(operators.at(1).pinHash, QStringLiteral("245409615fa3436e5c0b07a32208ca6e5cc003846e445c71ca7b3fc709c778de"));
}

void TestConfigLoader::skipsOperatorEntriesWithoutIdOrHash()
{
    const QByteArray json =
        "{\n"
        "  \"operators\": [\n"
        "    { \"display_name\": \"No ID\", \"role\": \"operator\", \"pin_hash\": \"abc\" },\n"
        "    { \"operator_id\": \"OP02\", \"display_name\": \"No Hash\", \"role\": \"operator\" },\n"
        "    { \"operator_id\": \"OP03\", \"display_name\": \"Valid\", \"role\": \"operator\", \"pin_hash\": \"def\" }\n"
        "  ]\n"
        "}";

    const QVector<OperatorAccount> operators = ConfigLoader::parseOperatorsJson(json);
    QCOMPARE(operators.size(), 1);
    QCOMPARE(operators.first().operatorId, QStringLiteral("OP03"));
    QCOMPARE(operators.first().displayName, QStringLiteral("Valid"));
}

QTEST_MAIN(TestConfigLoader)
#include "test_config_loader.moc"
