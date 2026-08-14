#include "DetectionOverlay.h"

#include <QPainter>
#include <QPainterPath>

#include "AspectFit.h"

DetectionOverlay::DetectionOverlay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    // 영상 위에 겹쳐 그리는 투명 레이어라 배경은 항상 투명
    setFillColor(Qt::transparent);
    setAntialiasing(true);
}

void DetectionOverlay::setVideoSize(const QSize &size)
{
    if (m_videoSize == size)
        return;
    m_videoSize = size;
    emit videoSizeChanged();
    update();
}

void DetectionOverlay::setPersonBBox(const BBox &box)
{
    if (m_personBBox == box)
        return;
    m_personBBox = box;
    emit personBBoxChanged();
    update();
}

void DetectionOverlay::setForkliftBBox(const BBox &box)
{
    if (m_forkliftBBox == box)
        return;
    m_forkliftBBox = box;
    emit forkliftBBoxChanged();
    update();
}

void DetectionOverlay::setDistanceM(double distance)
{
    if (qFuzzyCompare(m_distanceM + 1.0, distance + 1.0))
        return;
    m_distanceM = distance;
    emit distanceMChanged();
    update();
}

void DetectionOverlay::setDistanceValid(bool valid)
{
    if (m_distanceValid == valid)
        return;
    m_distanceValid = valid;
    emit distanceValidChanged();
    update();
}

void DetectionOverlay::setPersonColor(const QColor &color)
{
    if (m_personColor == color)
        return;
    m_personColor = color;
    emit personColorChanged();
    update();
}

void DetectionOverlay::setForkliftColor(const QColor &color)
{
    if (m_forkliftColor == color)
        return;
    m_forkliftColor = color;
    emit forkliftColorChanged();
    update();
}

void DetectionOverlay::setLineColor(const QColor &color)
{
    if (m_lineColor == color)
        return;
    m_lineColor = color;
    emit lineColorChanged();
    update();
}

void DetectionOverlay::setTtcValid(bool valid)
{
    if (m_ttcValid == valid)
        return;
    m_ttcValid = valid;
    emit ttcValidChanged();
    update();
}

void DetectionOverlay::setTtcSeconds(double seconds)
{
    if (qFuzzyCompare(m_ttcSeconds + 1.0, seconds + 1.0))
        return;
    m_ttcSeconds = seconds;
    emit ttcSecondsChanged();
    update();
}

void DetectionOverlay::setTtcConfidence(double confidence)
{
    if (qFuzzyCompare(m_ttcConfidence + 1.0, confidence + 1.0))
        return;
    m_ttcConfidence = confidence;
    emit ttcConfidenceChanged();
    update();
}

void DetectionOverlay::setTtcReason(const QString &reason)
{
    if (m_ttcReason == reason)
        return;
    m_ttcReason = reason;
    emit ttcReasonChanged();
    update();
}

void DetectionOverlay::setTtcColor(const QColor &color)
{
    if (m_ttcColor == color)
        return;
    m_ttcColor = color;
    emit ttcColorChanged();
    update();
}

void DetectionOverlay::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    update();
}

// update()가 호출될 때마다 Qt가 실행 (bbox/거리/색상 등 프로퍼티가 바뀔 때마다 update() 호출됨)
void DetectionOverlay::paint(QPainter *painter)
{
    const QRectF bounds(0, 0, width(), height());
    if (bounds.isEmpty())
        return;

    // VideoStream의 paint()와 동일한 계산 -- 그래야 bbox가 실제 영상 위치와 일치함
    const QRectF videoRect = AspectFit::fitRect(bounds.size(), m_videoSize);

    // 박스는 각자 따로 유효하면 그림 (사람만 있어도, 지게차만 있어도 그려짐)
    if (m_personBBox.isValid())
        drawBox(painter, videoRect, m_personBBox, m_personColor, QStringLiteral("PERSON"));
    if (m_forkliftBBox.isValid())
        drawBox(painter, videoRect, m_forkliftBBox, m_forkliftColor, QStringLiteral("FORKLIFT"));

    // 거리 라벨은 personBBox만 있으면 표시 -- forkliftBBox는 ArUco 연동을 안 하기로
    // 결정되어 앞으로도 계속 invalid라 조건에서 뺌. distanceM/distanceValid는 서버에서
    // personBBox와 무관하게 독립적으로 내려오는 값이라 그대로 씀
    if (m_personBBox.isValid()) {
        const QRectF personRect = AspectFit::mapNormalizedRect(
            QRectF(m_personBBox.x(), m_personBBox.y(), m_personBBox.width(), m_personBBox.height()), videoRect);

        const QString distanceLabel = m_distanceValid ? QStringLiteral("%1 m").arg(m_distanceM, 0, 'f', 2)
                                                       : QStringLiteral("측정 불가");

        QFont font = painter->font();
        font.setPointSize(11);
        font.setBold(true);
        painter->setFont(font);

        const QFontMetrics metrics(font);
        // personRect 우상단 바깥쪽에 배지 형태로 배치
        const QPointF anchor = personRect.topRight() + QPointF(6, -6);
        QRectF textRect = QRectF(metrics.boundingRect(distanceLabel)).adjusted(-8, -4, 8, 4).translated(anchor);

        // personBBox가 화면 가장자리에 가까우면 배지가 오버레이 밖으로 나가서 안 보일 수
        // 있음 -- 그리는 영역(bounds) 안으로 되돌림
        if (textRect.top() < bounds.top())
            textRect.moveTop(bounds.top());
        if (textRect.right() > bounds.right())
            textRect.moveRight(bounds.right());

        QPainterPath badgePath;
        badgePath.addRoundedRect(textRect, 6, 6);
        painter->fillPath(badgePath, QColor(0, 0, 0, 170));
        painter->setPen(Qt::white);
        painter->drawText(textRect, Qt::AlignCenter, distanceLabel);

        // TTC 예측 실험 배지 -- 같은 personRect 기준이지만 좌하단에 그려서 우상단
        // 거리 배지와 안 겹침
        drawTtcBadge(painter, bounds, personRect);
    }
}

