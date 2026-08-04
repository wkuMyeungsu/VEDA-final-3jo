#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/Types.h"

// 현재 CAUTION 이상인 카메라만 모아두는 "경보 목록" 모델
// - control_center: AlertListView.qml이 이 모델을 그대로 표시
// - CameraListModel과 다른 점: 여기는 "위험한 카메라만" 들어있고,
//   SAFE로 돌아오면 upsert() 안에서 자동으로 목록에서 빠짐 (행 개수가 변함)
// - CameraListModel은 반대로 "카메라 전체"를 항상 고정 개수로 들고 있음
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
        DistanceValidRole,
        ExceptionStateRole,
    };
    Q_ENUM(Roles)

    explicit AlertListModel(QObject *parent = nullptr);

    // cameraId가 이미 목록에 있으면 값만 갱신(update), 없으면 새로 추가(insert)
    // 단, level이 Safe고 exception도 None이면(=더 이상 위험하지 않으면) 목록에서 제거
    // distanceValid: distanceM이 실제 측정값인지(true), 서버가 측정불가로 보낸건지(false)
    void upsert(const QString &cameraId, const QString &name, const QString &zone, RiskTypes::RiskLevel level,
                double distanceM, bool distanceValid, RiskTypes::ExceptionState exception);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    // 항목 1개 = 경보 중인 카메라 1대. name/zone도 같이 들고 있어서
    // AlertListView.qml이 CameraListModel을 따로 조회할 필요 없음
    struct Entry {
        QString cameraId;
        QString name;
        QString zone;
        RiskTypes::RiskLevel riskLevel = RiskTypes::RiskLevel::Safe;
        double distanceM = 0.0;
        bool distanceValid = true;
        RiskTypes::ExceptionState exceptionState = RiskTypes::ExceptionState::None;
    };

    int rowForCameraId(const QString &cameraId) const;

    QVector<Entry> m_entries;
};
