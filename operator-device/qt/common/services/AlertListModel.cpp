#include "AlertListModel.h"

AlertListModel::AlertListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int AlertListModel::rowForCameraId(const QString &cameraId) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).cameraId == cameraId)
            return i;
    }
    return -1;
}

void AlertListModel::upsert(const QString &cameraId, const QString &name, const QString &zone,
                             RiskTypes::RiskLevel level, double distanceM, RiskTypes::ExceptionState exception)
{
    const bool shouldBeVisible = level != RiskTypes::RiskLevel::Safe || exception != RiskTypes::ExceptionState::None;
    const int row = rowForCameraId(cameraId);

    if (!shouldBeVisible) {
        if (row >= 0) {
            beginRemoveRows(QModelIndex(), row, row);
            m_entries.removeAt(row);
            endRemoveRows();
        }
        return;
    }

    if (row >= 0) {
        Entry &entry = m_entries[row];
        entry.riskLevel = level;
        entry.distanceM = distanceM;
        entry.exceptionState = exception;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {RiskLevelRole, DistanceRole, ExceptionStateRole});
        return;
    }

    Entry entry;
    entry.cameraId = cameraId;
    entry.name = name;
    entry.zone = zone;
    entry.riskLevel = level;
    entry.distanceM = distanceM;
    entry.exceptionState = exception;

    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.prepend(entry);
    endInsertRows();
}

int AlertListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant AlertListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case CameraIdRole: return entry.cameraId;
    case NameRole: return entry.name;
    case ZoneRole: return entry.zone;
    case RiskLevelRole: return QVariant::fromValue(entry.riskLevel);
    case DistanceRole: return entry.distanceM;
    case ExceptionStateRole: return QVariant::fromValue(entry.exceptionState);
    default: return {};
    }
}

QHash<int, QByteArray> AlertListModel::roleNames() const
{
    return {
        {CameraIdRole, "cameraId"},
        {NameRole, "name"},
        {ZoneRole, "zone"},
        {RiskLevelRole, "riskLevel"},
        {DistanceRole, "distanceM"},
        {ExceptionStateRole, "exceptionState"},
    };
}