// TTC 예측 실험 배지: personRect 좌하단 바깥쪽에 배지 형태로 그림 (거리 배지와
// 대칭되는 위치라 서로 안 겹침). 항상 "예측 실험" 표를 같이 그려서 안전 판정과
// 절대 혼동되지 않게 함 -- 색상도 위험 팔레트가 아닌 중립색(m_ttcColor, QML이
// Theme.colorTextMuted를 바인딩)만 씀. 무효할 땐 숫자 대신 짧은 사유만 표시하고,
// 절대 0/과거 값을 진짜처럼 보여주지 않음
void DetectionOverlay::drawTtcBadge(QPainter *painter, const QRectF &bounds, const QRectF &personRect) const
{
    const QString experimentTag = QStringLiteral("예측 실험");
    const QString body = m_ttcValid
        ? QStringLiteral("TTC %1s · 신뢰 %2").arg(m_ttcSeconds, 0, 'f', 1).arg(
              m_ttcConfidence >= 0.7 ? QStringLiteral("상") : (m_ttcConfidence >= 0.4 ? QStringLiteral("중") : QStringLiteral("하")))
        : m_ttcReason; // - 무효 사유(예: "표본 부족")를 대신 보여줌 -- 절대 숫자를 안 그림

    if (body.isEmpty()) // - 사유조차 없으면(초기 상태) 그릴 게 없음
        return;

    const QString label = experimentTag + QStringLiteral(" · ") + body;

    QFont font = painter->font();
    font.setPointSize(9);   // - 거리 배지(11pt, bold)보다 작고 안 굵게 -- 시각적으로 보조 지표임을 드러냄
    font.setBold(false);
    painter->setFont(font);

    const QFontMetrics metrics(font);
    // personRect 좌하단 바깥쪽에 배치 (거리 배지는 우상단 -- 대칭 위치라 안 겹침)
    const QPointF anchor = personRect.bottomLeft() + QPointF(0, 6);
    QRectF textRect = QRectF(metrics.boundingRect(label)).adjusted(-8, -4, 8, 4).translated(anchor);

    // 거리 배지와 동일한 로직: 화면 가장자리를 벗어나면 그리는 영역(bounds) 안으로 되돌림
    if (textRect.bottom() > bounds.bottom())
        textRect.moveBottom(bounds.bottom());
    if (textRect.left() < bounds.left())
        textRect.moveLeft(bounds.left());

    QPainterPath badgePath;
    badgePath.addRoundedRect(textRect, 6, 6);
    painter->fillPath(badgePath, QColor(0, 0, 0, 170));
    painter->setPen(m_ttcColor); // - 위험 팔레트가 아닌 중립색 -- 색만으로 상태를 말하지 않음(텍스트가 항상 같이 붙음)
    painter->drawText(textRect, Qt::AlignCenter, label);
}

// 박스 하나 + 좌상단에 붙는 색깔 있는 라벨(예: "PERSON")을 그림
void DetectionOverlay::drawBox(QPainter *painter, const QRectF &videoRect, const BBox &box, const QColor &color,
                                const QString &label) const
{
    const QRectF rect = AspectFit::mapNormalizedRect(QRectF(box.x(), box.y(), box.width(), box.height()), videoRect);

    QPen pen(color);
    pen.setWidthF(2.0);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(rect, 4, 4);

    QFont font = painter->font();
    font.setPointSize(9);
    font.setBold(true);
    painter->setFont(font);

    const QFontMetrics metrics(font);
    QRectF labelRect = metrics.boundingRect(label).adjusted(-6, -3, 6, 3);
    labelRect.moveBottomLeft(rect.topLeft());

    QPainterPath labelPath;
    labelPath.addRoundedRect(labelRect, 4, 4);
    painter->fillPath(labelPath, color);
    painter->setPen(Qt::black);
    painter->drawText(labelRect, Qt::AlignCenter, label);
}
