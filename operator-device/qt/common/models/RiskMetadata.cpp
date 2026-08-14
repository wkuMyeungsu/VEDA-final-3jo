#include "RiskMetadata.h"

RiskMetadata RiskMetadata::fromJson(const QJsonObject &obj)
{
    RiskMetadata meta;
    meta.setStreamId(obj.value(QStringLiteral("stream_id")).toString());
    meta.setCameraId(obj.value(QStringLiteral("camera_id")).toString());
    meta.setChannel(obj.value(QStringLiteral("channel")).toInt(0));
    meta.setZone(obj.value(QStringLiteral("zone")).toString());
    meta.setRiskLevel(RiskTypes::riskLevelFromInt(obj.value(QStringLiteral("risk_level")).toInt()));

    if (obj.contains(QStringLiteral("distance_mm")) && obj.value(QStringLiteral("distance_mm")).isDouble()) {
        meta.setDistanceValid(true);
        meta.setDistanceM(obj.value(QStringLiteral("distance_mm")).toDouble() / 1000.0);
    } else {
        const QJsonValue distanceValue = obj.value(QStringLiteral("distance_m"));
        meta.setDistanceValid(distanceValue.isDouble());
        meta.setDistanceM(distanceValue.isDouble() ? distanceValue.toDouble() : 0.0);
    }

    meta.setExceptionState(RiskTypes::exceptionStateFromString(obj.value(QStringLiteral("exception_state")).toString()));
    meta.setUtcTime(QDateTime::fromString(obj.value(QStringLiteral("utc_time")).toString(), Qt::ISODateWithMs));
    return meta;
}

QJsonObject RiskMetadata::toJson() const
{
    QJsonObject obj;
    if (!m_streamId.isEmpty())
        obj[QStringLiteral("stream_id")] = m_streamId;
    obj[QStringLiteral("camera_id")] = m_cameraId;
    obj[QStringLiteral("channel")] = m_channel;
    obj[QStringLiteral("zone")] = m_zone;
    obj[QStringLiteral("risk_level")] = static_cast<int>(m_riskLevel);
    obj[QStringLiteral("distance_m")] = m_distanceM;
    obj[QStringLiteral("distance_mm")] = m_distanceM * 1000.0;
    obj[QStringLiteral("exception_state")] = RiskTypes::exceptionStateToString(m_exceptionState);
    obj[QStringLiteral("utc_time")] = m_utcTime.toUTC().toString(Qt::ISODateWithMs);
    return obj;
}
