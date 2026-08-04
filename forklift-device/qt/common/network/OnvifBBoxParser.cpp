#include "OnvifBBoxParser.h"

#include <QLoggingCategory>
#include <limits>

namespace {
Q_LOGGING_CATEGORY(lcOnvifBBox, "safety.onvif.bbox")
// 카메라 원본 해상도(profile1 기준) — BoundingBox 픽셀 좌표의 정규화 기준
constexpr double kRefWidth = 2592.0;
constexpr double kRefHeight = 1520.0;
}

OnvifBBoxParser::OnvifBBoxParser(QObject *parent)
    : QObject(parent)
{
}

void OnvifBBoxParser::processMetadata(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    BBox nearestBox;
    double nearestDistance = std::numeric_limits<double>::max();
    bool found = false;

    while (!reader.atEnd() && !reader.hasError()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QLatin1String("Object"))
            continue;

        BBox box;
        double distance = 0.0;
        if (parseObject(reader, box, distance) && distance < nearestDistance) {
            nearestBox = box;
            nearestDistance = distance;
            found = true;
        }
    }

    if (reader.hasError())
        qCWarning(lcOnvifBBox) << "XML parse error:" << reader.errorString();
    qCDebug(lcOnvifBBox) << "frame result -- found:" << found << "nearestDistance:" << nearestDistance;

    emit personDetected(found ? nearestBox : BBox());
}

bool OnvifBBoxParser::parseObject(QXmlStreamReader &reader, BBox &outBBox, double &outDistance)
{
    QString classType;
    float left = 0, top = 0, right = 0, bottom = 0;
    bool hasBBox = false;
    bool hasDistance = false;
    bool insideClassCandidate = false;

    while (!reader.atEnd() && !reader.hasError()) {
        reader.readNext();
        const auto tokenName = reader.name();

        if (reader.isEndElement()) {
            if (tokenName == QLatin1String("Object"))
                break;
            if (tokenName == QLatin1String("ClassCandidate"))
                insideClassCandidate = false;
            continue;
        }
        if (!reader.isStartElement())
            continue;

        if (tokenName == QLatin1String("BoundingBox")) {
            const auto attrs = reader.attributes();
            left = attrs.value(QLatin1String("left")).toString().toFloat();
            top = attrs.value(QLatin1String("top")).toString().toFloat();
            right = attrs.value(QLatin1String("right")).toString().toFloat();
            bottom = attrs.value(QLatin1String("bottom")).toString().toFloat();
            hasBBox = true;
        } else if (tokenName == QLatin1String("ClassCandidate")) {
            insideClassCandidate = true;
        } else if (tokenName == QLatin1String("Type") && !insideClassCandidate) {
            // ClassCandidate 안이 아닌, Class 직계 자식 Type만 최종 클래스로 채택
            classType = reader.readElementText();
        } else if (tokenName == QLatin1String("ProximateObject") && !hasDistance) {
            // ProximateObjects가 여럿이면 첫 항목만 사용 (단순화)
            const auto attrs = reader.attributes();
            if (attrs.hasAttribute(QLatin1String("Distance"))) {
                outDistance = attrs.value(QLatin1String("Distance")).toString().toDouble();
                hasDistance = true;
            }
        }
    }

    qCDebug(lcOnvifBBox) << "object class:" << classType << "hasBBox:" << hasBBox << "left:" << left << "top:" << top
                         << "right:" << right << "bottom:" << bottom << "hasDistance:" << hasDistance
                         << "distance:" << outDistance;

    if (classType != QLatin1String("Human") || !hasBBox)
        return false;
    if (!hasDistance || outDistance <= 0.0)
        return false; // 거리값 0(또는 없음)은 버림

    outBBox = BBox(left / kRefWidth, top / kRefHeight, (right - left) / kRefWidth, (bottom - top) / kRefHeight);
    return true;
}
