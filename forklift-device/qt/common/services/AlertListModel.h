#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/Types.h"

// Cameras currently at CAUTION or above, for the control-center right
// panel. A camera drops out of this list as soon as MetadataService
// reports it back at SAFE with no exception.
class AlertListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        CameraIdRole = Qt::UserRole + 1,
        NameRole,
        ZoneRole,
        RiskLevelRole,
        DistanceRole,
        ExceptionStateRole,
    };
    Q_ENUM(Roles)

    explicit AlertListModel(QObject *parent = nullptr);

    // Adds/updates the alert for cameraId, or removes it if level is Safe
    // and exception is None.
    void upsert(const QString &cameraId, const QString &name, const QString &zone, RiskTypes::RiskLevel level,
                double distanceM, RiskTypes::ExceptionState exception);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Entry {
        QString cameraId;
        QString name;
        QString zone;
        RiskTypes::RiskLevel riskLevel = RiskTypes::RiskLevel::Safe;
        double distanceM = 0.0;
        RiskTypes::ExceptionState exceptionState = RiskTypes::ExceptionState::None;
    };

    int rowForCameraId(const QString &cameraId) const;

    QVector<Entry> m_entries;
};
