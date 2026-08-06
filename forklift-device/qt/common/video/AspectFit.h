#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>

// - 영상 비율 유지 및 좌표 변환 유틸리티 네임스페이스
namespace AspectFit {

inline QRectF fitRect(const QSizeF &itemSize, const QSizeF &contentSize)       // - 비율 유지 영역 계산: 원본 비율을 유지하며 표시 영역에 맞춘 영역 반환
{
    if (itemSize.isEmpty() || contentSize.width() <= 0.0 || contentSize.height() <= 0.0) // - 유효성 검증: 크기 정보 미존재 시 전체 영역 반환
        return QRectF(QPointF(0, 0), itemSize);

    const double itemAspect = itemSize.width() / itemSize.height();             // - 표시 영역 비율 계산: 화면 가로/세로 비율 측정
    const double contentAspect = contentSize.width() / contentSize.height();   // - 원본 영상 비율 계산: 영상 가로/세로 비율 측정

    QSizeF fitted;
    if (contentAspect > itemAspect) {                                           // - 가로 비율 우세 비교: 가로 폭 기준 영역 설정 (상하 여백 발생)
        fitted.setWidth(itemSize.width());                                     // - 폭 설정: 표시 영역 가로 폭 맞춤
        fitted.setHeight(itemSize.width() / contentAspect);                    // - 높이 계산: 비율 기준 높이 산출
    } else {                                                                   // - 세로 비율 우세 비교: 세로 높이 기준 영역 설정 (좌우 여백 발생)
        fitted.setHeight(itemSize.height());                                   // - 높이 설정: 표시 영역 세로 높이 맞춤
        fitted.setWidth(itemSize.height() * contentAspect);                    // - 폭 계산: 비율 기준 가로 폭 산출
    }

    const double x = (itemSize.width() - fitted.width()) / 2.0;                // - X 위치 계산: 좌우 중앙 정렬 여백 산출
    const double y = (itemSize.height() - fitted.height()) / 2.0;              // - Y 위치 계산: 상하 중앙 정렬 여백 산출
    return QRectF(QPointF(x, y), fitted);                                      // - 영역 반환: 여백이 적용된 최종 표시 영역 좌표 반환
}

inline QRectF mapNormalizedRect(const QRectF &normalized, const QRectF &videoRect) // - 좌표 정규화 변환: 0.0~1.0 비율 좌표를 실제 화면 픽셀 좌표로 변환
{
    return QRectF(videoRect.x() + normalized.x() * videoRect.width(),          // - X 좌표 계산: 영상 시작 위치 및 비율 기준 X 좌표 산출
                  videoRect.y() + normalized.y() * videoRect.height(),         // - Y 좌표 계산: 영상 시작 위치 및 비율 기준 Y 좌표 산출
                  normalized.width() * videoRect.width(),                      // - 너비 계산: 영상 폭 기준 너비 픽셀 산출
                  normalized.height() * videoRect.height());                   // - 높이 계산: 영상 높이 기준 높이 픽셀 산출
}

} // namespace AspectFit
