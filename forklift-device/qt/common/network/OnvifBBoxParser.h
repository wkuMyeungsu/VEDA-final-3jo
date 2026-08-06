#pragma once

#include <QObject>
#include <QXmlStreamReader>

#include "../models/BBox.h"

// - ONVIF 메타데이터 XML 해석 클래스 (사람 영역 데이터를 추출 및 정규화하여 신호 전달)
class OnvifBBoxParser : public QObject
{
    Q_OBJECT

public:
    explicit OnvifBBoxParser(QObject *parent = nullptr);                       // - 생성자: 부모 객체 지정 및 초기화

public slots:
    void processMetadata(const QByteArray &xml);                               // - 데이터 처리: XML 메타데이터 해석 및 좌표 추출

signals:
    void personDetected(const BBox &bbox);                                     // - 감지 알림: 사람 영역(BBox) 검출 시 발생 (미감지 시 무효 영역 전달)

private:
    static bool parseObject(QXmlStreamReader &reader, BBox &outBBox, double &outDistance); // - 객체 해석: XML 데이터에서 좌표 및 거리 정보 추출
};
