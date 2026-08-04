#pragma once

#include <QObject>
#include <QXmlStreamReader>

#include "../models/BBox.h"

// ONVIF 메타데이터 XML(raw)에서 Human bbox만 뽑아 정규화해서 내보내는 파서.
// RtspVideoSource::onvifMetadataReceived를 그대로 받아 처리.
class OnvifBBoxParser : public QObject
{
    Q_OBJECT

public:
    explicit OnvifBBoxParser(QObject *parent = nullptr);

public slots:
    void processMetadata(const QByteArray &xml);

signals:
    // 검출 없으면 기본(무효) BBox로 emit -> 오버레이 자동 해제
    void personDetected(const BBox &bbox);

private:
    static bool parseObject(QXmlStreamReader &reader, BBox &outBBox, double &outDistance);
};
