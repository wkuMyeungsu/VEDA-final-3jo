#include "RiskMetadata.h"

RiskMetadata RiskMetadata::fromJson(const QJsonObject &obj)
{
    RiskMetadata meta;
    meta.setCameraId(obj.value(QStringLiteral("camera_id")).toString());
    meta.setZone(obj.value(QStringLiteral("zone")).toString());
    meta.setRiskLevel(RiskTypes::riskLevelFromInt(obj.value(QStringLiteral("risk_level")).toInt()));
    meta.setDistanceM(obj.value(QStringLiteral("distance_m")).toDouble());
    meta.setPersonBBox(BBox::fromJson(obj.value(QStringLiteral("person_bbox")).toObject()));
    meta.setForkliftBBox(BBox::fromJson(obj.value(QStringLiteral("forklift_bbox")).toObject()));
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
    obj[QStringLiteral("person_bbox")] = m_personBBox.toJson();
    obj[QStringLiteral("forklift_bbox")] = m_forkliftBBox.toJson();
    obj[QStringLiteral("exception_state")] = RiskTypes::exceptionStateToString(m_exceptionState);
    obj[QStringLiteral("utc_time")] = m_utcTime.toUTC().toString(Qt::ISODateWithMs);
    return obj;
}
