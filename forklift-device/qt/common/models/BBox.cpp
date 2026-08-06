#include "BBox.h"

BBox::BBox(double x, double y, double width, double height)
    : m_x(x), m_y(y), m_width(width), m_height(height)                             // - 생성자: 사각형 좌표(x, y) 및 크기(너비, 높이) 초기화
{
}

BBox BBox::fromJson(const QJsonObject &obj)
{
    return BBox(obj.value(QStringLiteral("x")).toDouble(),                          // - JSON 파싱 생성: JSON 객체에서 좌표 및 크기 추출 후 객체 반환
                obj.value(QStringLiteral("y")).toDouble(),
                obj.value(QStringLiteral("width")).toDouble(),
                obj.value(QStringLiteral("height")).toDouble());
}

QJsonObject BBox::toJson() const
{
    QJsonObject obj;                                                               // - JSON 변환: 현재 사각형 데이터를 JSON 객체로 구성하여 반환
    obj[QStringLiteral("x")] = m_x;
    obj[QStringLiteral("y")] = m_y;
    obj[QStringLiteral("width")] = m_width;
    obj[QStringLiteral("height")] = m_height;
    return obj;
}

bool BBox::operator==(const BBox &other) const
{
    return qFuzzyCompare(m_x + 1.0, other.m_x + 1.0)                              // - 동등성 비교: 실수 오차를 고려한 좌표 및 크기 일치 여부 판별
        && qFuzzyCompare(m_y + 1.0, other.m_y + 1.0)
        && qFuzzyCompare(m_width + 1.0, other.m_width + 1.0)
        && qFuzzyCompare(m_height + 1.0, other.m_height + 1.0);
}
