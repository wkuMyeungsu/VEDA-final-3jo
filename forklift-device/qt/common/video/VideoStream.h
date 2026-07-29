#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickPaintedItem>
#include <qqmlintegration.h>

#include "../models/Types.h"

class IVideoSource;

// QML-facing video surface. Set `cameraId` and it looks up the matching
// IVideoSource through VideoSourceManager::instance() -- QML never knows
// whether that source is Mock, a local file, or (eventually) RTSP.
//
// Keeps painting the last received frame when the camera changes or the
// source disconnects, so the screen never goes black; a translucent
// "switching" veil is drawn on top until the first frame of a new source
// arrives.
class VideoStream : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId NOTIFY cameraIdChanged)
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(qreal fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(bool switching READ isSwitching NOTIFY switchingChanged)
    Q_PROPERTY(QColor placeholderColor READ placeholderColor WRITE setPlaceholderColor NOTIFY placeholderColorChanged)

public:
    explicit VideoStream(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QString cameraId() const { return m_cameraId; }
    void setCameraId(const QString &id);

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }
    QSize videoSize() const { return m_lastFrame.isNull() ? QSize(16, 9) : m_lastFrame.size(); }
    qreal fps() const { return m_fps; }
    bool isSwitching() const { return m_switching; }

    QColor placeholderColor() const { return m_placeholderColor; }
    void setPlaceholderColor(const QColor &color);

signals:
    void cameraIdChanged();
    void connectionStateChanged();
    void videoSizeChanged();
    void fpsChanged();
    void switchingChanged();
    void placeholderColorChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private slots:
    void handleFrame(const QImage &frame);
    void handleConnectionStateChanged(RiskTypes::ConnectionState state);

private:
    void attachToSource();
    void detachFromSource();
    void setSwitching(bool switching);
    void updateFpsCounter();
    void paintPlaceholder(QPainter *painter, const QRectF &bounds) const;

    QString m_cameraId;
    IVideoSource *m_source = nullptr;
    QImage m_lastFrame;
    QSize m_lastFrameSize;
    bool m_switching = false;
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
    QColor m_placeholderColor{0x10, 0x14, 0x1f};

    QElapsedTimer m_fpsClock;
    int m_frameCountWindow = 0;
    qreal m_fps = 0.0;
};
