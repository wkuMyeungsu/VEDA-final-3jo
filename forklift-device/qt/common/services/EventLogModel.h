#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/RiskMetadata.h"

// 특이사항(위험/예외) 이벤트를 최대 개수만 유지하며 쌓아두는 감사 로그
// - control_center: EventLogPanel.qml이 이 모델을 그대로 표시
// - operator_terminal: 채워지긴 해도 main.cpp에서 context property로 등록 안 돼서 QML에서 안 씀
// - "무엇이 특이사항인지"는 여기서 안 정함 -- 호출하는 쪽(MetadataDistributor)이
//   이미 걸러서 addEntry()를 부름. 이 클래스는 그냥 "개수 제한 있는 순서 있는 통"
// - 다른 두 모델(CameraListModel/AlertListModel)과 달리 여기는 RiskMetadata
//   객체를 통째로 저장 -- 그래서 나중에 새 필드(예: distanceValid)가 생겨도
//   이 클래스 자체는 손 댈 필요 없이 Roles/data()에 한 줄만 추가하면 됨
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
        DistanceValidRole,
        ExceptionStateRole,
    };
    Q_ENUM(Roles)

    // maxEntries: 이 개수를 넘으면 가장 오래된 항목부터 자동 삭제 (기본 200건)
    explicit EventLogModel(int maxEntries = 200, QObject *parent = nullptr);

    // 새 이벤트 1건을 맨 앞(row 0)에 추가 -- 최신순 정렬 유지
    void addEntry(const RiskMetadata &metadata);
    void clear();

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    int m_maxEntries;
    QVector<RiskMetadata> m_entries; // row 0 = 가장 최근 이벤트
};
