#include "RiskMetadata.h"

RiskMetadata RiskMetadata::fromJson(const QJsonObject &obj)
{
    RiskMetadata meta;                                                                 // - 객체 생성: 메타데이터 보관 객체 초기화
    meta.setCameraId(obj.value(QStringLiteral("camera_id")).toString());               // - 카메라 ID 설정: 수신된 카메라 식별자 파싱
    meta.setZone(obj.value(QStringLiteral("zone")).toString());                         // - 구역 설정: 설치 위치 정보 파싱
    meta.setRiskLevel(RiskTypes::riskLevelFromInt(obj.value(QStringLiteral("risk_level")).toInt())); // - 위험 단계 설정: 정수값을 위험 열거형으로 변환하여 저장
    const QJsonValue distanceValue = obj.value(QStringLiteral("distance_m"));          // - 거리 값 추출: JSON 내 distance_m 속성 확인
    meta.setDistanceValid(distanceValue.isDouble());                                   // - 유효성 설정: 실수값 존재 여부 확인 후 유효성 설정
    meta.setDistanceM(distanceValue.isDouble() ? distanceValue.toDouble() : 0.0);      // - 거리 설정: 유효한 경우 측정 거리(m) 저장
    meta.setExceptionState(RiskTypes::exceptionStateFromString(obj.value(QStringLiteral("exception_state")).toString())); // - 예외 상태 설정: 문자열을 예외 상태 열거형으로 변환하여 저장
    meta.setUtcTime(QDateTime::fromString(obj.value(QStringLiteral("utc_time")).toString(), Qt::ISODateWithMs)); // - 시각 설정: ISO 시각 문자열을 QDateTime으로 파싱하여 저장
    return meta;                                                                       // - 객체 반환: 완성된 메타데이터 반환
}

QJsonObject RiskMetadata::toJson() const
{
    QJsonObject obj;                                                                   // - JSON 객체 생성: 출력용 JSON 개체 초기화
    obj[QStringLiteral("camera_id")] = m_cameraId;                                     // - 카메라 ID 추가: 현재 카메라 식별자 설정
    obj[QStringLiteral("zone")] = m_zone;                                             // - 구역 추가: 현재 설치 위치 정보 설정
    obj[QStringLiteral("risk_level")] = static_cast<int>(m_riskLevel);                 // - 위험 단계 추가: 위험 수치를 정수형으로 변환 설정
    obj[QStringLiteral("distance_m")] = m_distanceM;                                   // - 거리 추가: 현재 측정 거리(m) 설정
    obj[QStringLiteral("exception_state")] = RiskTypes::exceptionStateToString(m_exceptionState); // - 예외 상태 추가: 예외 상태를 문자열로 변환 설정
    obj[QStringLiteral("utc_time")] = m_utcTime.toUTC().toString(Qt::ISODateWithMs);   // - 시각 추가: UTC 시각을 ISO 문자열로 변환 설정
    return obj;                                                                        // - JSON 반환: 구성된 JSON 객체 반환
}
