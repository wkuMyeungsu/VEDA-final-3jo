#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <qqmlintegration.h>

#include "../models/BBox.h"
#include "../models/Types.h"

// - 객체 감지 오버레이 클래스 (영상 상단에 감지 상자, 연결선, 거리 라벨을 그리는 QML 투명 레이어)
class DetectionOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT                                                                 // - QML 모듈 내 요소 등록 매크로
    Q_PROPERTY(QSize videoSize READ videoSize WRITE setVideoSize NOTIFY videoSizeChanged) // - 영상 크기 속성: 원본 비디오 해상도
    Q_PROPERTY(BBox personBBox READ personBBox WRITE setPersonBBox NOTIFY personBBoxChanged) // - 사람 영역 속성: 감지된 사람 좌표
    Q_PROPERTY(BBox forkliftBBox READ forkliftBBox WRITE setForkliftBBox NOTIFY forkliftBBoxChanged) // - 지게차 영역 속성: 감지된 지게차 좌표
    Q_PROPERTY(double distanceM READ distanceM WRITE setDistanceM NOTIFY distanceMChanged) // - 측정 거리 속성: 감지 거리(m)
    Q_PROPERTY(bool distanceValid READ distanceValid WRITE setDistanceValid NOTIFY distanceValidChanged) // - 거리 유효성 속성: 거리 측정 유효 여부
    Q_PROPERTY(QColor personColor READ personColor WRITE setPersonColor NOTIFY personColorChanged) // - 사람 색상 속성: 사람 박스 테두리 색상
    Q_PROPERTY(QColor forkliftColor READ forkliftColor WRITE setForkliftColor NOTIFY forkliftColorChanged) // - 지게차 색상 속성: 지게차 박스 테두리 색상
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY lineColorChanged) // - 선 색상 속성: 거리 연결선 색상

public:
    explicit DetectionOverlay(QQuickItem *parent = nullptr);                   // - 생성자: 부모 객체 지정 및 레이어 초기화

    void paint(QPainter *painter) override;                                    // - 그리기 함수: 감지 영역 박스, 연결선, 거리 배지 렌더링

    QSize videoSize() const { return m_videoSize; }                             // - 영상 크기 조회: 비디오 해상도 반환
    void setVideoSize(const QSize &size);                                      // - 영상 크기 설정: 비디오 해상도 변경 및 화면 갱신

    BBox personBBox() const { return m_personBBox; }                           // - 사람 영역 조회: 사람 BBox 반환
    void setPersonBBox(const BBox &box);                                       // - 사람 영역 설정: 사람 BBox 변경 및 화면 갱신

    BBox forkliftBBox() const { return m_forkliftBBox; }                       // - 지게차 영역 조회: 지게차 BBox 반환
    void setForkliftBBox(const BBox &box);                                     // - 지게차 영역 설정: 지게차 BBox 변경 및 화면 갱신

    double distanceM() const { return m_distanceM; }                           // - 측정 거리 조회: 감지 거리(m) 반환
    void setDistanceM(double distance);                                        // - 측정 거리 설정: 감지 거리(m) 변경 및 화면 갱신

    bool distanceValid() const { return m_distanceValid; }                     // - 거리 유효성 조회: 거리 측정 가능 여부 반환
    void setDistanceValid(bool valid);                                         // - 거리 유효성 설정: 거리 측정 가능 여부 변경 및 화면 갱신

    QColor personColor() const { return m_personColor; }                       // - 사람 색상 조회: 사람 박스 색상 반환
    void setPersonColor(const QColor &color);                                  // - 사람 색상 설정: 사람 박스 색상 변경 및 화면 갱신

    QColor forkliftColor() const { return m_forkliftColor; }                   // - 지게차 색상 조회: 지게차 박스 색상 반환
    void setForkliftColor(const QColor &color);                                // - 지게차 색상 설정: 지게차 박스 색상 변경 및 화면 갱신

    QColor lineColor() const { return m_lineColor; }                           // - 선 색상 조회: 연결선 색상 반환
    void setLineColor(const QColor &color);                                    // - 선 색상 설정: 연결선 색상 변경 및 화면 갱신

signals:
    void videoSizeChanged();                                                   // - 영상 크기 변경 알림: 비디오 해상도 변경 시 발생
    void personBBoxChanged();                                                  // - 사람 영역 변경 알림: 사람 BBox 좌표 변경 시 발생
    void forkliftBBoxChanged();                                                // - 지게차 영역 변경 알림: 지게차 BBox 좌표 변경 시 발생
    void distanceMChanged();                                                   // - 측정 거리 변경 알림: 감지 거리 변경 시 발생
    void distanceValidChanged();                                               // - 거리 유효성 변경 알림: 유효 상태 변경 시 발생
    void personColorChanged();                                                 // - 사람 색상 변경 알림: 박스 색상 변경 시 발생
    void forkliftColorChanged();                                               // - 지게차 색상 변경 알림: 박스 색상 변경 시 발생
    void lineColorChanged();                                                   // - 선 색상 변경 알림: 연결선 색상 변경 시 발생

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override; // - 영역 변경 처리: 오버레이 크기 변경 시 화면 갱신

private:
    void drawBox(QPainter *painter, const QRectF &videoRect, const BBox &box, const QColor &color,
                 const QString &label) const;                                  // - 상자 그리기: 개별 BBox 사각형 및 라벨 텍스트 렌더링

    QSize m_videoSize{16, 9};                                                  // - 영상 크기: 원본 비디오 해상도 보관 (기본값: 16x9)
    BBox m_personBBox;                                                         // - 사람 영역: 감지된 사람 위치 좌표
    BBox m_forkliftBBox;                                                       // - 지게차 영역: 감지된 지게차 위치 좌표
    double m_distanceM = 0.0;                                                  // - 측정 거리: 감지 대상과의 거리(m)
    bool m_distanceValid = true;                                               // - 거리 유효성: 거리 측정 유효 여부
    QColor m_personColor{0x4f, 0xc3, 0xf7};                                    // - 사람 색상: 사람 박스 테두리 색상 보관
    QColor m_forkliftColor{0xff, 0xb7, 0x4d};                                  // - 지게차 색상: 지게차 박스 테두리 색상 보관
    QColor m_lineColor{0xff, 0xff, 0xff};                                      // - 선 색상: 거리 연결선 색상 보관
};
