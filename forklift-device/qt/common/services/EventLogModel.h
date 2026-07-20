#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/RiskMetadata.h"

// Rolling, capped log of noteworthy risk events (time, camera, risk level,
// distance, exception) for the control-center bottom panel. Newest entry
// is row 0. MetadataService decides what counts as "noteworthy"; this
// class is just a bounded, ordered container with model roles.
class EventLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        UtcTimeRole = Qt::UserRole + 1,
        CameraIdRole,
        ZoneRole,
        RiskLevelRole,
        DistanceRole,
        ExceptionStateRole,
    };
    Q_ENUM(Roles)

    explicit EventLogModel(int maxEntries = 200, QObject *parent = nullptr);

    void addEntry(const RiskMetadata &metadata);
    void clear();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    int m_maxEntries;
    QVector<RiskMetadata> m_entries;
};
