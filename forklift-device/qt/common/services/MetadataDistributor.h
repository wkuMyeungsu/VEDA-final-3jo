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

// Consumes IMetadataSource events, keeps the latest known RiskMetadata per
// camera, and fans updates out to the three QML-facing models:
// CameraListModel (grid/status), AlertListModel (active warnings), and
// EventLogModel (audit trail). Entirely UI-independent -- see
// tests/test_mock_metadata_source.cpp for how it's exercised without QML.
class MetadataDistributor : public QObject
{
    Q_OBJECT
    // 값이 바뀌면 Qt가 QML에 자동으로 알려줌 -- QML이 계속 확인 안 해도 됨
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)

public:
    // cameras: 시작 시 모델에 채울 카메라 목록 (CameraListModel 초기화용)
    // eventLogMaxEntries: EventLogModel 최대 보관 건수 (이 앱에선 값만 넘기고 실사용 안 함)
    MetadataDistributor(QVector<CameraInfo> cameras, int eventLogMaxEntries, QObject *parent = nullptr);

    // source의 소유권은 호출한 쪽이 계속 가짐 -- MetadataDistributor는 연결(connect)만 함
    // (delete는 안 함 -- main.cpp에서 스택 변수로 관리)
    void setSource(IMetadataSource *source);
    void start();
    void stop();

    // 세 모델을 포인터로 노출 -- main.cpp가 QML context property로 등록하는 용도
    // 소유권은 계속 MetadataDistributor가 가짐 -- 호출부는 그냥 빌려 쓰는 것
    // 이 앱(operator_terminal)에선 cameraListModel만 실제로 QML(DemoPanel.qml)에 쓰임
    // eventLogModel/alertListModel은 채워지긴 해도 main.cpp에서 context property로
    // 등록조차 안 돼있어서 QML에서 접근 불가 (control_center 전용 화면들 대상)
    CameraListModel *cameraListModel() { return &m_cameraListModel; }
    EventLogModel *eventLogModel() { return &m_eventLogModel; }
    AlertListModel *alertListModel() { return &m_alertListModel; }

    // QML에서 직접 호출 가능 (Q_INVOKABLE) -- 구현은 .cpp 참고
    Q_INVOKABLE RiskMetadata latestFor(const QString &cameraId) const;
    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    // 새 이벤트 1건마다 원본 그대로 emit
    // 이 앱에선 ActiveCameraController가 유일한 구독자 (자기가 지금 보여주는
    // 카메라의 이벤트인지는 ActiveCameraController가 자체적으로 다시 걸러냄)
    void metadataUpdated(const RiskMetadata &metadata);
    void connectionStateChanged();

private slots:
    // source(IMetadataSource)의 신호에 연결되는 내부 슬롯 -- 외부에서 직접 호출 안 함
    void handleMetadata(const RiskMetadata &metadata);
    void handleSourceConnectionStateChanged(RiskTypes::ConnectionState state);

private:
    QHash<QString, QString> m_cameraNames; // cameraId -> display name (for AlertListModel rows)
    QHash<QString, RiskMetadata> m_latest;  // cameraId -> 마지막으로 받은 이벤트 (latestFor()가 읽음)
    CameraListModel m_cameraListModel;
    EventLogModel m_eventLogModel;
    AlertListModel m_alertListModel;
    IMetadataSource *m_source = nullptr;    // 현재 연결된 데이터 출처 (Mock 또는 실서버 연동체)
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
};
