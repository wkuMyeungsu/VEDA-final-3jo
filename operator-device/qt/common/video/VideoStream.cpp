#include "VideoStream.h"

#include <QPainter>

#include "AspectFit.h"
#include "IVideoSource.h"
#include "VideoSourceManager.h"

VideoStream::VideoStream(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFillColor(Qt::transparent);
    setAntialiasing(true);
}

void VideoStream::setCameraId(const QString &id)
{
    if (m_cameraId == id)
        return;

    detachFromSource();
    m_cameraId = id;
    // Deliberately do NOT clear m_lastFrame here: keeping the previous
    // frame on screen during a camera switch is the whole point of this
    // class (avoids a black flash on the operator terminal).
    if (!m_lastFrame.isNull())
        setSwitching(true);
    attachToSource();

    emit cameraIdChanged();
    update();
}

void VideoStream::setPlaceholderColor(const QColor &color)
{
    if (m_placeholderColor == color)
        return;
    m_placeholderColor = color;
    emit placeholderColorChanged();
    update();
}

void VideoStream::attachToSource()
{
    if (m_cameraId.isEmpty())
        return;

    VideoSourceManager *manager = VideoSourceManager::instance();
    m_source = manager ? manager->sourceFor(m_cameraId) : nullptr;

    if (!m_source) {
        handleConnectionStateChanged(RiskTypes::ConnectionState::Disconnected);
        return;
    }

    connect(m_source, &IVideoSource::frameReady, this, &VideoStream::handleFrame, Qt::UniqueConnection);
    connect(m_source, &IVideoSource::connectionStateChanged, this,
            &VideoStream::handleConnectionStateChanged, Qt::UniqueConnection);
    handleConnectionStateChanged(m_source->connectionState());
}

void VideoStream::detachFromSource()
{
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = nullptr;
}

void VideoStream::setSwitching(bool switching)
{
    if (m_switching == switching)
        return;
    m_switching = switching;
    emit switchingChanged();
}

void VideoStream::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    update();
}

void VideoStream::handleFrame(const QImage &frame)
{
    if (frame.isNull())
        return;

    m_lastFrame = frame;
    setSwitching(false);
    updateFpsCounter();

    if (m_lastFrameSize != frame.size()) {
        m_lastFrameSize = frame.size();
        emit videoSizeChanged();
    }

    update();
}

void VideoStream::handleConnectionStateChanged(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged();
    update();
}

void VideoStream::updateFpsCounter()
{
    if (!m_fpsClock.isValid()) {
        m_fpsClock.start();
        m_frameCountWindow = 0;
        return;
    }

    ++m_frameCountWindow;
    const qint64 elapsed = m_fpsClock.elapsed();
    if (elapsed >= 1000) {
        m_fps = m_frameCountWindow * 1000.0 / elapsed;
        m_frameCountWindow = 0;
        m_fpsClock.restart();
        emit fpsChanged();
    }
}

void VideoStream::paint(QPainter *painter)
{
    const QRectF bounds(0, 0, width(), height());
    if (bounds.isEmpty())
        return;

    painter->fillRect(bounds, m_placeholderColor);

    if (!m_lastFrame.isNull()) {
        const QRectF videoRect = AspectFit::fitRect(bounds.size(), m_lastFrame.size());
        painter->drawImage(videoRect, m_lastFrame);
    } else {
        paintPlaceholder(painter, bounds);
    }

    if (m_switching) {
        painter->fillRect(bounds, QColor(0, 0, 0, 110));
        QFont font = painter->font();
        font.setPointSize(12);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(255, 255, 255, 230));
        painter->drawText(bounds, Qt::AlignCenter, QStringLiteral("전환 중..."));
    }
}

void VideoStream::paintPlaceholder(QPainter *painter, const QRectF &bounds) const
{
    painter->setPen(QPen(QColor(255, 255, 255, 18), 1));
    for (int x = -static_cast<int>(bounds.height()); x < static_cast<int>(bounds.width()); x += 28)
        painter->drawLine(QPointF(x, bounds.height()), QPointF(x + bounds.height(), 0));

    QFont font = painter->font();
    font.setPointSize(14);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(QColor(255, 255, 255, 150));
    painter->drawText(bounds.adjusted(0, -10, 0, -10), Qt::AlignCenter,
                       m_cameraId.isEmpty() ? QStringLiteral("NO SIGNAL") : m_cameraId);

    QFont subFont = painter->font();
    subFont.setPointSize(10);
    subFont.setBold(false);
    painter->setFont(subFont);
    painter->setPen(QColor(255, 255, 255, 90));
    painter->drawText(bounds.adjusted(0, 16, 0, 16), Qt::AlignCenter, QStringLiteral("NO SIGNAL"));
}
