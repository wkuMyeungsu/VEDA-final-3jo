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
    // ---- TTC 예측 실험 표시용 (TtcExperiment가 계산, 안전 판정과 무관 -- 표시 전용) ----
    Q_PROPERTY(bool ttcValid READ ttcValid WRITE setTtcValid NOTIFY ttcValidChanged)
    Q_PROPERTY(double ttcSeconds READ ttcSeconds WRITE setTtcSeconds NOTIFY ttcSecondsChanged)
    Q_PROPERTY(double ttcConfidence READ ttcConfidence WRITE setTtcConfidence NOTIFY ttcConfidenceChanged)
    // ttcValid=false일 때 보여줄 짧은 무효 사유 (예: "표본 부족", "정지·후진")
    Q_PROPERTY(QString ttcReason READ ttcReason WRITE setTtcReason NOTIFY ttcReasonChanged)
    // 위험 팔레트(colorDanger 등)와 절대 겹치지 않는 중립/보조 색 (Theme.colorTextMuted 바인딩용)
    Q_PROPERTY(QColor ttcColor READ ttcColor WRITE setTtcColor NOTIFY ttcColorChanged)

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

    bool ttcValid() const { return m_ttcValid; }
    void setTtcValid(bool valid);

    double ttcSeconds() const { return m_ttcSeconds; }
    void setTtcSeconds(double seconds);

    double ttcConfidence() const { return m_ttcConfidence; }
    void setTtcConfidence(double confidence);

    QString ttcReason() const { return m_ttcReason; }
    void setTtcReason(const QString &reason);

    QColor ttcColor() const { return m_ttcColor; }
    void setTtcColor(const QColor &color);

signals:
    void videoSizeChanged();
    void personBBoxChanged();
    void forkliftBBoxChanged();
    void distanceMChanged();
    void distanceValidChanged();
    void personColorChanged();
    void forkliftColorChanged();
    void lineColorChanged();
    void ttcValidChanged();
    void ttcSecondsChanged();
    void ttcConfidenceChanged();
    void ttcReasonChanged();
    void ttcColorChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    // box 1개를 videoRect 기준으로 변환해서 사각형 + 라벨 텍스트를 그림
    void drawBox(QPainter *painter, const QRectF &videoRect, const BBox &box, const QColor &color,
                 const QString &label) const;

    // personRect 좌하단에 TTC 예측 실험 배지를 그림 (거리 배지는 우상단이라 안 겹침)
    void drawTtcBadge(QPainter *painter, const QRectF &bounds, const QRectF &personRect) const;

    QSize m_videoSize{16, 9};
    BBox m_personBBox;
    BBox m_forkliftBBox;
    double m_distanceM = 0.0;
    bool m_distanceValid = true;
    QColor m_personColor{0x4f, 0xc3, 0xf7};
    QColor m_forkliftColor{0xff, 0xb7, 0x4d};
    QColor m_lineColor{0xff, 0xff, 0xff};

    bool m_ttcValid = false;
    double m_ttcSeconds = 0.0;
    double m_ttcConfidence = 0.0;
    QString m_ttcReason;
    QColor m_ttcColor{0x6b, 0x76, 0x90}; // - Theme.colorTextMuted와 동일 기본값(실제 값은 QML이 바인딩)
};
