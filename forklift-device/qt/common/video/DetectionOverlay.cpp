#include "DetectionOverlay.h"

#include <QPainter>
#include <QPainterPath>

#include "AspectFit.h"

DetectionOverlay::DetectionOverlay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFillColor(Qt::transparent);                                             // - 투명 채우기 설정: 배경 투명화 처리
    setAntialiasing(true);                                                      // - 안티앨리어싱 설정: 테두리 부드럽게 표현
}

void DetectionOverlay::setVideoSize(const QSize &size)
{
    if (m_videoSize == size)                                                   // - 중복 변경 방지: 기존 영상 크기와 동일한 경우 생략
        return;
    m_videoSize = size;                                                        // - 영상 크기 갱신: 새로운 영상 크기 저장
    emit videoSizeChanged();                                                   // - 신호 발생: 영상 크기 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::setPersonBBox(const BBox &box)
{
    if (m_personBBox == box)                                                   // - 중복 변경 방지: 기존 좌표와 동일한 경우 생략
        return;
    m_personBBox = box;                                                        // - 사람 영역 갱신: 새로운 사람 BBox 좌표 저장
    emit personBBoxChanged();                                                  // - 신호 발생: 사람 영역 좌표 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::setForkliftBBox(const BBox &box)
{
    if (m_forkliftBBox == box)                                                 // - 중복 변경 방지: 기존 좌표와 동일한 경우 생략
        return;
    m_forkliftBBox = box;                                                      // - 지게차 영역 갱신: 새로운 지게차 BBox 좌표 저장
    emit forkliftBBoxChanged();                                                // - 신호 발생: 지게차 영역 좌표 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::setDistanceM(double distance)
{
    if (qFuzzyCompare(m_distanceM + 1.0, distance + 1.0))                      // - 중복 변경 방지: 기존 거리값과 동일한 경우 생략
        return;
    m_distanceM = distance;                                                    // - 측정 거리 갱신: 새로운 감지 거리(m) 저장
    emit distanceMChanged();                                                   // - 신호 발생: 감지 거리 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::setDistanceValid(bool valid)
{
    if (m_distanceValid == valid)                                              // - 중복 변경 방지: 기존 유효성 상태와 동일한 경우 생략
        return;
    m_distanceValid = valid;                                                   // - 거리 유효성 갱신: 새로운 유효성 상태 저장
    emit distanceValidChanged();                                               // - 신호 발생: 거리 유효성 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::setPersonColor(const QColor &color)
{
    if (m_personColor == color)                                                // - 중복 변경 방지: 기존 색상과 동일한 경우 생략
        return;
    m_personColor = color;                                                     // - 사람 영역 색상 갱신: 박스 표시 색상 저장
    emit personColorChanged();                                                 // - 신호 발생: 사람 영역 색상 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::setForkliftColor(const QColor &color)
{
    if (m_forkliftColor == color)                                              // - 중복 변경 방지: 기존 색상과 동일한 경우 생략
        return;
    m_forkliftColor = color;                                                   // - 지게차 영역 색상 갱신: 박스 표시 색상 저장
    emit forkliftColorChanged();                                               // - 신호 발생: 지게차 영역 색상 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::setLineColor(const QColor &color)
{
    if (m_lineColor == color)                                                  // - 중복 변경 방지: 기존 색상과 동일한 경우 생략
        return;
    m_lineColor = color;                                                       // - 거리 선 색상 갱신: 연결선 표시 색상 저장
    emit lineColorChanged();                                                   // - 신호 발생: 거리 선 색상 변경 알림
    update();                                                                  // - 화면 갱신: 오버레이 다시 그리기 요청
}

void DetectionOverlay::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);              // - 영역 변경 처리: 부모 클래스 위치/크기 변경 함수 호출
    update();                                                                  // - 화면 갱신: 변경된 크기에 맞춰 다시 그리기 요청
}

void DetectionOverlay::paint(QPainter *painter)
{
    const QRectF bounds(0, 0, width(), height());                              // - 그려질 영역 생성: 오버레이 아이템 전체 영역
    if (bounds.isEmpty())                                                      // - 영역 검증: 크기가 없으면 그리기 생략
        return;

    const QRectF videoRect = AspectFit::fitRect(bounds.size(), m_videoSize);   // - 영상 표시 영역 계산: 원본 비율 유지 표시 영역 획득

    if (m_personBBox.isValid())                                                // - 사람 검출 영역 확인: 유효한 사람 BBox 영역 그리기
        drawBox(painter, videoRect, m_personBBox, m_personColor, QStringLiteral("PERSON"));
    if (m_forkliftBBox.isValid())                                              // - 지게차 검출 영역 확인: 유효한 지게차 BBox 영역 그리기
        drawBox(painter, videoRect, m_forkliftBBox, m_forkliftColor, QStringLiteral("FORKLIFT"));

    // 거리 라벨은 personBBox만 있으면 표시 -- forkliftBBox는 ArUco 연동을 안 하기로
    // 결정되어 앞으로도 계속 invalid라 조건에서 뺌. distanceM/distanceValid는 서버에서
    // personBBox와 무관하게 독립적으로 내려오는 값이라 그대로 씀
    if (m_personBBox.isValid()) {                                              // - 사람 검출 확인: 사람 BBox가 유효할 때만 거리 표시
        const QRectF personRect = AspectFit::mapNormalizedRect(               // - 사람 픽셀 좌표 변환: 비율 좌표를 화면 픽셀 좌표로 변환
            QRectF(m_personBBox.x(), m_personBBox.y(), m_personBBox.width(), m_personBBox.height()), videoRect);

        const QString distanceLabel = m_distanceValid ? QStringLiteral("%1 m").arg(m_distanceM, 0, 'f', 2)
                                                       : QStringLiteral("측정 불가"); // - 거리 텍스트 구성: 유효성에 따른 텍스트 생성

        QFont font = painter->font();                                          // - 폰트 가져오기: 그려질 텍스트 폰트 설정
        font.setPointSize(11);                                                 // - 글자 크기 설정: 11pt 지정
        font.setBold(true);                                                    // - 굵게 설정: 굵은 글씨체 지정
        painter->setFont(font);                                                // - 폰트 적용: 그려질 폰트 등록

        const QFontMetrics metrics(font);                                      // - 폰트 측정기 생성: 텍스트 영역 계산용
        const QPointF anchor = personRect.topRight() + QPointF(6, -6);         // - 배지 기준점 산출: 사람 상자 우상단 바깥쪽 위치
        QRectF textRect = QRectF(metrics.boundingRect(distanceLabel)).adjusted(-8, -4, 8, 4).translated(anchor); // - 라벨 영역 산출: 여백 포함 영역 지정

        // personBBox가 화면 가장자리에 가까우면 배지가 오버레이 밖으로 나가서 안 보일 수
        // 있음 -- 그리는 영역(bounds) 안으로 되돌림
        if (textRect.top() < bounds.top())                                     // - 위쪽 이탈 확인: 배지가 위로 벗어나면 되돌림
            textRect.moveTop(bounds.top());                                    // - 위치 보정: 위쪽 경계에 맞춤
        if (textRect.right() > bounds.right())                                 // - 오른쪽 이탈 확인: 배지가 우측으로 벗어나면 되돌림
            textRect.moveRight(bounds.right());                                // - 위치 보정: 오른쪽 경계에 맞춤

        QPainterPath badgePath;                                                // - 배지 경로 생성: 둥근 사각형 배경 경로 생성
        badgePath.addRoundedRect(textRect, 6, 6);                              // - 둥근 모서리 추가: 모서리 반경 지정
        painter->fillPath(badgePath, QColor(0, 0, 0, 170));                    // - 배경 채우기: 반투명 검은색 채우기
        painter->setPen(Qt::white);                                            // - 텍스트 펜 설정: 흰색 글씨용 펜 지정
        painter->drawText(textRect, Qt::AlignCenter, distanceLabel);            // - 텍스트 그리기: 중앙 정렬 거리 라벨 그리기
    }
}

void DetectionOverlay::drawBox(QPainter *painter, const QRectF &videoRect, const BBox &box, const QColor &color,
                                const QString &label) const
{
    const QRectF rect = AspectFit::mapNormalizedRect(QRectF(box.x(), box.y(), box.width(), box.height()), videoRect); // - 픽셀 좌표 변환: BBox 좌표를 화면 픽셀로 변환

    QPen pen(color);                                                           // - 영역 펜 생성: BBox 테두리 펜 설정
    pen.setWidthF(2.0);                                                        // - 선 두께 설정: 테두리 두께 지정
    painter->setPen(pen);                                                      // - 펜 적용: 그려질 펜 등록
    painter->setBrush(Qt::NoBrush);                                            // - 브러시 해제: 내부 채우기 안 함
    painter->drawRoundedRect(rect, 4, 4);                                      // - 영역 상자 그리기: 둥근 모서리 사각형 그리기

    QFont font = painter->font();                                              // - 폰트 가져오기: 라벨용 폰트 설정
    font.setPointSize(9);                                                      // - 글자 크기 설정: 9pt 지정
    font.setBold(true);                                                        // - 굵게 설정: 굵은 글씨체 지정
    painter->setFont(font);                                                    // - 폰트 적용: 그려질 폰트 등록

    const QFontMetrics metrics(font);                                          // - 폰트 측정기 생성: 텍스트 영역 계산용
    QRectF labelRect = metrics.boundingRect(label).adjusted(-6, -3, 6, 3);      // - 라벨 영역 산출: 여백 포함 영역 지정
    labelRect.moveBottomLeft(rect.topLeft());                                  // - 라벨 위치 이동: 상자 좌상단 위쪽 배치

    QPainterPath labelPath;                                                    // - 라벨 배경 경로 생성: 둥근 사각형 경로 생성
    labelPath.addRoundedRect(labelRect, 4, 4);                                 // - 둥근 모서리 추가: 모서리 반경 지정
    painter->fillPath(labelPath, color);                                       // - 배경 채우기: 지정 색상 채우기
    painter->setPen(Qt::black);                                                // - 텍스트 펜 설정: 검은색 글씨용 펜 지정
    painter->drawText(labelRect, Qt::AlignCenter, label);                       // - 텍스트 그리기: 라벨 명칭 중앙 정렬 그리기
}
