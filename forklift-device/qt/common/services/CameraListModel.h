#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "../models/CameraInfo.h"
#include "../models/Types.h"

// - 전체 카메라 목록 모델 (모든 카메라의 정보 및 상태 관리, QML 화면 바인딩용 모델)
class CameraListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        CameraIdRole = Qt::UserRole + 1,                                       // - 카메라 ID 역할: 카메라 식별자
        NameRole,                                                              // - 카메라 이름 역할: 카메라 명칭
        ZoneRole,                                                              // - 설치 구역 역할: 설치 위치
        RiskLevelRole,                                                         // - 위험 단계 역할: 위험 수준 (Safe/Warning/Danger 등)
        ExceptionStateRole,                                                    // - 예외 상태 역할: 시스템 예외 상황 정보
        DistanceRole,                                                          // - 측정 거리 역할: 감지 거리(m)
        DistanceValidRole,                                                     // - 거리 유효성 역할: 거리 측정 가능 여부
        VideoConnectionStateRole,                                              // - 영상 연결 상태 역할: 영상 접속 상태
        StreamIdRole,                                                          // - 스트림 ID 역할: 채널별 스트림 식별자
        ChannelRole,                                                           // - 채널 번호 역할: 채널 인덱스
    };
    Q_ENUM(Roles)                                                              // - Roles 열거형 Qt 등록

    explicit CameraListModel(QObject *parent = nullptr);                       // - 생성자: 부모 객체 지정 및 모델 초기화

    void setCameras(const QVector<CameraInfo> &cameras);                       // - 카메라 목록 등록: 최초 카메라 설정 배열을 통해 행 목록 생성

    void updateRisk(const QString &cameraId, RiskTypes::RiskLevel level, RiskTypes::ExceptionState exception,
                     double distanceM, bool distanceValid);                    // - 위험 정보 갱신: 지정 카메라의 위험 수준, 예외 상태 및 거리값 업데이트

    void updateVideoConnectionState(const QString &cameraId, RiskTypes::ConnectionState state); // - 영상 상태 갱신: 지정 카메라의 영상 연결 상태 업데이트

    Q_INVOKABLE QString cameraIdAt(int row) const;                            // - 행 번호 기준 카메라 식별자 반환
    Q_INVOKABLE int indexForCameraId(const QString &cameraId) const;            // - 행 인덱스 조회: 카메라 ID 기준 목록 인덱스 반환 (QML 호출 가능)
    Q_INVOKABLE int videoConnectionStateFor(const QString &cameraId) const;
    Q_INVOKABLE int riskLevelFor(const QString &cameraId) const;
    Q_INVOKABLE int exceptionStateFor(const QString &cameraId) const;
    Q_INVOKABLE double distanceMFor(const QString &cameraId) const;
    Q_INVOKABLE bool distanceValidFor(const QString &cameraId) const;
    Q_INVOKABLE QString nameFor(const QString &cameraId) const;
    Q_INVOKABLE QString zoneFor(const QString &cameraId) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;    // - 행 수 조회: 전체 카메라 개수 반환
    QVariant data(const QModelIndex &index, int role) const override;          // - 데이터 조회: 지정 인덱스 및 역할의 데이터 반환
    QHash<int, QByteArray> roleNames() const override;                         // - 역할 이름 매핑: QML 속성명 매핑 테이블 반환

private:
    struct Row {                                                               // - 카메라 행 구조체 (개별 카메라의 설정 및 실시간 상태 보관)
        CameraInfo info;                                                       // - 카메라 정보: 고정 설정값 보관 (ID, 이름, 구역 등)
        RiskTypes::RiskLevel riskLevel = RiskTypes::RiskLevel::Safe;            // - 위험 단계: 실시간 위험 수준 보관
        RiskTypes::ExceptionState exceptionState = RiskTypes::ExceptionState::None; // - 예외 상태: 실시간 예외 상태 보관
        double distanceM = 0.0;                                                // - 측정 거리: 실시간 감지 거리(m) 보관
        bool distanceValid = false;                                            // - 거리 유효성: 기동 직후/미수신 시 '측정 불가/대기' 기본값
        RiskTypes::ConnectionState videoConnectionState = RiskTypes::ConnectionState::Disconnected; // - 영상 연결 상태: 실시간 영상 접속 상태 보관
    };

    int rowForCameraId(const QString &cameraId) const;                          // - 행 위치 조회: 카메라 ID 기준 목록 인덱스 검색

    QVector<Row> m_rows;                                                       // - 카메라 행 목록: 전체 카메라 상태 정보 배열 보관
};
