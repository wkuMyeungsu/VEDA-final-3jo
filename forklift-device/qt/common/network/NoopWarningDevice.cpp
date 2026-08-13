#include "NoopWarningDevice.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcWarningDevice, "safety.network.warningdevice")
}

NoopWarningDevice::NoopWarningDevice(QObject *parent)
    : IWarningDevice(parent)
{
}

void NoopWarningDevice::setRiskLevel(RiskTypes::RiskLevel level)
{
    if (m_lastLevel == level)
        return;
    m_lastLevel = level;
    qCInfo(lcWarningDevice) << "(no physical device attached) would signal risk level:"
                             << RiskTypes::riskLevelToString(level);
}

void NoopWarningDevice::sendClearError()
{
    qCInfo(lcWarningDevice) << "(no physical device attached) would send CLEAR_ERROR";
}

void NoopWarningDevice::sendSelfTest(int mode)
{
    qCInfo(lcWarningDevice) << "(no physical device attached) would send SELF_TEST mode:" << mode;
}
