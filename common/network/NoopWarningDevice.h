#pragma once

#include "IWarningDevice.h"

// Current stand-in for the physical warning device: just logs what it
// would have done. Swap the instantiation in
// apps/operator_terminal/main.cpp for a real SerialWarningDevice once the
// UART hardware is connected.
class NoopWarningDevice : public IWarningDevice
{
    Q_OBJECT

public:
    explicit NoopWarningDevice(QObject *parent = nullptr);

    void setRiskLevel(RiskTypes::RiskLevel level) override;

private:
    RiskTypes::RiskLevel m_lastLevel = RiskTypes::RiskLevel::Safe;
};
