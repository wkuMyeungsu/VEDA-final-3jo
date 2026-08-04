#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <qqmlintegration.h>

#include "../models/BBox.h"
#include "../models/Types.h"

// 영상 위에 사람/지게차 박스, 연결선, 거리 라벨을 그리는 투명 오버레이
// - VideoStream과 별개 아이템, 같은 videoSize로 AspectFit 계산해서 위치 맞춤
// - 박스는 각각 유효할 때만 그림, 선+거리 라벨은 둘 다 있을 때만 그림
class DetectionOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QSize videoSize READ videoSize WRITE setVideoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(BBox personBBox READ personBBox WRITE setPersonBBox NOTIFY personBBoxChanged)
    Q_PROPERTY(BBox forkliftBBox READ forkliftBBox WRITE setForkliftBBox NOTIFY forkliftBBoxChanged)
    Q_PROPERTY(double distanceM READ distanceM WRITE setDistanceM NOTIFY distanceMChanged)
    Q_PROPERTY(bool distanceValid READ distanceValid WRITE setDistanceValid NOTIFY distanceValidChanged)
    Q_PROPERTY(QColor personColor READ personColor WRITE setPersonColor NOTIFY personColorChanged)
    Q_PROPERTY(QColor forkliftColor READ forkliftColor WRITE setForkliftColor NOTIFY forkliftColorChanged)
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY lineColorChanged)

public:
    explicit DetectionOverlay(QQuickItem *parent = nullptr);

    // update()가 호출될 때마다 Qt가 실행 -- 실제 박스/선/라벨 그리기
    void paint(QPainter *painter) override;

    QSize videoSize() const { return m_videoSize; }
    void setVideoSize(const QSize &size);

    BBox personBBox() const { return m_personBBox; }
    void setPersonBBox(const BBox &box);

    BBox forkliftBBox() const { return m_forkliftBBox; }
    void setForkliftBBox(const BBox &box);

    double distanceM() const { return m_distanceM; }
    void setDistanceM(double distance);

    bool distanceValid() const { return m_distanceValid; }
    void setDistanceValid(bool valid);

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
    void distanceValidChanged();
    void personColorChanged();
    void forkliftColorChanged();
    void lineColorChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    // box 1개를 videoRect 기준으로 변환해서 사각형 + 라벨 텍스트를 그림
    void drawBox(QPainter *painter, const QRectF &videoRect, const BBox &box, const QColor &color,
                 const QString &label) const;

    QSize m_videoSize{16, 9};
    BBox m_personBBox;
    BBox m_forkliftBBox;
    double m_distanceM = 0.0;
    bool m_distanceValid = true;
    QColor m_personColor{0x4f, 0xc3, 0xf7};
    QColor m_forkliftColor{0xff, 0xb7, 0x4d};
    QColor m_lineColor{0xff, 0xff, 0xff};
};
