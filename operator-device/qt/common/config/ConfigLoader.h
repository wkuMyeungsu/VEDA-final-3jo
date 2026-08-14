#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "../models/CameraInfo.h"
#include "../models/OperatorAccount.h"
#include "ConfigTypes.h"

// Loads cameras.json / control_center.json / terminal.json / operators.json
class ConfigLoader
{
public:
    explicit ConfigLoader(QString configDir);

    QVector<CameraInfo> loadCameras() const;
    ControlCenterConfig loadControlCenterConfig() const;
    TerminalConfig loadTerminalConfig() const;
    QVector<OperatorAccount> loadOperators() const;

    // Exposed separately so tests can feed in JSON bytes directly without
    // touching the filesystem.
    static QVector<CameraInfo> parseCamerasJson(const QByteArray &jsonData);
    static QVector<OperatorAccount> parseOperatorsJson(const QByteArray &jsonData);

    QString configDir() const { return m_configDir; }

private:
    QByteArray readFile(const QString &fileName) const;

    QString m_configDir;
};
