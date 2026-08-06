#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/RiskMetadata.h"

// - 이벤트 이력 로그 목록 모델 (최신순 이벤트 누적, 최대 보관 개수 제한 및 QML 표출)
class EventLogModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        UtcTimeRole = Qt::UserRole + 1,                                       // - 발생 시각 역할: 이벤트 발생 UTC 시각
        CameraIdRole,                                                         // - 카메라 ID 역할: 카메라 식별자
        ZoneRole,                                                             // - 설치 구역 역할: 설치 위치
        RiskLevelRole,                                                        // - 위험 단계 역할: 위험 수준 (Caution/Danger 등)
        DistanceRole,                                                         // - 측정 거리 역할: 감지 거리(m)
        DistanceValidRole,                                                    // - 거리 유효성 역할: 거리 측정 가능 여부
        ExceptionStateRole,                                                   // - 예외 상태 역할: 시스템 예외 상황 정보
    };
    Q_ENUM(Roles)                                                             // - Roles 열거형 Qt 등록

    explicit EventLogModel(int maxEntries = 200, QObject *parent = nullptr);  // - 생성자: 최대 보관 개수(기본 200건) 지정 및 모델 초기화

    void addEntry(const RiskMetadata &metadata);                               // - 이벤트 추가: 신규 이벤트를 최상단(row 0)에 추가 및 오래된 로그 자동 삭제
    void clear();                                                             // - 전체 삭제: 수집된 전체 이력 로그 초기화

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;   // - 행 수 조회: 현재 보관 중인 로그 개수 반환
    QVariant data(const QModelIndex &index, int role) const override;         // - 데이터 조회: 지정 인덱스 및 역할의 데이터 반환
    QHash<int, QByteArray> roleNames() const override;                        // - 역할 이름 매핑: QML 속성명 매핑 테이블 반환

private:
    int m_maxEntries;                                                          // - 최대 보관 개수: 이력 로그 최대 저장 제한 수
    QVector<RiskMetadata> m_entries;                                          // - 이벤트 이력 목록: 메타데이터 배열 보관 (0번 인덱스 = 최신 이벤트)
};
