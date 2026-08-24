#include <QtTest>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "network/RiskEventSource.h"

class TestRiskEventSource : public QObject
{
    Q_OBJECT

private slots:
    void staleRetentionSingleMessagePolicy();

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

QTEST_MAIN(TestRiskEventSource)
#include "test_risk_event_source.moc"
