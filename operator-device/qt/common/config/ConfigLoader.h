#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "../models/CameraInfo.h"
#include "ConfigTypes.h"

// Loads cameras.json / control_center.json / terminal.json from a
// directory (by default, next to the executable; overridable with
// --config <dir>). Pure C++ with no QML dependency, so it can be unit
// tested directly (see tests/test_config_loader.cpp).
class ConfigLoader
{
public:
    explicit ConfigLoader(QString configDir);

    QVector<CameraInfo> loadCameras() const;
    ControlCenterConfig loadControlCenterConfig() const;
    TerminalConfig loadTerminalConfig() const;

    // Exposed separately so tests can feed in JSON bytes directly without
    // touching the filesystem.
    static QVector<CameraInfo> parseCamerasJson(const QByteArray &jsonData);

    QString configDir() const { return m_configDir; }

private:
    QByteArray readFile(const QString &fileName) const;

    QString m_configDir;
};
