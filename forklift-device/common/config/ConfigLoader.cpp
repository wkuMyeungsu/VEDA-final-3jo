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
        info.cameraId = obj.value(QStringLiteral("camera_id")).toString();
        info.name = obj.value(QStringLiteral("name")).toString();
        info.zone = obj.value(QStringLiteral("zone")).toString();
        info.sourceType = videoSourceTypeFromString(obj.value(QStringLiteral("source_type")).toString());
        info.rtspUrl = obj.value(QStringLiteral("rtsp_url")).toString();
        info.localFilePath = obj.value(QStringLiteral("local_file_path")).toString();

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
    config.serverHost = obj.value(QStringLiteral("server_host")).toString(config.serverHost);
    config.serverPort = static_cast<quint16>(obj.value(QStringLiteral("server_port")).toInt(config.serverPort));
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
