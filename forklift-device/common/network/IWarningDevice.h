#pragma once

#include <QObject>

#include "../models/Types.h"

// Abstraction over the operator terminal's physical warning device
// (buzzer / light / seat vibration), to be connected via UART once
// hardware is available. NoopWarningDevice is the only implementation
// today; a future SerialWarningDevice would implement the same interface
// on top of QSerialPort -- see docs/INTEGRATION.md.
class IWarningDevice : public QObject
{
    Q_OBJECT

public:
    explicit IWarningDevice(QObject *parent = nullptr) : QObject(parent) {}
    ~IWarningDevice() override = default;

    virtual void setRiskLevel(RiskTypes::RiskLevel level) = 0;
};
