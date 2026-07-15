#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <qqmlintegration.h>

// Normalized (0.0-1.0) bounding box, independent of source video resolution.
// Exposed to QML as a value type so it can be read straight out of
// RiskMetadata without an extra wrapper QObject.
class BBox
{
    Q_GADGET
    QML_VALUE_TYPE(bbox)
    Q_PROPERTY(double x READ x WRITE setX)
    Q_PROPERTY(double y READ y WRITE setY)
    Q_PROPERTY(double width READ width WRITE setWidth)
    Q_PROPERTY(double height READ height WRITE setHeight)
    Q_PROPERTY(bool valid READ isValid)

public:
    BBox() = default;
    BBox(double x, double y, double width, double height);

    double x() const { return m_x; }
    void setX(double x) { m_x = x; }

    double y() const { return m_y; }
    void setY(double y) { m_y = y; }

    double width() const { return m_width; }
    void setWidth(double width) { m_width = width; }

    double height() const { return m_height; }
    void setHeight(double height) { m_height = height; }

    // False when this box represents "no detection" (default-constructed).
    bool isValid() const { return m_width > 0.0 && m_height > 0.0; }

    static BBox fromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

    bool operator==(const BBox &other) const;
    bool operator!=(const BBox &other) const { return !(*this == other); }

private:
    double m_x = 0.0;
    double m_y = 0.0;
    double m_width = 0.0;
    double m_height = 0.0;
};

Q_DECLARE_METATYPE(BBox)
