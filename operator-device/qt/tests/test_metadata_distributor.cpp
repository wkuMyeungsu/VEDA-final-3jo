#include <QtTest>

#include "network/IMetadataSource.h"
#include "services/MetadataDistributor.h"

class DummyMetadataSource : public IMetadataSource
{
    Q_OBJECT
public:
    explicit DummyMetadataSource(QObject *parent = nullptr) : IMetadataSource(parent) {}
    void start() override {}
    void stop() override {}

    void emitMetadata(const RiskMetadata &meta) { emit metadataReceived(meta); }
    void triggerLinkLost() { emit linkLost(); }
};

class TestMetadataDistributor : public QObject
{
    Q_OBJECT

private slots:
    void linkLostFansOutToAllChannelsAndRecovers();
};

namespace {
QVector<CameraInfo> makeFourChannels()
{
    QVector<CameraInfo> cameras;
    for (int i = 1; i <= 4; ++i) {
        CameraInfo info;
        info.cameraId = QStringLiteral("CAM_01");
        info.streamId = QStringLiteral("CAM_01_CH_0%1").arg(i);
        info.channel = i - 1;
        info.zone = QStringLiteral("ZONE_A");
        info.name = QStringLiteral("CH %1").arg(i);
        cameras.append(info);
    }
    return cameras;
}
}

void TestMetadataDistributor::linkLostFansOutToAllChannelsAndRecovers()
{
    const QVector<CameraInfo> cameras = makeFourChannels();
    MetadataDistributor distributor(cameras, 50);

    DummyMetadataSource source;
    distributor.setSource(&source);

    CameraListModel *camModel = distributor.cameraListModel();
    EventLogModel *logModel = distributor.eventLogModel();

    QCOMPARE(camModel->rowCount(), 4);
    QCOMPARE(logModel->rowCount(), 0);

    // 1) 워치독 등으로 통신 두절 발생 (linkLost)
    source.triggerLinkLost();

    // 4개 채널 모두 NetworkDisconnected 상태로 변경되었는지 확인
    for (int i = 1; i <= 4; ++i) {
        const QString streamId = QStringLiteral("CAM_01_CH_0%1").arg(i);
        QCOMPARE(camModel->exceptionStateFor(streamId),
                 static_cast<int>(RiskTypes::ExceptionState::NetworkDisconnected));
    }
    // 전이 감지되어 이벤트 로그에 4개 행 적재 확인
    QCOMPARE(logModel->rowCount(), 4);

    // 1-1) 두절 상태가 지속되는 동안 워치독이 계속 triggerLinkLost()를 호출해도 중복 팬아웃 방지
    source.triggerLinkLost();
    QCOMPARE(logModel->rowCount(), 4);

    // 2) 정상 통신 복구: 서버에서 CAM_01_CH_02 1개 채널만 정상 수신
    RiskMetadata normalMeta;
    normalMeta.setStreamId(QStringLiteral("CAM_01_CH_02"));
    normalMeta.setCameraId(QStringLiteral("CAM_01"));
    normalMeta.setZone(QStringLiteral("ZONE_A"));
    normalMeta.setExceptionState(RiskTypes::ExceptionState::None);
    normalMeta.setRiskLevel(RiskTypes::RiskLevel::Safe);
    normalMeta.setUtcTime(QDateTime::currentDateTimeUtc());

    source.emitMetadata(normalMeta);

    // CH 2뿐만 아니라 나머지 비활성 채널들도 회색 두절에서 정상/대기(None)로 복구되었는지 확인
    for (int i = 1; i <= 4; ++i) {
        const QString streamId = QStringLiteral("CAM_01_CH_0%1").arg(i);
        QCOMPARE(camModel->exceptionStateFor(streamId),
                 static_cast<int>(RiskTypes::ExceptionState::None));
        // 비활성 채널은 0.00m 오표시 방지를 위해 distanceValid가 false ("측정 불가")여야 함
        if (streamId != QStringLiteral("CAM_01_CH_02")) {
            QCOMPARE(camModel->distanceValidFor(streamId), false);
        }
    }

    // 3) 2차 통신 두절 발생 시: 다시 전이 감지되어 이벤트 로그에 4개 행 추가 적재 (총 8행)
    source.triggerLinkLost();

    for (int i = 1; i <= 4; ++i) {
        const QString streamId = QStringLiteral("CAM_01_CH_0%1").arg(i);
        QCOMPARE(camModel->exceptionStateFor(streamId),
                 static_cast<int>(RiskTypes::ExceptionState::NetworkDisconnected));
    }
    QCOMPARE(logModel->rowCount(), 8);
}

QTEST_MAIN(TestMetadataDistributor)
#include "test_metadata_distributor.moc"
