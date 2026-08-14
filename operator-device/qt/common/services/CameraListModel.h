#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/CameraInfo.h"
#include "../models/Types.h"

// 카메라 그리드/상태 목록용 데이터 모델
// - control_center: CameraGrid.qml -> CameraCard.qml이 이 모델을 그대로 표시
// - operator_terminal: DemoPanel.qml의 카메라 선택 드롭다운에서만 씀
// - 카메라 목록 자체(이름/구역)는 setCameras()로 한 번만 채우고,
//   이후엔 updateRisk()/updateVideoConnectionState()로 행(Row) 내용만 갱신
//   (목록을 통째로 다시 안 만듦 -> QML 애니메이션/스크롤 위치가 안 끊김)
// - QAbstractListModel: Qt가 제공하는 "QML에 바로 물릴 수 있는 목록" 베이스 클래스
//   rowCount()/data()/roleNames() 세 개를 구현하면 QML ListView/GridView가
//   바로 model: cameraListModel 이런 식으로 바인딩해서 씀
class CameraListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        CameraIdRole = Qt::UserRole + 1,
        NameRole,
        ZoneRole,
        RiskLevelRole,
        ExceptionStateRole,
        DistanceRole,
        DistanceValidRole,
        VideoConnectionStateRole,
        StreamIdRole,
        ChannelRole,
    };
    Q_ENUM(Roles)

    explicit CameraListModel(QObject *parent = nullptr);

signals:
    void countChanged();
    void videoConnectionChanged(const QString &cameraId, int state);
    void riskUpdated(const QString &cameraId, int riskLevel, int exceptionState, double distanceM);

public:
    // 최초 1회 호출 (ConfigLoader가 읽은 카메라 목록으로 행을 만듦)
    void setCameras(const QVector<CameraInfo> &cameras);

    // 카메라 1대의 위험도/거리 갱신. cameraId로 기존 행을 찾아서 값만 덮어씀
    // distanceValid: distanceM이 실제 측정값인지(true), 서버가 측정불가로 보낸건지(false)
    void updateRisk(const QString &cameraId, RiskTypes::RiskLevel level, RiskTypes::ExceptionState exception,
                     double distanceM, bool distanceValid);
    // 카메라 1대의 영상 연결 상태만 갱신 (위험도와 별개 채널이라 함수 분리)
    void updateVideoConnectionState(const QString &cameraId, RiskTypes::ConnectionState state);

    Q_INVOKABLE int indexForCameraId(const QString &cameraId) const;
    Q_INVOKABLE int videoConnectionStateFor(const QString &cameraId) const;
    Q_INVOKABLE int riskLevelFor(const QString &cameraId) const;
    Q_INVOKABLE int exceptionStateFor(const QString &cameraId) const;
    Q_INVOKABLE double distanceMFor(const QString &cameraId) const;
    Q_INVOKABLE bool distanceValidFor(const QString &cameraId) const;
    Q_INVOKABLE QString nameFor(const QString &cameraId) const;
    Q_INVOKABLE QString zoneFor(const QString &cameraId) const;

    // QAbstractListModel이 요구하는 필수 구현 3종 (QML이 내부적으로 호출)
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    // 행 1개 = 카메라 1대의 현재 상태 스냅샷
    struct Row {
        CameraInfo info;                                                          // 이름/구역 등 고정값
        RiskTypes::RiskLevel riskLevel = RiskTypes::RiskLevel::Safe;               // updateRisk()가 갱신
        RiskTypes::ExceptionState exceptionState = RiskTypes::ExceptionState::None; // updateRisk()가 갱신
        double distanceM = 0.0;                                                    // updateRisk()가 갱신
        bool distanceValid = true;                                                 // updateRisk()가 갱신 (측정불가 구분용)
        RiskTypes::ConnectionState videoConnectionState = RiskTypes::ConnectionState::Disconnected; // updateVideoConnectionState()가 갱신
    };

    // cameraId로 몇 번째 행인지 찾음 (카메라 대수가 적어서 순차 탐색으로 충분)
    int rowForCameraId(const QString &cameraId) const;

    QVector<Row> m_rows;
};
