#pragma once

#include <QObject>
#include <qqmlintegration.h>

// - 시스템 공통 열거형 정의 네임스페이스 (QML 및 C++ 공용 데이터 타입 제공)
namespace RiskTypes {
Q_NAMESPACE                                                                     // - Qt 네임스페이스 등록 매크로
QML_ELEMENT                                                                     // - QML 모듈 내 요소 등록 매크로

// - 위험 단계 열거형
enum class RiskLevel {
    Safe = 0,                                                                   // - 안전 단계: 위험 요소 없음 (0)
    Caution = 1,                                                                // - 주의 단계: 보행자/장애물 접근 (1)
    Danger = 2,                                                                 // - 경고 단계: 근접 위험 상태 (2)
    Emergency = 3                                                               // - 비상 단계: 즉시 정지 위험 상태 (3)
};
Q_ENUM_NS(RiskLevel)                                                            // - RiskLevel 열거형 네임스페이스 등록

// - 예외 상태 열거형
enum class ExceptionState {
    None,                                                                       // - 정상 상태: 예외 사항 없음
    SensorFault,                                                                // - 센서 오류: 센서 장치 고장/이상
    DeadReckoning,                                                              // - 위치 추정: 센서 미수신 시 위치 추정 동작
    EmergencyImpact,                                                            // - 충돌 발생: 비상 충돌 감지
    NetworkDisconnected,                                                        // - 통신 끊김: 네트워크 연결 해제
    CameraDisconnected,                                                         // - 카메라 끊김: 카메라 영상 신호 끊김
    UnconfirmedProximity                                                        // - 미확인 근접: 사각지대 근접 우려
};
Q_ENUM_NS(ExceptionState)                                                       // - ExceptionState 열거형 네임스페이스 등록

// - 통신 연결 상태 열거형
enum class ConnectionState {
    Disconnected,                                                               // - 끊김 상태: 서버/카메라 연결 해제
    Connecting,                                                                 // - 연결 중: 서버/카메라 접속 시도 중
    Connected                                                                   // - 연결됨: 서버/카메라 접속 완료
};
Q_ENUM_NS(ConnectionState)                                                      // - ConnectionState 열거형 네임스페이스 등록

QString riskLevelToString(RiskLevel level);                                     // - 문자열 변환: 위험 단계를 문자열로 변환
RiskLevel riskLevelFromInt(int value);                                          // - 열거형 변환: 정수값을 위험 단계로 변환

QString exceptionStateToString(ExceptionState state);                           // - 문자열 변환: 예외 상태를 문자열로 변환
ExceptionState exceptionStateFromString(const QString &value);                  // - 열거형 변환: 문자열을 예외 상태로 변환

QString connectionStateToString(ConnectionState state);                         // - 문자열 변환: 연결 상태를 문자열로 변환

} // namespace RiskTypes
