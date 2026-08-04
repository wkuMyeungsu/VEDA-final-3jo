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

// "추가 또는 갱신"을 한 함수에서 처리 (그래서 이름이 upsert = update + insert)
// 처리 순서: 1) 안전해졌으면 제거  2) 이미 있으면 갱신  3) 없으면 새로 추가
void AlertListModel::upsert(const QString &cameraId, const QString &name, const QString &zone,
                             RiskTypes::RiskLevel level, double distanceM, bool distanceValid,
                             RiskTypes::ExceptionState exception)
{
    // SAFE + 예외없음 = 더 이상 경보 대상 아님
    const bool shouldBeVisible = level != RiskTypes::RiskLevel::Safe || exception != RiskTypes::ExceptionState::None;
    const int row = rowForCameraId(cameraId);

    // 1) 안전해졌는데 목록에 있었다면 -> 제거
    //    beginRemoveRows/endRemoveRows: "이 행이 사라진다"고 QML에 알리는 표준 절차
    //    (이거 없이 그냥 지우면 QML이 이미 없는 행을 그리려다 죽을 수 있음)
    if (!shouldBeVisible) {
        if (row >= 0) {
            beginRemoveRows(QModelIndex(), row, row);
            m_entries.removeAt(row);
            endRemoveRows();
        }
        return;
    }

    // 2) 이미 목록에 있는 카메라 -> 값만 덮어쓰고 끝 (순서는 그대로 유지)
    if (row >= 0) {
        Entry &entry = m_entries[row];
        entry.riskLevel = level;
        entry.distanceM = distanceM;
        entry.distanceValid = distanceValid;
        entry.exceptionState = exception;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, {RiskLevelRole, DistanceRole, DistanceValidRole, ExceptionStateRole});
        return;
    }

    // 3) 처음 위험해진 카메라 -> 새 항목을 만들어 맨 앞에 추가
    //    prepend + beginInsertRows(0,0): "방금 위험해진 카메라"가 항상 목록 맨 위에 뜨게 함
    Entry entry;
    entry.cameraId = cameraId;
    entry.name = name;
    entry.zone = zone;
    entry.riskLevel = level;
    entry.distanceM = distanceM;
    entry.distanceValid = distanceValid;
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
    case DistanceValidRole: return entry.distanceValid;
    case ExceptionStateRole: return QVariant::fromValue(entry.exceptionState);
    default: return {};
    }
}

// Roles enum <-> QML 문자열 이름 매핑 (AlertListView.qml의 model.riskLevel 등)
QHash<int, QByteArray> AlertListModel::roleNames() const
{
    return {
        {CameraIdRole, "cameraId"},
        {NameRole, "name"},
        {ZoneRole, "zone"},
        {RiskLevelRole, "riskLevel"},
        {DistanceRole, "distanceM"},
        {DistanceValidRole, "distanceValid"},
        {ExceptionStateRole, "exceptionState"},
    };
}
