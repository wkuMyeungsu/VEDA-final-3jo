#include <QtTest>

#include "video/AspectFit.h"

class TestAspectFit : public QObject
{
    Q_OBJECT

private slots:
    void letterboxesWideContent();
    void pillarboxesTallContent();
    void mapsNormalizedRectToVideoRect();
};

void TestAspectFit::letterboxesWideContent()
{
    // 16:9 content inside a square item -> fits width, bars top/bottom.
    const QRectF rect = AspectFit::fitRect(QSizeF(400, 400), QSizeF(1600, 900));
    QCOMPARE(rect.width(), 400.0);
    QVERIFY(rect.height() < 400.0);
    QCOMPARE(rect.x(), 0.0);
    QVERIFY(rect.y() > 0.0);
}

void TestAspectFit::pillarboxesTallContent()
{
    // Portrait content inside a square item -> fits height, bars left/right.
    const QRectF rect = AspectFit::fitRect(QSizeF(400, 400), QSizeF(900, 1600));
    QCOMPARE(rect.height(), 400.0);
    QVERIFY(rect.width() < 400.0);
    QCOMPARE(rect.y(), 0.0);
    QVERIFY(rect.x() > 0.0);
}

void TestAspectFit::mapsNormalizedRectToVideoRect()
{
    const QRectF videoRect(10, 20, 100, 200);
    const QRectF mapped = AspectFit::mapNormalizedRect(QRectF(0.5, 0.5, 0.25, 0.25), videoRect);

    QCOMPARE(mapped.x(), 10 + 0.5 * 100);
    QCOMPARE(mapped.y(), 20 + 0.5 * 200);
    QCOMPARE(mapped.width(), 0.25 * 100);
    QCOMPARE(mapped.height(), 0.25 * 200);
}

QTEST_MAIN(TestAspectFit)
#include "test_bbox_aspect_fit.moc"
