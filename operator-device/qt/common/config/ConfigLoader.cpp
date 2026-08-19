#include "ConfigLoader.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcConfig, "safety.config")
}

ConfigLoader::ConfigLoader(QString configDir)
    : m_configDir(std::move(configDir))
{
}

QByteArray ConfigLoader::readFile(const QString &fileName) const
{
    const QString path = QDir(m_configDir).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcConfig) << "could not open config file" << path << "-" << file.errorString();
        return {};
    }
    return file.readAll();
}

QVector<CameraInfo> ConfigLoader::parseCamerasJson(const QByteArray &jsonData)
{
    QVector<CameraInfo> result;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    const QJsonArray cameras = doc.object().value(QStringLiteral("cameras")).toArray();

    for (const QJsonValue &value : cameras) {
        const QJsonObject obj = value.toObject();

        CameraInfo info;
        info.streamId = obj.value(QStringLiteral("stream_id")).toString();
        info.cameraId = obj.value(QStringLiteral("camera_id")).toString();
        info.channel = obj.value(QStringLiteral("channel")).toInt(0);
        info.name = obj.value(QStringLiteral("name")).toString();
        info.zone = obj.value(QStringLiteral("zone")).toString();
        info.sourceType = videoSourceTypeFromString(obj.value(QStringLiteral("source_type")).toString());
        info.rtspUrl = obj.value(QStringLiteral("rtsp_url")).toString();
        info.localFilePath = obj.value(QStringLiteral("local_file_path")).toString();

        // camera_id/stream_id 중 하나만 있어도 다른 쪽을 채워 서로 보완
        if (info.cameraId.isEmpty() && !info.streamId.isEmpty()) {
            info.cameraId = info.streamId;
        } else if (info.streamId.isEmpty() && !info.cameraId.isEmpty()) {
            info.streamId = info.cameraId;
        }

        if (info.cameraId.isEmpty()) {
            qCWarning(lcConfig) << "skipping camera entry with an empty camera_id";
            continue;
        }
        result.append(info);
    }
    return result;
}

QVector<CameraInfo> ConfigLoader::loadCameras() const
{
    return parseCamerasJson(readFile(QStringLiteral("cameras.json")));
}

ControlCenterConfig ConfigLoader::loadControlCenterConfig() const
{
    ControlCenterConfig config;
    const QJsonObject obj = QJsonDocument::fromJson(readFile(QStringLiteral("control_center.json"))).object();
    if (obj.isEmpty())
        return config;

    config.systemName = obj.value(QStringLiteral("system_name")).toString(config.systemName);
    config.mqttBrokerHost = obj.value(QStringLiteral("mqtt_broker_host")).toString(config.mqttBrokerHost); // - 브로커 주소 읽기
    config.mqttBrokerPort =
        static_cast<quint16>(obj.value(QStringLiteral("mqtt_broker_port")).toInt(config.mqttBrokerPort)); // - 브로커 포트 읽기
    config.terminalId = obj.value(QStringLiteral("terminal_id")).toString();  // - 단말 ID 읽기
    config.metadataSourceType =
        obj.value(QStringLiteral("metadata_source_type")).toString(config.metadataSourceType);
    config.eventLogMaxEntries = obj.value(QStringLiteral("event_log_max_entries")).toInt(config.eventLogMaxEntries);
    return config;
}

TerminalConfig ConfigLoader::loadTerminalConfig() const
{
    TerminalConfig config;
    const QJsonObject obj = QJsonDocument::fromJson(readFile(QStringLiteral("terminal.json"))).object();
    if (obj.isEmpty())
        return config;

    config.defaultCameraId = obj.value(QStringLiteral("default_camera_id")).toString();
    config.serverHost = obj.value(QStringLiteral("server_host")).toString(config.serverHost);
    config.serverPort = static_cast<quint16>(obj.value(QStringLiteral("server_port")).toInt(config.serverPort));
    config.metadataSourceType =
        obj.value(QStringLiteral("metadata_source_type")).toString(config.metadataSourceType);
    return config;
}

QVector<OperatorAccount> ConfigLoader::parseOperatorsJson(const QByteArray &jsonData)
{
    QVector<OperatorAccount> result;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    const QJsonArray operators = doc.object().value(QStringLiteral("operators")).toArray();

    for (const QJsonValue &value : operators) {
        const QJsonObject obj = value.toObject();

        OperatorAccount account;
        account.operatorId = obj.value(QStringLiteral("operator_id")).toString();
        account.displayName = obj.value(QStringLiteral("display_name")).toString();
        account.role = operatorRoleFromString(obj.value(QStringLiteral("role")).toString());
        account.pinHash = obj.value(QStringLiteral("pin_hash")).toString();

        if (account.operatorId.isEmpty() || account.pinHash.isEmpty()) {
            qCWarning(lcConfig) << "skipping operator entry with an empty operator_id or pin_hash";
            continue;
        }
        result.append(account);
    }
    return result;
}

QVector<OperatorAccount> ConfigLoader::loadOperators() const
{
    return parseOperatorsJson(readFile(QStringLiteral("operators.json")));
}
