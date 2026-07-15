#include "BBox.h"

BBox::BBox(double x, double y, double width, double height)
    : m_x(x), m_y(y), m_width(width), m_height(height)
{
}

BBox BBox::fromJson(const QJsonObject &obj)
{
    return BBox(obj.value(QStringLiteral("x")).toDouble(),
                obj.value(QStringLiteral("y")).toDouble(),
                obj.value(QStringLiteral("width")).toDouble(),
                obj.value(QStringLiteral("height")).toDouble());
}

QJsonObject BBox::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("x")] = m_x;
    obj[QStringLiteral("y")] = m_y;
    obj[QStringLiteral("width")] = m_width;
    obj[QStringLiteral("height")] = m_height;
    return obj;
}

bool BBox::operator==(const BBox &other) const
{
    return qFuzzyCompare(m_x + 1.0, other.m_x + 1.0)
        && qFuzzyCompare(m_y + 1.0, other.m_y + 1.0)
        && qFuzzyCompare(m_width + 1.0, other.m_width + 1.0)
        && qFuzzyCompare(m_height + 1.0, other.m_height + 1.0);
}
