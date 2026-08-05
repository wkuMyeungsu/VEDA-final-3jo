#pragma once

#include <QString>

// Parsed contents of config/control_center.json.
struct ControlCenterConfig {
    QString systemName = QStringLiteral("Forklift Blind-Spot Safety - Control Center");
    QString serverHost = QStringLiteral("127.0.0.1");
    quint16 serverPort = 9000;
    QString metadataSourceType = QStringLiteral("mock"); // "mock" | "tcp"
    int eventLogMaxEntries = 200;
};

// Parsed contents of config/terminal.json.
struct TerminalConfig {
    QString defaultCameraId;
    QString serverHost = QStringLiteral("127.0.0.1");
    quint16 riskPort = 9000;      // 위험판정 채널 (RiskEventSource ↔ 서버 ResultPublisher)
    quint16 handoverPort = 9001;  // 핸드오버 제어 채널 (HandoverClient)
    QString metadataSourceType = QStringLiteral("mock"); // "mock" | "tcp"
};
