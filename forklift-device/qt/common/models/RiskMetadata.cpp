#include "RiskMetadata.h"

RiskMetadata RiskMetadata::fromJson(const QJsonObject &obj)
{
    RiskMetadata meta;
    meta.setCameraId(obj.value(QStringLiteral("camera_id")).toString());
    meta.setZone(obj.value(QStringLiteral("zone")).toString());
    meta.setRiskLevel(RiskTypes::riskLevelFromInt(obj.value(QStringLiteral("risk_level")).toInt()));
    // null이면 isDouble()로 구분 (toDouble()은 null도 0.0 반환)
    const QJsonValue distanceValue = obj.value(QStringLiteral("distance_m"));
    meta.setDistanceValid(distanceValue.isDouble());
    meta.setDistanceM(distanceValue.isDouble() ? distanceValue.toDouble() : 0.0);
    meta.setExceptionState(RiskTypes::exceptionStateFromString(obj.value(QStringLiteral("exception_state")).toString()));
    meta.setUtcTime(QDateTime::fromString(obj.value(QStringLiteral("utc_time")).toString(), Qt::ISODateWithMs));
    return meta;
}

QJsonObject RiskMetadata::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("camera_id")] = m_cameraId;
    obj[QStringLiteral("zone")] = m_zone;
    obj[QStringLiteral("risk_level")] = static_cast<int>(m_riskLevel);
    obj[QStringLiteral("distance_m")] = m_distanceM;
    obj[QStringLiteral("exception_state")] = RiskTypes::exceptionStateToString(m_exceptionState);
    obj[QStringLiteral("utc_time")] = m_utcTime.toUTC().toString(Qt::ISODateWithMs);
    return obj;
}
