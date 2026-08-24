#pragma once

#include <QObject>

#include "../models/Types.h"

// - 물리 경고 장치 제어 인터페이스 (FPGA 경고 보드: 부저/경고등/전진차단 릴레이 등 하드웨어 제어 추상 클래스)
// - 프로토콜 상세는 forklift-device/gpio-control/src/PROTOCOL.md 참고
class IWarningDevice : public QObject
{
    Q_OBJECT

public:
    explicit IWarningDevice(QObject *parent = nullptr) : QObject(parent) {}     // - 생성자: 부모 객체 지정 및 초기화
    ~IWarningDevice() override = default;                                       // - 소멸자: 기본 소멸자 지정

    virtual void setRiskLevel(RiskTypes::RiskLevel level) = 0;                  // - 위험 단계 설정: 위험 수치에 따른 경고 작동 순수 가상 함수 (SET_RISK)
    virtual void sendClearError() = 0;                                          // - 오류 플래그 초기화 요청 순수 가상 함수 (CLEAR_ERROR)
    virtual void sendSelfTest(int mode) = 0;                                    // - 자가진단 요청 순수 가상 함수 (SELF_TEST, 0=STOP/1=LED/2=BUZZER/3=ALL)

    bool estopActive() const { return m_estopActive; }                          // - 비상정지 상태 조회: 물리 ESTOP 버튼 눌림 여부
    bool movementCutoffActive() const { return m_movementCutoffActive; }        // - 전진 차단 상태 조회: 조종기 전진 릴레이 차단 작동 여부
    bool commError() const { return m_commError; }                              // - FPGA 관점 통신 오류 조회: watchdog(500ms) 타임아웃 여부
    bool checksumErrorLatched() const { return m_checksumErrorLatched; }        // - 체크섬 오류 누적 플래그 조회: CLEAR_ERROR 전까지 유지
    bool protocolErrorLatched() const { return m_protocolErrorLatched; }        // - 프로토콜 오류 누적 플래그 조회: CLEAR_ERROR 전까지 유지
    bool timeoutErrorLatched() const { return m_timeoutErrorLatched; }          // - 타임아웃 오류 누적 플래그 조회: CLEAR_ERROR 전까지 유지
    RiskTypes::ConnectionState fpgaConnectionState() const { return m_fpgaConnectionState; } // - FPGA 연결 상태 조회: heartbeat 수신 여부 기반
    bool riskTxSuspended() const { return m_riskTxSuspended; }                  // - SET_RISK 송신 중단 여부 조회

    // - 서버 링크 두절 구간에서 SET_RISK 송신을 멈춘다.
    //   FPGA watchdog(500ms)이 갱신되지 않아야 자율 COMM_ERROR 경고가 걸린다.
    void setRiskTxSuspended(bool suspended)
    {
        if (m_riskTxSuspended == suspended)
            return;
        m_riskTxSuspended = suspended;
        onRiskTxSuspendedChanged(suspended);                                    // - 구현체 훅: 재개 시 즉시 1회 송신 등
        emit riskTxSuspendedChanged(suspended);
    }

signals:
    void estopActiveChanged(bool active);                                       // - 비상정지 상태 변경 알림
    void movementCutoffActiveChanged(bool active);                              // - 전진 차단 상태 변경 알림
    void commErrorChanged(bool active);                                         // - FPGA 통신 오류 상태 변경 알림
    void errorLatchChanged();                                                   // - 오류 누적 플래그 변경 알림 (checksum/protocol/timeout 중 하나라도 변경 시)
    void fpgaConnectionStateChanged(RiskTypes::ConnectionState state);          // - FPGA 연결 상태 변경 알림 (heartbeat 무수신 600ms 등)
    void riskTxSuspendedChanged(bool suspended);                                // - SET_RISK 송신 중단 상태 변경 알림

protected:
    virtual void onRiskTxSuspendedChanged(bool suspended) { Q_UNUSED(suspended); } // - 기본 동작 없음

protected:
    // - 아래 setter들은 구현체(SerialWarningDevice 등)가 수신 데이터를 반영할 때만 호출.
    //   값이 실제로 바뀔 때만 신호를 발생시켜, QML의 불필요한 재바인딩을 방지한다.
    void setEstopActive(bool active)
    {
        if (m_estopActive == active)
            return;
        m_estopActive = active;
        emit estopActiveChanged(active);
    }

    void setMovementCutoffActive(bool active)
    {
        if (m_movementCutoffActive == active)
            return;
        m_movementCutoffActive = active;
        emit movementCutoffActiveChanged(active);
    }

    void setCommError(bool active)
    {
        if (m_commError == active)
            return;
        m_commError = active;
        emit commErrorChanged(active);
    }

    void setErrorLatches(bool checksum, bool protocolErr, bool timeout)
    {
        if (m_checksumErrorLatched == checksum && m_protocolErrorLatched == protocolErr
            && m_timeoutErrorLatched == timeout)
            return;
        m_checksumErrorLatched = checksum;
        m_protocolErrorLatched = protocolErr;
        m_timeoutErrorLatched = timeout;
        emit errorLatchChanged();
    }

    void setFpgaConnectionState(RiskTypes::ConnectionState state)
    {
        if (m_fpgaConnectionState == state)
            return;
        m_fpgaConnectionState = state;
        emit fpgaConnectionStateChanged(state);
    }

private:
    bool m_estopActive = false;                                                 // - 비상정지 상태 보관
    bool m_movementCutoffActive = false;                                        // - 전진 차단 상태 보관
    bool m_commError = false;                                                   // - FPGA 통신 오류 상태 보관
    bool m_checksumErrorLatched = false;                                        // - 체크섬 오류 누적 플래그 보관
    bool m_protocolErrorLatched = false;                                        // - 프로토콜 오류 누적 플래그 보관
    bool m_timeoutErrorLatched = false;                                         // - 타임아웃 오류 누적 플래그 보관
    RiskTypes::ConnectionState m_fpgaConnectionState = RiskTypes::ConnectionState::Disconnected; // - FPGA 연결 상태 보관
    bool m_riskTxSuspended = false;                                             // - SET_RISK 송신 중단 상태 보관
};
