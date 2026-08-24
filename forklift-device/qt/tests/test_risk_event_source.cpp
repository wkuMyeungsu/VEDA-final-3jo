#include <QtTest>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "models/RiskMetadata.h"
#include "models/Types.h"
#include "network/RiskEventSource.h"

class TestRiskEventSource : public QObject
{
    Q_OBJECT

private slots:
    void staleRetentionSingleMessagePolicy();
    void staleThresholdBoundary();
    void watchdogThresholdBoundary();
    void startupGracePeriodBoundary();
    void riskLevelIntegerConversions();
    void exceptionStateStringConversions();
    void distanceFieldFallbackPriority();

private:
    static QByteArray makePayloadWithAge(qint64 ageMs)
    {
        const QDateTime dt = QDateTime::currentDateTimeUtc().addMSecs(-ageMs);
        QJsonObject obj;
        obj["terminal_id"] = QStringLiteral("TERM_01");
        obj["camera_id"] = QStringLiteral("CAM_01");
        obj["stream_id"] = QStringLiteral("CAM_01_CH_01");
        obj["channel"] = 1;
        obj["risk_level"] = 1;
        obj["distance_mm"] = 2500.0;
        obj["utc_time"] = dt.toString(Qt::ISODateWithMs);
        return QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }
};

