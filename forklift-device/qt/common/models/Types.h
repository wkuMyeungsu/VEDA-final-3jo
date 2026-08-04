#pragma once

#include <QObject>
#include <qqmlintegration.h>

// Shared enums exposed to both C++ and QML. Grouped under a single
// namespace so QML can reference e.g. RiskTypes.Danger, RiskTypes.Caution.
namespace RiskTypes {
Q_NAMESPACE
QML_ELEMENT

// Risk level as defined by the perception/metadata pipeline.
enum class RiskLevel {
    Safe = 0,
    Caution = 1,
    Danger = 2,
    Emergency = 3
};
Q_ENUM_NS(RiskLevel)

// Exceptional conditions reported alongside (or instead of) a risk level.
enum class ExceptionState {
    None,
    SensorFault,
    DeadReckoning,
    EmergencyImpact,
    NetworkDisconnected,
    CameraDisconnected,
    UnconfirmedProximity
};
Q_ENUM_NS(ExceptionState)

// Generic connection state used for server, camera, and sensor links.
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected
};
Q_ENUM_NS(ConnectionState)

QString riskLevelToString(RiskLevel level);
RiskLevel riskLevelFromInt(int value);

QString exceptionStateToString(ExceptionState state);
ExceptionState exceptionStateFromString(const QString &value);

QString connectionStateToString(ConnectionState state);

} // namespace RiskTypes
