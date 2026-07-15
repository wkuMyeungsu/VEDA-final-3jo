#include "DetectionOverlay.h"

#include <QPainter>
#include <QPainterPath>

#include "AspectFit.h"

DetectionOverlay::DetectionOverlay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
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

void DetectionOverlay::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    update();
}

void DetectionOverlay::paint(QPainter *painter)
{
    const QRectF bounds(0, 0, width(), height());
    if (bounds.isEmpty())
        return;

    const QRectF videoRect = AspectFit::fitRect(bounds.size(), m_videoSize);

    if (m_personBBox.isValid())
        drawBox(painter, videoRect, m_personBBox, m_personColor, QStringLiteral("PERSON"));
    if (m_forkliftBBox.isValid())
        drawBox(painter, videoRect, m_forkliftBBox, m_forkliftColor, QStringLiteral("FORKLIFT"));

    if (m_personBBox.isValid() && m_forkliftBBox.isValid()) {
        const QRectF personRect = AspectFit::mapNormalizedRect(
            QRectF(m_personBBox.x(), m_personBBox.y(), m_personBBox.width(), m_personBBox.height()), videoRect);
        const QRectF forkliftRect = AspectFit::mapNormalizedRect(
            QRectF(m_forkliftBBox.x(), m_forkliftBBox.y(), m_forkliftBBox.width(), m_forkliftBBox.height()),
            videoRect);

        const QPointF personCenter = personRect.center();
        const QPointF forkliftCenter = forkliftRect.center();

        QPen linePen(m_lineColor);
        linePen.setWidthF(1.5);
        linePen.setStyle(Qt::DashLine);
        painter->setPen(linePen);
        painter->drawLine(personCenter, forkliftCenter);

        const QPointF mid = (personCenter + forkliftCenter) / 2.0;
        const QString distanceLabel = QStringLiteral("%1 m").arg(m_distanceM, 0, 'f', 2);

        QFont font = painter->font();
        font.setPointSize(11);
        font.setBold(true);
        painter->setFont(font);

        const QFontMetrics metrics(font);
        const QRectF textRect = QRectF(metrics.boundingRect(distanceLabel)).adjusted(-8, -4, 8, 4).translated(mid);

        QPainterPath badgePath;
        badgePath.addRoundedRect(textRect, 6, 6);
        painter->fillPath(badgePath, QColor(0, 0, 0, 170));
        painter->setPen(Qt::white);
        painter->drawText(textRect, Qt::AlignCenter, distanceLabel);
    }
}

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
