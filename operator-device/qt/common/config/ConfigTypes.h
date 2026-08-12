#pragma once

#include <QString>

// Parsed contents of config/control_center.json.
struct ControlCenterConfig {
    QString systemName = QStringLiteral("Forklift Blind-Spot Safety - Control Center");
    QString mqttBrokerHost = QStringLiteral("127.0.0.1"); // - MQTT 브로커 주소
    quint16 mqttBrokerPort = 1883;                        // - MQTT 브로커 포트
    QString terminalId;                                   // - 단말 식별자
    QString metadataSourceType = QStringLiteral("mock"); // "mock" | "mqtt"
    int eventLogMaxEntries = 200;
};

// Parsed contents of config/terminal.json.
struct TerminalConfig {
    QString defaultCameraId;
    QString serverHost = QStringLiteral("127.0.0.1");
    quint16 serverPort = 9000;
    QString metadataSourceType = QStringLiteral("mock"); // "mock" | "tcp"
};
