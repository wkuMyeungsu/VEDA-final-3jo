#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "../models/CameraInfo.h"
#include "../models/RiskMetadata.h"
#include "AlertListModel.h"
#include "CameraListModel.h"
#include "EventLogModel.h"

class IMetadataSource;

// - 위험 메타데이터 분배기 클래스 (데이터 소스 수신 후 3개 데이터 모델 갱신 및 실시간 신호 전달)
class MetadataDistributor : public QObject
{
    Q_OBJECT

    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged) // - 데이터 소스 연결 상태 속성: 데이터 소스의 접속 상태 전달

public:
    MetadataDistributor(QVector<CameraInfo> cameras, int eventLogMaxEntries, QObject *parent = nullptr); // - 생성자: 카메라 목록 및 로그 보관 제한 수 설정

    void setSource(IMetadataSource *source);                                   // - 데이터 소스 설정: 메타데이터 수신 인터페이스 객체 연결
    void setDemoSource(IMetadataSource *source);                               // - 데모 보조 입력 설정: 주 소스는 그대로 두고 metadataReceived만 추가로 받음 (mqtt 모드에서도 데모 버튼 동작하게 하려는 용도)
    void start();                                                             // - 수신 시작: 데이터 소스 수신 개시
    void stop();                                                              // - 수신 정지: 데이터 소스 수신 중단

    CameraListModel *cameraListModel() { return &m_cameraListModel; }          // - 카메라 목록 모델 반환: 전체 카메라 상태 모델 포인터 반환
    EventLogModel *eventLogModel() { return &m_eventLogModel; }                // - 이벤트 로그 모델 반환: 이력 로그 모델 포인터 반환
    AlertListModel *alertListModel() { return &m_alertListModel; }             // - 경보 목록 모델 반환: 위험 경보 목록 모델 포인터 반환

    Q_INVOKABLE RiskMetadata latestFor(const QString &cameraId) const;         // - 최신 상태 조회: 지정 카메라의 마지막 위험 메타데이터 반환 (QML 호출 가능)
    RiskTypes::ConnectionState connectionState() const { return m_connectionState; } // - 연결 상태 조회: 현재 데이터 소스 접속 상태 반환

signals:
    void metadataUpdated(const RiskMetadata &metadata);                       // - 데이터 갱신 알림: 새 위험 메타데이터 수신 시 전달
    void connectionStateChanged();                                            // - 상태 변경 알림: 데이터 소스 접속 상태 변경 시 발생

private slots:
    void handleMetadata(const RiskMetadata &metadata);                        // - 데이터 수신 처리: 수신 메타데이터를 캐시 및 3개 모델에 분배
    void handleSourceConnectionStateChanged(RiskTypes::ConnectionState state); // - 상태 변경 처리: 데이터 소스의 연결 상태 변경 감지

private:
    QHash<QString, QString> m_cameraNames;                                     // - 카메라 이름 맵: 카메라 ID 기준 명칭 보관
    QHash<QString, RiskMetadata> m_latest;                                     // - 최신 데이터 맵: 카메라 ID별 마지막 위험 메타데이터 보관
    CameraListModel m_cameraListModel;                                         // - 전체 카메라 모델: 카메라 그리드 및 상태 보관
    EventLogModel m_eventLogModel;                                             // - 이벤트 로그 모델: 위험 및 예외 이력 저장소
    AlertListModel m_alertListModel;                                           // - 경보 목록 모델: 활성 위험 카메라 목록 보관
    IMetadataSource *m_source = nullptr;                                       // - 데이터 소스 포인터: 연결된 메타데이터 수신 객체 참조
    IMetadataSource *m_demoSource = nullptr;                                   // - 데모 보조 입력 포인터: 데모 패널 값 전용 (mqtt 모드에서만 사용, 없으면 nullptr)
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected; // - 연결 상태: 데이터 소스 접속 상태 보관
};
