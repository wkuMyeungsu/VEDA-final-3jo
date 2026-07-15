#include <QtTest>

#include "network/MockMetadataSource.h"

class TestMockMetadataSource : public QObject
{
    Q_OBJECT

private slots:
    void riskOverrideEmitsImmediately();
    void clearOverrideStopsForcedState();
};

namespace {
QVector<CameraInfo> singleCamera()
{
    CameraInfo info;
    info.cameraId = QStringLiteral("CAM_01");
    info.zone = QStringLiteral("ZONE_A");
    return {info};
}
}

void TestMockMetadataSource::riskOverrideEmitsImmediately()
{
    MockMetadataSource source(singleCamera());
    QSignalSpy spy(&source, &MockMetadataSource::metadataReceived);

    source.setRiskOverride(QStringLiteral("CAM_01"), RiskTypes::RiskLevel::Danger, 0.75,
                            RiskTypes::ExceptionState::None);

    QCOMPARE(spy.count(), 1);
    const RiskMetadata meta = spy.at(0).at(0).value<RiskMetadata>();
    QCOMPARE(meta.cameraId(), QStringLiteral("CAM_01"));
    QCOMPARE(static_cast<int>(meta.riskLevel()), static_cast<int>(RiskTypes::RiskLevel::Danger));
    QCOMPARE(meta.distanceM(), 0.75);
    QVERIFY(meta.personBBox().isValid());
    QVERIFY(meta.forkliftBBox().isValid());
}

void TestMockMetadataSource::clearOverrideStopsForcedState()
{
    MockMetadataSource source(singleCamera());
    source.setAutoPlay(false);
    source.setRiskOverride(QStringLiteral("CAM_01"), RiskTypes::RiskLevel::Emergency, 0.1,
                            RiskTypes::ExceptionState::None);

    QSignalSpy spy(&source, &MockMetadataSource::metadataReceived);
    source.clearRiskOverride(QStringLiteral("CAM_01"));

    // autoPlay is off and the override was just cleared, so no further
    // event should fire for this camera until autoplay resumes or a new
    // override is set.
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestMockMetadataSource)
#include "test_mock_metadata_source.moc"
