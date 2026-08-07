#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <qqmlintegration.h>

// - 객체 감지 영역(Bounding Box) 데이터 클래스 (비율 기준 좌표 0.0~1.0 보관 및 QML 연동 지원)
class BBox
{
    Q_GADGET                                                                       // - QML 값 타입 등록용 매크로
    QML_VALUE_TYPE(bbox)                                                           // - QML 타입명 지정: 'bbox'로 QML 접근 가능
    Q_PROPERTY(double x READ x WRITE setX)                                         // - X 좌표 속성: 좌측 상단 X 비율 좌표
    Q_PROPERTY(double y READ y WRITE setY)                                         // - Y 좌표 속성: 좌측 상단 Y 비율 좌표
    Q_PROPERTY(double width READ width WRITE setWidth)                             // - 너비 속성: 사각형 너비 비율
    Q_PROPERTY(double height READ height WRITE setHeight)                          // - 높이 속성: 사각형 높이 비율
    Q_PROPERTY(bool valid READ isValid)                                           // - 유효성 속성: 감지 영역 유효 여부

public:
    BBox() = default;                                                              // - 기본 생성자: 초기화되지 않은 무효 영역 생성
    BBox(double x, double y, double width, double height);                         // - 매개변수 생성자: 좌표 및 크기 지정 생성

    double x() const { return m_x; }                                               // - X 좌표 조회: X 비율 좌표 반환
    void setX(double x) { m_x = x; }                                               // - X 좌표 설정: X 비율 좌표 변경

    double y() const { return m_y; }                                               // - Y 좌표 조회: Y 비율 좌표 반환
    void setY(double y) { m_y = y; }                                               // - Y 좌표 설정: Y 비율 좌표 변경

    double width() const { return m_width; }                                       // - 너비 조회: 너비 비율 반환
    void setWidth(double width) { m_width = width; }                               // - 너비 설정: 너비 비율 변경

    double height() const { return m_height; }                                     // - 높이 조회: 높이 비율 반환
    void setHeight(double height) { m_height = height; }                           // - 높이 설정: 높이 비율 변경

    bool isValid() const { return m_width > 0.0 && m_height > 0.0; }              // - 유효성 검증: 너비와 높이가 0보다 큰 경우 유효 판단

    static BBox fromJson(const QJsonObject &obj);                                  // - JSON 파싱 생성: JSON 객체 데이터를 BBox 객체로 변환
    QJsonObject toJson() const;                                                    // - JSON 변환: 현재 BBox 데이터를 JSON 객체로 구성하여 반환

    bool operator==(const BBox &other) const;                                      // - 동등성 비교: 두 BBox 객체 간 좌표 일치 여부 판별
    bool operator!=(const BBox &other) const { return !(*this == other); }          // - 부등성 비교: 두 BBox 객체 간 불일치 여부 판별

private:
    double m_x = 0.0;                                                              // - X 좌표: 좌측 상단 X 비율 위치 (0.0~1.0)
    double m_y = 0.0;                                                              // - Y 좌표: 좌측 상단 Y 비율 위치 (0.0~1.0)
    double m_width = 0.0;                                                          // - 너비: 사각형 너비 비율 (0.0~1.0)
    double m_height = 0.0;                                                         // - 높이: 사각형 높이 비율 (0.0~1.0)
};

Q_DECLARE_METATYPE(BBox)                                                           // - 메타 타입 선언: Qt 신호/슬롯 시스템 연동 등록
