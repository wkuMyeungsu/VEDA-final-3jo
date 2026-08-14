#include <QtTest>

#include "network/IMetadataSource.h"
#include "services/MetadataDistributor.h"

// setDemoSource() 테스트 전용 가짜 IMetadataSource
// - start()/stop()은 아무 것도 안 함 (테스트에서 신호를 직접 쏘는 방식이라 필요 없음)
// - emitMetadata: 서버에서 이벤트 1건이 온 것처럼 흉내
// - emitConnectionState: setConnectionState()가 protected라 대신 감싸서 공개
//   (실제 소스와 동일하게 "값이 같으면 emit 안 함" 로직도 그대로 적용됨)
class FakeMetadataSource : public IMetadataSource
{
    Q_OBJECT

public:
    explicit FakeMetadataSource(QObject *parent = nullptr) : IMetadataSource(parent) {}

    void start() override {}
    void stop() override {}

    void emitMetadata(const RiskMetadata &metadata) { emit metadataReceived(metadata); }
    void emitConnectionState(RiskTypes::ConnectionState state) { setConnectionState(state); }
};

class TestDemoSource : public QObject
{
    Q_OBJECT

private slots:
    // 1) 데모 소스를 붙이면 metadataReceived가 모델까지 도달하는지 (latestFor + metadataUpdated)
    void demoSourceReachesModels();
    // 2) 핵심 안전 속성: 데모 소스의 connectionStateChanged는 connectionState()에 반영되면 안 됨
    void demoConnectionStateIgnored();
    // 3) 주 소스와 같은 객체를 데모로 또 넣으면 아무 일도 없어야 함 (주 연결이 안 끊기는지까지 확인)
    void sameAsPrimaryIsNoopAndKeepsPrimaryAlive();
    // 4) 데모 소스 교체 -> 이전 것은 더 이상 안 들어오고 새 것만 들어옴
    void replacingDemoSourceSwitchesDelivery();
};

namespace {
QVector<CameraInfo> twoCameras()
{
    CameraInfo a;
    a.cameraId = QStringLiteral("CAM_01");
    a.zone = QStringLiteral("ZONE_A");
    CameraInfo b;
    b.cameraId = QStringLiteral("CAM_02");
    b.zone = QStringLiteral("ZONE_B");
    return {a, b};
}

RiskMetadata makeMetadata(const QString &cameraId, RiskTypes::RiskLevel level)
{
    RiskMetadata meta;
    meta.setCameraId(cameraId);
    meta.setRiskLevel(level);
    meta.setDistanceM(1.0);
    meta.setDistanceValid(true);
    return meta;
}
}

void TestDemoSource::demoSourceReachesModels()
{
    MetadataDistributor distributor(twoCameras(), 50);
    FakeMetadataSource demo;
    distributor.setDemoSource(&demo);

    QSignalSpy updatedSpy(&distributor, &MetadataDistributor::metadataUpdated);

    demo.emitMetadata(makeMetadata(QStringLiteral("CAM_01"), RiskTypes::RiskLevel::Danger));

    QCOMPARE(updatedSpy.count(), 1);
    const RiskMetadata delivered = updatedSpy.at(0).at(0).value<RiskMetadata>();
    QCOMPARE(delivered.cameraId(), QStringLiteral("CAM_01"));
    QCOMPARE(static_cast<int>(delivered.riskLevel()), static_cast<int>(RiskTypes::RiskLevel::Danger));

    // latestFor()도 같이 갱신됐는지 (ExpandedCameraView.qml이 기대하는 즉시 조회 경로)
    const RiskMetadata latest = distributor.latestFor(QStringLiteral("CAM_01"));
    QCOMPARE(static_cast<int>(latest.riskLevel()), static_cast<int>(RiskTypes::RiskLevel::Danger));
}

void TestDemoSource::demoConnectionStateIgnored()
{
    MetadataDistributor distributor(twoCameras(), 50);
    FakeMetadataSource primary;
    FakeMetadataSource demo;

    distributor.setSource(&primary);
    primary.emitConnectionState(RiskTypes::ConnectionState::Connected);
    QCOMPARE(static_cast<int>(distributor.connectionState()), static_cast<int>(RiskTypes::ConnectionState::Connected));

    distributor.setDemoSource(&demo);

    QSignalSpy connSpy(&distributor, &MetadataDistributor::connectionStateChanged);
    demo.emitConnectionState(RiskTypes::ConnectionState::Connecting);
    demo.emitConnectionState(RiskTypes::ConnectionState::Disconnected);

    // 데모 소스가 연결 상태를 아무리 바꿔도 상태바 배지의 진실은 여전히 주 소스(primary)여야 함
    QCOMPARE(connSpy.count(), 0);
    QCOMPARE(static_cast<int>(distributor.connectionState()), static_cast<int>(RiskTypes::ConnectionState::Connected));
}

void TestDemoSource::sameAsPrimaryIsNoopAndKeepsPrimaryAlive()
{
    MetadataDistributor distributor(twoCameras(), 50);
    FakeMetadataSource shared;

    distributor.setSource(&shared);
    distributor.setDemoSource(&shared); // 주 소스와 동일 객체 -- 내부 disconnect가 주 경로까지 끊으면 안 됨

    QSignalSpy updatedSpy(&distributor, &MetadataDistributor::metadataUpdated);
    shared.emitMetadata(makeMetadata(QStringLiteral("CAM_01"), RiskTypes::RiskLevel::Caution));

    // setDemoSource(shared)가 실수로 disconnect(shared, ...)를 태웠다면 여기서 0건이 됨
    QCOMPARE(updatedSpy.count(), 1);
}

void TestDemoSource::replacingDemoSourceSwitchesDelivery()
{
    MetadataDistributor distributor(twoCameras(), 50);
    FakeMetadataSource demoA;
    FakeMetadataSource demoB;

    distributor.setDemoSource(&demoA);
    distributor.setDemoSource(&demoB); // demoA는 disconnect되고 demoB로 교체됨

    QSignalSpy updatedSpy(&distributor, &MetadataDistributor::metadataUpdated);

    demoA.emitMetadata(makeMetadata(QStringLiteral("CAM_01"), RiskTypes::RiskLevel::Danger));
    QCOMPARE(updatedSpy.count(), 0); // 옛 데모 소스는 더 이상 안 들어옴

    demoB.emitMetadata(makeMetadata(QStringLiteral("CAM_02"), RiskTypes::RiskLevel::Emergency));
    QCOMPARE(updatedSpy.count(), 1);
    const RiskMetadata delivered = updatedSpy.at(0).at(0).value<RiskMetadata>();
    QCOMPARE(delivered.cameraId(), QStringLiteral("CAM_02"));
}

QTEST_MAIN(TestDemoSource)
#include "test_demo_source.moc"
