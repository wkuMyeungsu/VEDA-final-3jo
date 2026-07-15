#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

// Shared letterbox/pillarbox math. CameraVideoItem uses fitRect() to know
// where to draw the video image; DetectionOverlay uses the same function
// (with the same video-native size) so bounding boxes always line up with
// the frame underneath, regardless of the camera's aspect ratio.
namespace AspectFit {

inline QRectF fitRect(const QSizeF &itemSize, const QSizeF &contentSize)
{
    if (itemSize.isEmpty() || contentSize.width() <= 0.0 || contentSize.height() <= 0.0)
        return QRectF(QPointF(0, 0), itemSize);

    const double itemAspect = itemSize.width() / itemSize.height();
    const double contentAspect = contentSize.width() / contentSize.height();

    QSizeF fitted;
    if (contentAspect > itemAspect) {
        // Content is relatively wider than the item -> fit width, letterbox top/bottom.
        fitted.setWidth(itemSize.width());
        fitted.setHeight(itemSize.width() / contentAspect);
    } else {
        // Content is relatively taller than the item -> fit height, pillarbox left/right.
        fitted.setHeight(itemSize.height());
        fitted.setWidth(itemSize.height() * contentAspect);
    }

    const double x = (itemSize.width() - fitted.width()) / 2.0;
    const double y = (itemSize.height() - fitted.height()) / 2.0;
    return QRectF(QPointF(x, y), fitted);
}

// Maps a normalized [0,1] rect (BBox coordinates) into pixel space within
// `videoRect`, the letterboxed area returned by fitRect().
inline QRectF mapNormalizedRect(const QRectF &normalized, const QRectF &videoRect)
{
    return QRectF(videoRect.x() + normalized.x() * videoRect.width(),
                  videoRect.y() + normalized.y() * videoRect.height(),
                  normalized.width() * videoRect.width(),
                  normalized.height() * videoRect.height());
}

} // namespace AspectFit
