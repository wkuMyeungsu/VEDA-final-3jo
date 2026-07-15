#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <qqmlintegration.h>

#include "BBox.h"
#include "Types.h"

// One risk-detection event for a single camera, matching the metadata
// payload documented in docs/INTEGRATION.md. Produced by IMetadataSource
// implementations (Mock now, Tcp/WebSocket later) and consumed by
// MetadataService.
class RiskMetadata
{
    Q_GADGET
    QML_VALUE_TYPE(riskMetadata)
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId)
    Q_PROPERTY(QString zone READ zone WRITE setZone)
    Q_PROPERTY(RiskTypes::RiskLevel riskLevel READ riskLevel WRITE setRiskLevel)
    Q_PROPERTY(double distanceM READ distanceM WRITE setDistanceM)
    Q_PROPERTY(BBox personBBox READ personBBox WRITE setPersonBBox)
    Q_PROPERTY(BBox forkliftBBox READ forkliftBBox WRITE setForkliftBBox)
    Q_PROPERTY(RiskTypes::ExceptionState exceptionState READ exceptionState WRITE setExceptionState)
    Q_PROPERTY(QDateTime utcTime READ utcTime WRITE setUtcTime)

public:
    RiskMetadata() = default;

    QString cameraId() const { return m_cameraId; }
    void setCameraId(const QString &id) { m_cameraId = id; }

    QString zone() const { return m_zone; }
    void setZone(const QString &zone) { m_zone = zone; }

    RiskTypes::RiskLevel riskLevel() const { return m_riskLevel; }
    void setRiskLevel(RiskTypes::RiskLevel level) { m_riskLevel = level; }

    double distanceM() const { return m_distanceM; }
    void setDistanceM(double distance) { m_distanceM = distance; }

    BBox personBBox() const { return m_personBBox; }
    void setPersonBBox(const BBox &box) { m_personBBox = box; }

    BBox forkliftBBox() const { return m_forkliftBBox; }
    void setForkliftBBox(const BBox &box) { m_forkliftBBox = box; }

    RiskTypes::ExceptionState exceptionState() const { return m_exceptionState; }
    void setExceptionState(RiskTypes::ExceptionState state) { m_exceptionState = state; }

    QDateTime utcTime() const { return m_utcTime; }
    void setUtcTime(const QDateTime &time) { m_utcTime = time; }

    static RiskMetadata fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

private:
    QString m_cameraId;
    QString m_zone;
    RiskTypes::RiskLevel m_riskLevel = RiskTypes::RiskLevel::Safe;
    double m_distanceM = 0.0;
    BBox m_personBBox;
    BBox m_forkliftBBox;
    RiskTypes::ExceptionState m_exceptionState = RiskTypes::ExceptionState::None;
    QDateTime m_utcTime;
};

Q_DECLARE_METATYPE(RiskMetadata)
