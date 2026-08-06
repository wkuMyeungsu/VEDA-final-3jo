#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/Types.h"

// - 위험 경보 카메라 목록 모델 (위험 상태인 카메라 항목의 가변 목록 관리 및 QML 표출)
class AlertListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        CameraIdRole = Qt::UserRole + 1,                                       // - 카메라 ID 역할: 카메라 식별자
        NameRole,                                                              // - 카메라 이름 역할: 카메라 명칭
        ZoneRole,                                                              // - 설치 구역 역할: 설치 위치
        RiskLevelRole,                                                         // - 위험 단계 역할: 위험 수준 (Caution/Danger 등)
        DistanceRole,                                                          // - 측정 거리 역할: 감지 거리(m)
        DistanceValidRole,                                                     // - 거리 유효성 역할: 거리 측정 가능 여부
        ExceptionStateRole,                                                    // - 예외 상태 역할: 시스템 예외 상황 정보
    };
    Q_ENUM(Roles)                                                              // - Roles 열거형 Qt 등록

    explicit AlertListModel(QObject *parent = nullptr);                       // - 생성자: 부모 객체 지정 및 모델 초기화

    void upsert(const QString &cameraId, const QString &name, const QString &zone, RiskTypes::RiskLevel level,
                double distanceM, bool distanceValid, RiskTypes::ExceptionState exception); // - 항목 추가/갱신/제거: 위험 상태에 따른 목록 업데이트

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;    // - 행 수 조회: 현재 목록의 전체 항목 수 반환
    QVariant data(const QModelIndex &index, int role) const override;          // - 데이터 조회: 지정 인덱스 및 역할의 데이터 반환
    QHash<int, QByteArray> roleNames() const override;                         // - 역할 이름 매핑: QML 속성명 매핑 테이블 반환

private:
    struct Entry {                                                             // - 경보 항목 구조체 (개별 위험 카메라 정보 보관)
        QString cameraId;                                                      // - 카메라 ID: 카메라 식별자
        QString name;                                                          // - 카메라 이름: 화면 표시용 명칭
        QString zone;                                                          // - 설치 구역: 카메라 설치 위치
        RiskTypes::RiskLevel riskLevel = RiskTypes::RiskLevel::Safe;            // - 위험 단계: 위험 수준
        double distanceM = 0.0;                                                // - 측정 거리: 감지 거리(m)
        bool distanceValid = true;                                             // - 거리 유효성: 거리 측정 유효 여부
        RiskTypes::ExceptionState exceptionState = RiskTypes::ExceptionState::None; // - 예외 상태: 시스템 예외 상황 정보
    };

    int rowForCameraId(const QString &cameraId) const;                          // - 행 위치 조회: 카메라 ID 기준 목록 인덱스 검색

    QVector<Entry> m_entries;                                                  // - 경보 항목 목록: 위험 카메라 정보 배열 보관
};
