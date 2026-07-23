#include "CameraListModel.h"

CameraListModel::CameraListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void CameraListModel::setCameras(const QVector<CameraInfo> &cameras)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(cameras.size());
    for (const CameraInfo &info : cameras) {
        Row row;
        row.info = info;
        m_rows.append(row);
    }
    endResetModel();
}

int CameraListModel::rowForCameraId(const QString &cameraId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).info.cameraId == cameraId)
            return i;
    }
    return -1;
}

int CameraListModel::indexForCameraId(const QString &cameraId) const
{
    return rowForCameraId(cameraId);
}

void CameraListModel::updateRisk(const QString &cameraId, RiskTypes::RiskLevel level,
                                  RiskTypes::ExceptionState exception, double distanceM)
{
    const int row = rowForCameraId(cameraId);
    if (row < 0)
        return;

    Row &target = m_rows[row];
    target.riskLevel = level;
    target.exceptionState = exception;
    target.distanceM = distanceM;

    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {RiskLevelRole, ExceptionStateRole, DistanceRole});
}

void CameraListModel::updateVideoConnectionState(const QString &cameraId, RiskTypes::ConnectionState state)
{
    const int row = rowForCameraId(cameraId);
    if (row < 0)
        return;

    m_rows[row].videoConnectionState = state;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {VideoConnectionStateRole});
}

int CameraListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant CameraListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case CameraIdRole: return row.info.cameraId;
    case NameRole: return row.info.name;
    case ZoneRole: return row.info.zone;
    case RiskLevelRole: return QVariant::fromValue(row.riskLevel);
    case ExceptionStateRole: return QVariant::fromValue(row.exceptionState);
    case DistanceRole: return row.distanceM;
    case VideoConnectionStateRole: return QVariant::fromValue(row.videoConnectionState);
    default: return {};
    }
}

QHash<int, QByteArray> CameraListModel::roleNames() const
{
    return {
        {CameraIdRole, "cameraId"},
        {NameRole, "name"},
        {ZoneRole, "zone"},
        {RiskLevelRole, "riskLevel"},
        {ExceptionStateRole, "exceptionState"},
        {DistanceRole, "distanceM"},
        {VideoConnectionStateRole, "videoConnectionState"},
    };
}
