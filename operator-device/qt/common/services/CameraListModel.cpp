#include "CameraListModel.h"

CameraListModel::CameraListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

// beginResetModel()/endResetModel(): "목록 전체가 바뀐다"고 QML에 알리는 표준 절차
// 이 사이에서 내부 데이터(m_rows)를 마음대로 바꿔도 안전함
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
                                  RiskTypes::ExceptionState exception, double distanceM, bool distanceValid)
{
    const int row = rowForCameraId(cameraId);
    if (row < 0)
        return; // 모델에 없는 cameraId면 조용히 무시 (설정에 없는 카메라에서 이벤트가 온 경우)

    Row &target = m_rows[row];
    target.riskLevel = level;
    target.exceptionState = exception;
    target.distanceM = distanceM;
    target.distanceValid = distanceValid;

    // setCameras()와 달리 행 4개(리스크/예외/거리/거리유효성)만 바뀌었다고 콕 집어서 알림
    // -> QML이 해당 셀만 다시 그림 (카드 전체를 다시 그리는 것보다 가벼움)
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {RiskLevelRole, ExceptionStateRole, DistanceRole, DistanceValidRole});
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

// QML이 "이 행의 이 role 값 줘"라고 물어볼 때마다 호출됨 (delegate가 화면에 그릴 때마다)
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
    case DistanceValidRole: return row.distanceValid;
    case VideoConnectionStateRole: return QVariant::fromValue(row.videoConnectionState);
    default: return {};
    }
}

// Roles enum 값 <-> QML에서 쓰는 문자열 이름 매핑표
// 예: CameraGrid.qml의 "model.riskLevel"이 바로 여기 "riskLevel" 문자열과 연결됨
QHash<int, QByteArray> CameraListModel::roleNames() const
{
    return {
        {CameraIdRole, "cameraId"},
        {NameRole, "name"},
        {ZoneRole, "zone"},
        {RiskLevelRole, "riskLevel"},
        {ExceptionStateRole, "exceptionState"},
        {DistanceRole, "distanceM"},
        {DistanceValidRole, "distanceValid"},
        {VideoConnectionStateRole, "videoConnectionState"},
    };
}
