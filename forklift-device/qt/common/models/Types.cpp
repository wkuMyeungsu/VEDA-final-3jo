#include "Types.h"

namespace RiskTypes {

QString riskLevelToString(RiskLevel level)
{
    switch (level) {
    case RiskLevel::Safe: return QStringLiteral("SAFE");                       // - 안전 단계 변환: Safe 열거형을 "SAFE" 문자열로 반환
    case RiskLevel::Caution: return QStringLiteral("CAUTION");                 // - 주의 단계 변환: Caution 열거형을 "CAUTION" 문자열로 반환
    case RiskLevel::Danger: return QStringLiteral("DANGER");                   // - 경고 단계 변환: Danger 열거형을 "DANGER" 문자열로 반환
    case RiskLevel::Emergency: return QStringLiteral("EMERGENCY");             // - 위험 단계 변환: Emergency 열거형을 "EMERGENCY" 문자열로 반환
    }
    return QStringLiteral("SAFE");                                             // - 기본값 반환: 알 수 없는 타입인 경우 "SAFE" 반환
}

RiskLevel riskLevelFromInt(int value)
{
    switch (value) {
    case 1: return RiskLevel::Caution;                                         // - 1단계 변환: 값 1을 Caution 열거형으로 반환
    case 2: return RiskLevel::Danger;                                          // - 2단계 변환: 값 2를 Danger 열거형으로 반환
    case 3: return RiskLevel::Emergency;                                       // - 3단계 변환: 값 3을 Emergency 열거형으로 반환
    default: return RiskLevel::Safe;                                           // - 기본값 변환: 그 외 값은 Safe 열거형으로 반환
    }
}

QString exceptionStateToString(ExceptionState state)
{
    switch (state) {
    case ExceptionState::None: return QStringLiteral("NONE");                  // - 정상 상태 변환: None 열거형을 "NONE" 문자열로 반환
    case ExceptionState::SensorFault: return QStringLiteral("SENSOR_FAULT");   // - 센서 오류 변환: SensorFault 열거형을 "SENSOR_FAULT" 문자열로 반환
    case ExceptionState::DeadReckoning: return QStringLiteral("DEAD_RECKONING");// - 추정 위치 변환: DeadReckoning 열거형을 "DEAD_RECKONING" 문자열로 반환
    case ExceptionState::EmergencyImpact: return QStringLiteral("EMERGENCY_IMPACT"); // - 충돌 위험 변환: EmergencyImpact 열거형을 "EMERGENCY_IMPACT" 문자열로 반환
    case ExceptionState::NetworkDisconnected: return QStringLiteral("NETWORK_DISCONNECTED"); // - 네트워크 끊김 변환: NetworkDisconnected 열거형을 "NETWORK_DISCONNECTED" 문자열로 반환
    case ExceptionState::CameraDisconnected: return QStringLiteral("CAMERA_DISCONNECTED");   // - 카메라 끊김 변환: CameraDisconnected 열거형을 "CAMERA_DISCONNECTED" 문자열로 반환
    case ExceptionState::UnconfirmedProximity: return QStringLiteral("UNCONFIRMED_PROXIMITY"); // - 미확인 근접 변환: UnconfirmedProximity 열거형을 "UNCONFIRMED_PROXIMITY" 문자열로 반환
    }
    return QStringLiteral("NONE");                                             // - 기본값 반환: 알 수 없는 상태인 경우 "NONE" 반환
}

ExceptionState exceptionStateFromString(const QString &value)
{
    if (value == QStringLiteral("SENSOR_FAULT")) return ExceptionState::SensorFault;               // - 센서 오류 비교: "SENSOR_FAULT" 문자열을 SensorFault 열거형으로 반환
    if (value == QStringLiteral("DEAD_RECKONING")) return ExceptionState::DeadReckoning;           // - 추정 위치 비교: "DEAD_RECKONING" 문자열을 DeadReckoning 열거형으로 반환
    if (value == QStringLiteral("EMERGENCY_IMPACT")) return ExceptionState::EmergencyImpact;       // - 충돌 위험 비교: "EMERGENCY_IMPACT" 문자열을 EmergencyImpact 열거형으로 반환
    if (value == QStringLiteral("NETWORK_DISCONNECTED")) return ExceptionState::NetworkDisconnected; // - 네트워크 끊김 비교: "NETWORK_DISCONNECTED" 문자열을 NetworkDisconnected 열거형으로 반환
    if (value == QStringLiteral("CAMERA_DISCONNECTED")) return ExceptionState::CameraDisconnected;   // - 카메라 끊김 비교: "CAMERA_DISCONNECTED" 문자열을 CameraDisconnected 열거형으로 반환
    if (value == QStringLiteral("UNCONFIRMED_PROXIMITY")) return ExceptionState::UnconfirmedProximity; // - 미확인 근접 비교: "UNCONFIRMED_PROXIMITY" 문자열을 UnconfirmedProximity 열거형으로 반환
    return ExceptionState::None;                                               // - 기본값 반환: 그 외 문자열인 경우 None 열거형 반환
}

QString connectionStateToString(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected: return QStringLiteral("DISCONNECTED"); // - 끊김 상태 변환: Disconnected 열거형을 "DISCONNECTED" 문자열로 반환
    case ConnectionState::Connecting: return QStringLiteral("CONNECTING");     // - 연결 중 변환: Connecting 열거형을 "CONNECTING" 문자열로 반환
    case ConnectionState::Connected: return QStringLiteral("CONNECTED");       // - 연결됨 변환: Connected 열거형을 "CONNECTED" 문자열로 반환
    }
    return QStringLiteral("DISCONNECTED");                                     // - 기본값 반환: 알 수 없는 상태인 경우 "DISCONNECTED" 반환
}

} // namespace RiskTypes
