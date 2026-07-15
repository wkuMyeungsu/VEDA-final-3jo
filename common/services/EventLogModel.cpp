#include "EventLogModel.h"

EventLogModel::EventLogModel(int maxEntries, QObject *parent)
    : QAbstractListModel(parent)
    , m_maxEntries(maxEntries)
{
}

void EventLogModel::addEntry(const RiskMetadata &metadata)
{
    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.prepend(metadata);
    endInsertRows();

    if (m_entries.size() > m_maxEntries) {
        beginRemoveRows(QModelIndex(), m_entries.size() - 1, m_entries.size() - 1);
        m_entries.removeLast();
        endRemoveRows();
    }
}

void EventLogModel::clear()
{
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

int EventLogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant EventLogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const RiskMetadata &entry = m_entries.at(index.row());
    switch (role) {
    case UtcTimeRole: return entry.utcTime();
    case CameraIdRole: return entry.cameraId();
    case ZoneRole: return entry.zone();
    case RiskLevelRole: return QVariant::fromValue(entry.riskLevel());
    case DistanceRole: return entry.distanceM();
    case ExceptionStateRole: return QVariant::fromValue(entry.exceptionState());
    default: return {};
    }
}

QHash<int, QByteArray> EventLogModel::roleNames() const
{
    return {
        {UtcTimeRole, "utcTime"},
        {CameraIdRole, "cameraId"},
        {ZoneRole, "zone"},
        {RiskLevelRole, "riskLevel"},
        {DistanceRole, "distanceM"},
        {ExceptionStateRole, "exceptionState"},
    };
}
