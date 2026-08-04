#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

// 영상 비율 유지 계산(레터박스/필러박스) 공용 함수
// - VideoStream: 영상 그릴 위치 계산에 사용
// - DetectionOverlay: 박스 그릴 위치 계산에 같은 함수 사용
// - 계산을 한 곳에 모아야 영상과 박스 위치가 항상 일치함
namespace AspectFit {

// itemSize(화면에 할당된 영역) 안에 contentSize(영상 원본 비율)를 비율 유지하며
// 최대로 맞춰 넣었을 때의 사각형을 반환 (남는 공간은 위아래 또는 좌우 여백이 됨)
inline QRectF fitRect(const QSizeF &itemSize, const QSizeF &contentSize)
{
    // 크기 정보 없으면 비율 계산 불가 -> 원본 영역 그대로 반환
    if (itemSize.isEmpty() || contentSize.width() <= 0.0 || contentSize.height() <= 0.0)
        return QRectF(QPointF(0, 0), itemSize);

    // 가로세로 비율 계산
    const double itemAspect = itemSize.width() / itemSize.height();
    const double contentAspect = contentSize.width() / contentSize.height();

    QSizeF fitted;
    if (contentAspect > itemAspect) {
        // 영상이 더 넓적함 -> 폭 기준으로 맞춤 (레터박스: 위아래 여백)
        fitted.setWidth(itemSize.width());                  // 폭을 꽉 채움
        fitted.setHeight(itemSize.width() / contentAspect); // 비율대로 높이 계산
    } else {
        // 영상이 더 세로로 김 -> 높이 기준으로 맞춤 (필러박스: 좌우 여백)
        fitted.setHeight(itemSize.height());                // 높이를 꽉 채움
        fitted.setWidth(itemSize.height() * contentAspect); // 비율대로 폭 계산
    }

    // 남는 여백을 절반씩 나눠서 가운데 정렬
    const double x = (itemSize.width() - fitted.width()) / 2.0;
    const double y = (itemSize.height() - fitted.height()) / 2.0;
    return QRectF(QPointF(x, y), fitted); // 최종 위치+크기
}

// 정규화 좌표(0.0~1.0)를 실제 픽셀 좌표로 변환
// videoRect: fitRect()가 계산한, 영상이 실제로 그려지는 영역
// -> bbox가 항상 영상 위에 정확히 겹쳐짐
inline QRectF mapNormalizedRect(const QRectF &normalized, const QRectF &videoRect)
{
    return QRectF(videoRect.x() + normalized.x() * videoRect.width(),   // x: 여백만큼 밀어줌
                  videoRect.y() + normalized.y() * videoRect.height(),  // y: 여백만큼 밀어줌
                  normalized.width() * videoRect.width(),               // width: 영상 폭 기준 환산
                  normalized.height() * videoRect.height());            // height: 영상 높이 기준 환산
}

} // namespace AspectFit
