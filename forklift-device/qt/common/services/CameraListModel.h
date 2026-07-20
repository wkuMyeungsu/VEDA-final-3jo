#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/CameraInfo.h"
#include "../models/Types.h"

// Backs the control-center camera grid and the right-panel camera status
// list. Holds the static config (name/zone/source type) plus the latest
// known risk state and video connection state per camera. Populated once
// from ConfigLoader, then updated in place as metadata/video events arrive
// -- never rebuilt, so QML bindings and animations stay stable.
class CameraListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        CameraIdRole = Qt::UserRole + 1,
        NameRole,
        ZoneRole,
        RiskLevelRole,
        ExceptionStateRole,
        DistanceRole,
        VideoConnectionStateRole,
    };
    Q_ENUM(Roles)

    explicit CameraListModel(QObject *parent = nullptr);

    void setCameras(const QVector<CameraInfo> &cameras);

    void updateRisk(const QString &cameraId, RiskTypes::RiskLevel level, RiskTypes::ExceptionState exception,
                     double distanceM);
    void updateVideoConnectionState(const QString &cameraId, RiskTypes::ConnectionState state);

    Q_INVOKABLE int indexForCameraId(const QString &cameraId) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Row {
        CameraInfo info;
        RiskTypes::RiskLevel riskLevel = RiskTypes::RiskLevel::Safe;
        RiskTypes::ExceptionState exceptionState = RiskTypes::ExceptionState::None;
        double distanceM = 0.0;
        RiskTypes::ConnectionState videoConnectionState = RiskTypes::ConnectionState::Disconnected;
    };

    int rowForCameraId(const QString &cameraId) const;

    QVector<Row> m_rows;
};
