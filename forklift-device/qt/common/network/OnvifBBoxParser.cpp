#include "OnvifBBoxParser.h"

#include <QLoggingCategory>
#include <limits>

namespace {
Q_LOGGING_CATEGORY(lcOnvifBBox, "safety.onvif.bbox")                             // - 로깅 카테고리 정의: ONVIF BBox 로그 분류용 이름 지정
constexpr double kRefWidth = 2592.0;                                            // - 원본 해상도 너비 (2592px): 좌표 정규화 기준값 지정
constexpr double kRefHeight = 1520.0;                                           // - 원본 해상도 높이 (1520px): 좌표 정규화 기준값 지정
}

OnvifBBoxParser::OnvifBBoxParser(QObject *parent)
    : QObject(parent)
{
}

void OnvifBBoxParser::processMetadata(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);                                               // - XML 리더 생성: 수신된 바이너리 XML 데이터 연결
    BBox nearestBox;                                                            // - 가장 가까운 객체 상자: 최고 우선순위 BBox 저장용
    double nearestDistance = std::numeric_limits<double>::max();               // - 최소 거리 초기화: 실수 최대값으로 설정
    bool found = false;                                                         // - 검출 여부 플래그: 사람 객체 발견 상태 보관

    while (!reader.atEnd() && !reader.hasError()) {                             // - XML 문서 탐색: 데이터 끝 또는 에러 발생 전까지 반복
        reader.readNext();                                                      // - 다음 노드 읽기: XML 스트림 노드 이동
        if (!reader.isStartElement() || reader.name() != QLatin1String("Object")) // - 시작 태그 검증: 'Object' 시작 태그가 아닌 경우 스킵
            continue;

        BBox box;                                                               // - 객체 영역 상자: 단일 객체의 BBox 저장용
        double distance = 0.0;                                                  // - 객체 거리: 단일 객체의 거리 값 저장용
        if (parseObject(reader, box, distance) && distance < nearestDistance) { // - 객체 해석 및 거리 비교: 유효 객체 중 최단 거리 항목 갱신
            nearestBox = box;                                                   // - BBox 갱신: 최단 거리 객체의 영역 저장
            nearestDistance = distance;                                         // - 최단 거리 갱신: 최소 거리값 업데이트
            found = true;                                                       // - 검출 플래그 설정: 사람 객체 발견 표시
        }
    }

    if (reader.hasError())                                                      // - 에러 확인: XML 파싱 중 오류 발생 시 처리
        qCWarning(lcOnvifBBox) << "XML parse error:" << reader.errorString();   // - 경고 로그 출력: XML 분석 오류 내용 기록
    qCDebug(lcOnvifBBox) << "frame result -- found:" << found << "nearestDistance:" << nearestDistance; // - 디버그 로그: 검출 결과 출력

    emit personDetected(found ? nearestBox : BBox());                            // - 신호 발생: 사람 객체 검출 시 최단 거리 BBox 전달 (미검출 시 빈 BBox)
}

bool OnvifBBoxParser::parseObject(QXmlStreamReader &reader, BBox &outBBox, double &outDistance)
{
    QString classType;                                                          // - 객체 분류 유형: 사람/차량 등 클래스 이름 보관
    float left = 0, top = 0, right = 0, bottom = 0;                             // - 영역 좌표: BBox 사각형 pixel 좌표 변수
    bool hasBBox = false;                                                       // - BBox 존재 여부: 영역 좌표 수신 상태 보관
    bool hasDistance = false;                                                   // - 거리 존재 여부: 거리 정보 수신 상태 보관
    bool insideClassCandidate = false;                                          // - 후보 클래스 영역 여부: 내부 노드 판단용 플래그

    while (!reader.atEnd() && !reader.hasError()) {                             // - Object 내부 탐색: 태그 끝 또는 에러 발생 전까지 반복
        reader.readNext();                                                      // - 다음 노드 읽기: XML 스트림 노드 이동
        const auto tokenName = reader.name();                                   // - 현재 태그 이름: 태그 명칭 추출

        if (reader.isEndElement()) {                                            // - 종료 태그 처리: 태그 닫힘 여부 확인
            if (tokenName == QLatin1String("Object"))                           // - Object 종료: 현재 객체 탐색 완료 시 반복 탈출
                break;
            if (tokenName == QLatin1String("ClassCandidate"))                   // - ClassCandidate 종료: 후보 영역 탐색 완료 처리
                insideClassCandidate = false;
            continue;
        }
        if (!reader.isStartElement())                                           // - 시작 태그 확인: 시작 태그가 아닌 경우 스킵
            continue;

        if (tokenName == QLatin1String("BoundingBox")) {                        // - BoundingBox 태그: 영역 좌표 속성 추출
            const auto attrs = reader.attributes();                            // - 속성 목록 가져오기: 태그 내 속성 데이터 수집
            left = attrs.value(QLatin1String("left")).toString().toFloat();      // - 좌측 좌표 추출: left 속성값 읽기
            top = attrs.value(QLatin1String("top")).toString().toFloat();        // - 상단 좌표 추출: top 속성값 읽기
            right = attrs.value(QLatin1String("right")).toString().toFloat();    // - 우측 좌표 추출: right 속성값 읽기
            bottom = attrs.value(QLatin1String("bottom")).toString().toFloat();  // - 하단 좌표 추출: bottom 속성값 읽기
            hasBBox = true;                                                     // - 플래그 설정: BBox 수신 완료 표시
        } else if (tokenName == QLatin1String("ClassCandidate")) {              // - ClassCandidate 태그: 후보 객체 구간 진입
            insideClassCandidate = true;                                        // - 플래그 설정: 후보 객체 구간 표시
        } else if (tokenName == QLatin1String("Type") && !insideClassCandidate) { // - Type 태그: 직계 분류 이름 추출 (후보 구간 제외)
            classType = reader.readElementText();                               // - 클래스명 수집: 태그 텍스트 데이터 읽기
        } else if (tokenName == QLatin1String("ProximateObject") && !hasDistance) { // - ProximateObject 태그: 첫 번째 거리 정보 추출
            const auto attrs = reader.attributes();                            // - 속성 목록 가져오기: 태그 내 속성 데이터 수집
            if (attrs.hasAttribute(QLatin1String("Distance"))) {                // - Distance 속성 확인: 거리 정보 존재 시 추출
                outDistance = attrs.value(QLatin1String("Distance")).toString().toDouble(); // - 거리값 추출: Distance 속성값 읽기
                hasDistance = true;                                             // - 플래그 설정: 거리값 수신 완료 표시
            }
        }
    }

    qCDebug(lcOnvifBBox) << "object class:" << classType << "hasBBox:" << hasBBox << "left:" << left << "top:" << top
                         << "right:" << right << "bottom:" << bottom << "hasDistance:" << hasDistance
                         << "distance:" << outDistance;                        // - 디버그 로그: 객체 정보 상세 출력

    if (classType != QLatin1String("Human") || !hasBBox)                       // - 유효성 검증: 사람이 아니거나 BBox가 없는 경우 필터링
        return false;
    if (!hasDistance || outDistance <= 0.0)                                     // - 거리 검증: 거리 정보가 없거나 0 이하인 경우 필터링
        return false;

    outBBox = BBox(left / kRefWidth, top / kRefHeight, (right - left) / kRefWidth, (bottom - top) / kRefHeight); // - 좌표 정규화: 0.0~1.0 비율 좌표로 변환 및 저장
    return true;                                                                // - 처리 성공: 유효 객체 데이터 반환
}
