#include "Types.h"

namespace RiskTypes {

QString riskLevelToString(RiskLevel level)
{
    switch (level) {
    case RiskLevel::Safe: return QStringLiteral("SAFE");
    case RiskLevel::Caution: return QStringLiteral("CAUTION");
    case RiskLevel::Danger: return QStringLiteral("DANGER");
    case RiskLevel::Emergency: return QStringLiteral("EMERGENCY");
    }
    return QStringLiteral("SAFE");
}

RiskLevel riskLevelFromInt(int value)
{
    switch (value) {
    case 1: return RiskLevel::Caution;
    case 2: return RiskLevel::Danger;
    case 3: return RiskLevel::Emergency;
    default: return RiskLevel::Safe;
    }
}

QString exceptionStateToString(ExceptionState state)
{
    switch (state) {
    case ExceptionState::None: return QStringLiteral("NONE");
    case ExceptionState::SensorFault: return QStringLiteral("SENSOR_FAULT");
    case ExceptionState::DeadReckoning: return QStringLiteral("DEAD_RECKONING");
    case ExceptionState::EmergencyImpact: return QStringLiteral("EMERGENCY_IMPACT");
    case ExceptionState::NetworkDisconnected: return QStringLiteral("NETWORK_DISCONNECTED");
    case ExceptionState::CameraDisconnected: return QStringLiteral("CAMERA_DISCONNECTED");
    case ExceptionState::UnconfirmedProximity: return QStringLiteral("UNCONFIRMED_PROXIMITY");
    }
    return QStringLiteral("NONE");
}

ExceptionState exceptionStateFromString(const QString &value)
{
    if (value == QStringLiteral("SENSOR_FAULT")) return ExceptionState::SensorFault;
    if (value == QStringLiteral("DEAD_RECKONING")) return ExceptionState::DeadReckoning;
    if (value == QStringLiteral("EMERGENCY_IMPACT")) return ExceptionState::EmergencyImpact;
    if (value == QStringLiteral("NETWORK_DISCONNECTED")) return ExceptionState::NetworkDisconnected;
    if (value == QStringLiteral("CAMERA_DISCONNECTED")) return ExceptionState::CameraDisconnected;
    if (value == QStringLiteral("UNCONFIRMED_PROXIMITY")) return ExceptionState::UnconfirmedProximity;
    return ExceptionState::None;
}

QString connectionStateToString(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected: return QStringLiteral("DISCONNECTED");
    case ConnectionState::Connecting: return QStringLiteral("CONNECTING");
    case ConnectionState::Connected: return QStringLiteral("CONNECTED");
    }
    return QStringLiteral("DISCONNECTED");
}

} // namespace RiskTypes
