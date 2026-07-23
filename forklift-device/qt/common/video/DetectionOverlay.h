#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <qqmlintegration.h>

#include "../models/BBox.h"
#include "../models/Types.h"

// Draws person/forklift detection boxes, a connecting line, and the
// reported distance on top of a CameraVideoView. Kept independent from
// CameraVideoItem -- it only needs the same video-native size (bound from
// QML) to letterbox-align itself with the frame underneath, via the same
// AspectFit helper.
class DetectionOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QSize videoSize READ videoSize WRITE setVideoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(BBox personBBox READ personBBox WRITE setPersonBBox NOTIFY personBBoxChanged)
    Q_PROPERTY(BBox forkliftBBox READ forkliftBBox WRITE setForkliftBBox NOTIFY forkliftBBoxChanged)
    Q_PROPERTY(double distanceM READ distanceM WRITE setDistanceM NOTIFY distanceMChanged)
    Q_PROPERTY(QColor personColor READ personColor WRITE setPersonColor NOTIFY personColorChanged)
    Q_PROPERTY(QColor forkliftColor READ forkliftColor WRITE setForkliftColor NOTIFY forkliftColorChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY lineColorChanged)

public:
    explicit DetectionOverlay(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QSize videoSize() const { return m_videoSize; }
    void setVideoSize(const QSize &size);

    BBox personBBox() const { return m_personBBox; }
    void setPersonBBox(const BBox &box);

    BBox forkliftBBox() const { return m_forkliftBBox; }
    void setForkliftBBox(const BBox &box);

    double distanceM() const { return m_distanceM; }
    void setDistanceM(double distance);

    QColor personColor() const { return m_personColor; }
    void setPersonColor(const QColor &color);

    QColor forkliftColor() const { return m_forkliftColor; }
    void setForkliftColor(const QColor &color);

    QColor lineColor() const { return m_lineColor; }
    void setLineColor(const QColor &color);

signals:
    void videoSizeChanged();
    void personBBoxChanged();
    void forkliftBBoxChanged();
    void distanceMChanged();
    void personColorChanged();
    void forkliftColorChanged();
    void lineColorChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void drawBox(QPainter *painter, const QRectF &videoRect, const BBox &box, const QColor &color,
                 const QString &label) const;

    QSize m_videoSize{16, 9};
    BBox m_personBBox;
    BBox m_forkliftBBox;
    double m_distanceM = 0.0;
    QColor m_personColor{0x4f, 0xc3, 0xf7};
    QColor m_forkliftColor{0xff, 0xb7, 0x4d};
    QColor m_lineColor{0xff, 0xff, 0xff};
};