void TestRiskEventSource::staleRetentionSingleMessagePolicy()
{
    RiskEventSource source(QStringLiteral("localhost"), 1883, QStringLiteral("TERM_01"));
    QSignalSpy spy(&source, &IMetadataSource::metadataReceived);

    // 1) 검사 활성(접속 직후 모사) + 나이 2000ms (1000ms 초과) -> 폐기 (신호 미발생)
    source.m_staleCheckArmed = true;
    source.processPayload(makePayloadWithAge(2000));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(source.m_staleCheckArmed, false); // 검사가 1회 소진되었는지 확인

    // 2) 검사 소진 후(두 번째 메시지부터) + 나이 2000ms (단말 시계 뒤처짐 모사) -> 정상 수용 (신호 발생)
    source.processPayload(makePayloadWithAge(2000));
    QCOMPARE(spy.count(), 1);
    spy.clear();

    // 3) 검사 활성(재접속 모사) + 미래 시각(NTP 스큐, age = -5000ms) -> 정상 수용 (신호 발생)
    source.m_staleCheckArmed = true;
    source.processPayload(makePayloadWithAge(-5000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(source.m_staleCheckArmed, false);
    spy.clear();

    // 4) 검사 재활성(재접속 모사) + 나이 2000ms -> 다시 폐기 (신호 미발생)
    source.m_staleCheckArmed = true;
    source.processPayload(makePayloadWithAge(2000));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(source.m_staleCheckArmed, false);
}

void TestRiskEventSource::staleThresholdBoundary()
{
    RiskEventSource source(QStringLiteral("localhost"), 1883, QStringLiteral("TERM_01"));
    QSignalSpy spy(&source, &IMetadataSource::metadataReceived);

    // 경계값 1: 999ms (< 1000ms) -> 수용 (신호 1회 발생)
    source.m_staleCheckArmed = true;
    source.processPayload(makePayloadWithAge(999));
    QCOMPARE(spy.count(), 1);
    spy.clear();

    // 경계값 2: 1000ms (== 1000ms, ageMs > 1000 조건 미충족) -> 수용 (신호 1회 발생)
    source.m_staleCheckArmed = true;
    source.processPayload(makePayloadWithAge(1000));
    QCOMPARE(spy.count(), 1);
    spy.clear();

    // 경계값 3: 1001ms (> 1000ms, 초과) -> 폐기 (신호 미발생)
    source.m_staleCheckArmed = true;
    source.processPayload(makePayloadWithAge(1001));
    QCOMPARE(spy.count(), 0);
}

void TestRiskEventSource::watchdogThresholdBoundary()
{
    // 정상 운영 중(첫 메시지 수신 후): 워치독 임계값 = 200ms * 5 = 1000ms
    RiskEventSource source(QStringLiteral("localhost"), 1883, QStringLiteral("TERM_01"));
    source.m_hasReceivedFirstMessage = true;
    QSignalSpy spy(&source, &IMetadataSource::linkLost);

    // 경계값 1: 999ms -> 타임아웃 미발생 (false)
    QCOMPARE(source.evaluateWatchdog(999), false);
    QCOMPARE(spy.count(), 0);

    // 경계값 2: 1000ms -> 타임아웃 발생 (true, linkLost 1회)
    QCOMPARE(source.evaluateWatchdog(1000), true);
    QCOMPARE(spy.count(), 1);
    spy.clear();

    // 경계값 3: 1001ms -> 타임아웃 발생 (true, linkLost 1회)
    QCOMPARE(source.evaluateWatchdog(1001), true);
    QCOMPARE(spy.count(), 1);
}

void TestRiskEventSource::startupGracePeriodBoundary()
{
    // 부팅 직후(첫 메시지 수신 전): 기동 유예 임계값 = 5000ms
    RiskEventSource source(QStringLiteral("localhost"), 1883, QStringLiteral("TERM_01"));
    source.m_hasReceivedFirstMessage = false;
    QSignalSpy spy(&source, &IMetadataSource::linkLost);

    // 경계값 1: 4999ms -> 유예 기간 유지, 타임아웃 미발생 (false)
    QCOMPARE(source.evaluateWatchdog(4999), false);
    QCOMPARE(spy.count(), 0);

    // 경계값 2: 5000ms -> 유예 만료, 타임아웃 발생 (true, linkLost 1회)
    QCOMPARE(source.evaluateWatchdog(5000), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(source.m_hasReceivedFirstMessage, true); // 첫 통지 후 정상 워치독 모드로 전환
    spy.clear();

    // 경계값 3: 5001ms -> 타임아웃 발생 (true)
    source.m_hasReceivedFirstMessage = false;
    QCOMPARE(source.evaluateWatchdog(5001), true);
    QCOMPARE(spy.count(), 1);
}

void TestRiskEventSource::riskLevelIntegerConversions()
{
    using namespace RiskTypes;

    // -1 -> 범위 밖 예외: Safe(0) 폴백
    QCOMPARE(riskLevelFromInt(-1), RiskLevel::Safe);

    // 0 -> Safe
    QCOMPARE(riskLevelFromInt(0), RiskLevel::Safe);

    // 1 -> Caution
    QCOMPARE(riskLevelFromInt(1), RiskLevel::Caution);

    // 2 -> Danger
    QCOMPARE(riskLevelFromInt(2), RiskLevel::Danger);

    // 3 -> Emergency (Critical)
    QCOMPARE(riskLevelFromInt(3), RiskLevel::Emergency);

    // 4 -> 범위 밖 예외: Safe(0) 폴백
    QCOMPARE(riskLevelFromInt(4), RiskLevel::Safe);
}

void TestRiskEventSource::exceptionStateStringConversions()
{
    using namespace RiskTypes;

    QCOMPARE(exceptionStateFromString(QStringLiteral("NONE")), ExceptionState::None);
    QCOMPARE(exceptionStateFromString(QStringLiteral("SENSOR_FAULT")), ExceptionState::SensorFault);
    QCOMPARE(exceptionStateFromString(QStringLiteral("DEAD_RECKONING")), ExceptionState::DeadReckoning);
    QCOMPARE(exceptionStateFromString(QStringLiteral("EMERGENCY_IMPACT")), ExceptionState::EmergencyImpact);
    QCOMPARE(exceptionStateFromString(QStringLiteral("NETWORK_DISCONNECTED")), ExceptionState::NetworkDisconnected);
    QCOMPARE(exceptionStateFromString(QStringLiteral("CAMERA_DISCONNECTED")), ExceptionState::CameraDisconnected);
    QCOMPARE(exceptionStateFromString(QStringLiteral("UNCONFIRMED_PROXIMITY")), ExceptionState::UnconfirmedProximity);

    // 알 수 없는 문자열 -> None 폴백
    QCOMPARE(exceptionStateFromString(QStringLiteral("UNKNOWN_CUSTOM_FAULT")), ExceptionState::None);
    QCOMPARE(exceptionStateFromString(QStringLiteral("")), ExceptionState::None);
}

void TestRiskEventSource::distanceFieldFallbackPriority()
{
    // Case 1: distance_mm (밀리미터)가 있으면 distance_m보다 우선 적용 (1500mm -> 1.5m)
    {
        QJsonObject obj;
        obj["distance_mm"] = 1500.0;
        obj["distance_m"] = 9.99;
        const RiskMetadata meta = RiskMetadata::fromJson(obj);
        QCOMPARE(meta.distanceValid(), true);
        QCOMPARE(meta.distanceM(), 1.5);
    }

    // Case 2: distance_mm이 없고 distance_m만 있는 경우 fallback
    {
        QJsonObject obj;
        obj["distance_m"] = 2.75;
        const RiskMetadata meta = RiskMetadata::fromJson(obj);
        QCOMPARE(meta.distanceValid(), true);
        QCOMPARE(meta.distanceM(), 2.75);
    }

    // Case 3: distance_mm, distance_m 둘 다 없거나 null인 경우
    {
        QJsonObject obj;
        const RiskMetadata meta = RiskMetadata::fromJson(obj);
        QCOMPARE(meta.distanceValid(), false);
        QCOMPARE(meta.distanceM(), 0.0);
    }
}

QTEST_MAIN(TestRiskEventSource)
#include "test_risk_event_source.moc"
