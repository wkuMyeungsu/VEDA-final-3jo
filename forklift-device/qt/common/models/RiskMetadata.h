#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <qqmlintegration.h>

#include "BBox.h"
#include "Types.h"

// - 위험 감지 메타데이터 클래스 (단일 카메라의 위험 수준, 거리, 객체 영역 및 시각 정보 보관)
class RiskMetadata
{
    Q_GADGET                                                                       // - QML 값 타입 등록용 매크로
    QML_VALUE_TYPE(riskMetadata)                                                   // - QML 타입명 지정: 'riskMetadata'로 QML 접근 가능
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId)                   // - 카메라 ID 속성: 카메라 식별자
    Q_PROPERTY(QString zone READ zone WRITE setZone)                               // - 구역 속성: 설치 위치 정보
    Q_PROPERTY(RiskTypes::RiskLevel riskLevel READ riskLevel WRITE setRiskLevel)   // - 위험 단계 속성: 위험 수준 (Safe/Warning/Danger 등)
    Q_PROPERTY(double distanceM READ distanceM WRITE setDistanceM)                 // - 측정 거리 속성: 감지 대상과의 거리(m)
    Q_PROPERTY(bool distanceValid READ distanceValid WRITE setDistanceValid)       // - 거리 유효성 속성: 거리 측정 가능 여부 (0.0m와 측정불가 구분용)
    Q_PROPERTY(BBox personBBox READ personBBox WRITE setPersonBBox)               // - 사람 영역 속성: 감지된 사람 위치 좌표
    Q_PROPERTY(BBox forkliftBBox READ forkliftBBox WRITE setForkliftBBox)         // - 지게차 영역 속성: 감지된 지게차 위치 좌표
    Q_PROPERTY(RiskTypes::ExceptionState exceptionState READ exceptionState WRITE setExceptionState) // - 예외 상태 속성: 시스템 예외 상황 정보
    Q_PROPERTY(QDateTime utcTime READ utcTime WRITE setUtcTime)                    // - 측정 시각 속성: 메타데이터 생성 UTC 시각

public:
    RiskMetadata() = default;                                                      // - 기본 생성자: 초기화된 기본 객체 생성

    QString cameraId() const { return m_cameraId; }                                // - 카메라 ID 조회: 식별용 ID 반환
    void setCameraId(const QString &id) { m_cameraId = id; }                        // - 카메라 ID 설정: 식별용 ID 지정

    QString zone() const { return m_zone; }                                        // - 구역 조회: 설치 위치 정보 반환
    void setZone(const QString &zone) { m_zone = zone; }                            // - 구역 설정: 설치 위치 정보 지정

    RiskTypes::RiskLevel riskLevel() const { return m_riskLevel; }                 // - 위험 단계 조회: 위험 수준 열거형 반환
    void setRiskLevel(RiskTypes::RiskLevel level) { m_riskLevel = level; }          // - 위험 단계 설정: 위험 수준 열거형 지정

    double distanceM() const { return m_distanceM; }                               // - 측정 거리 조회: 감지 거리(m) 반환
    void setDistanceM(double distance) { m_distanceM = distance; }                // - 측정 거리 설정: 감지 거리(m) 지정

    bool distanceValid() const { return m_distanceValid; }                         // - 거리 유효성 조회: 거리 측정 가능 여부 반환
    void setDistanceValid(bool valid) { m_distanceValid = valid; }                // - 거리 유효성 설정: 거리 측정 가능 여부 지정

    BBox personBBox() const { return m_personBBox; }                               // - 사람 영역 조회: 사람 위치 BBox 반환
    void setPersonBBox(const BBox &box) { m_personBBox = box; }                    // - 사람 영역 설정: 사람 위치 BBox 지정

    BBox forkliftBBox() const { return m_forkliftBBox; }                           // - 지게차 영역 조회: 지게차 위치 BBox 반환
    void setForkliftBBox(const BBox &box) { m_forkliftBBox = box; }                // - 지게차 영역 설정: 지게차 위치 BBox 지정

    RiskTypes::ExceptionState exceptionState() const { return m_exceptionState; }  // - 예외 상태 조회: 시스템 예외 상태 반환
    void setExceptionState(RiskTypes::ExceptionState state) { m_exceptionState = state; } // - 예외 상태 설정: 시스템 예외 상태 지정

    QDateTime utcTime() const { return m_utcTime; }                                // - 측정 시각 조회: 생성 시각(UTC) 반환
    void setUtcTime(const QDateTime &time) { m_utcTime = time; }                   // - 측정 시각 설정: 생성 시각(UTC) 지정

    static RiskMetadata fromJson(const QJsonObject &obj);                          // - JSON 파싱 생성: JSON 데이터를 RiskMetadata 객체로 변환
    QJsonObject toJson() const;                                                    // - JSON 변환: 현재 메타데이터를 JSON 객체로 구성하여 반환

private:
    QString m_cameraId;                                                            // - 카메라 ID: 카메라 식별 문자열 보관
    QString m_zone;                                                                // - 설치 구역: 카메라 설치 위치 보관
    RiskTypes::RiskLevel m_riskLevel = RiskTypes::RiskLevel::Safe;                 // - 위험 단계: 위험 수준 보관 (기본값: Safe)
    double m_distanceM = 0.0;                                                      // - 측정 거리: 감지 대상과의 거리(m) 보관
    bool m_distanceValid = true;                                                   // - 거리 유효성: 거리 측정 유효 여부 보관 (기본값: true)
    BBox m_personBBox;                                                             // - 사람 영역: 사람 BBox 좌표 보관
    BBox m_forkliftBBox;                                                           // - 지게차 영역: 지게차 BBox 좌표 보관
    RiskTypes::ExceptionState m_exceptionState = RiskTypes::ExceptionState::None;  // - 예외 상태: 시스템 예외 상태 보관 (기본값: None)
    QDateTime m_utcTime;                                                           // - 측정 시각: 데이터 생성 UTC 시각 보관
};

Q_DECLARE_METATYPE(RiskMetadata)                                                   // - 메타 타입 선언: Qt 신호/슬롯 시스템 연동 등록
