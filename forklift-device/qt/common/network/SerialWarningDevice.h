#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include "IWarningDevice.h"

// - FPGA 경고 보드(gpio-control)와 UART로 통신하는 실제 구현체.
//   프로토콜 상세: forklift-device/gpio-control/src/PROTOCOL.md
//   - Pi -> FPGA: SET_RISK(100ms 반복, watchdog 갱신용) / CLEAR_ERROR / SELF_TEST, 4바이트 고정
//   - FPGA -> Pi: heartbeat(200ms) + 이벤트, 5바이트 고정, 0x55 헤더로 재동기화
class SerialWarningDevice : public IWarningDevice
{
    Q_OBJECT
    friend class TestSerialWarningDevice;

public:
    SerialWarningDevice(QString portName, qint32 baudRate, QObject *parent = nullptr); // - 생성자: 포트 이름/보레이트 보관, 실제 연결은 start()에서
    ~SerialWarningDevice() override;                                                   // - 소멸자: 포트 정리

    void start();                                                                      // - 통신 시작: 포트 열기 + watchdog 송신/무수신 감시 타이머 구동
    void stop();                                                                       // - 통신 정지: 포트 닫기 + 타이머 정지, 이후 재연결 시도 안 함

    void setRiskLevel(RiskTypes::RiskLevel level) override;                            // - 위험 단계 값만 갱신 (실제 전송은 100ms 타이머가 전담, PROTOCOL.md 1.2절)
    void sendClearError() override;                                                    // - CLEAR_ERROR 1회 전송
    void sendSelfTest(int mode) override;                                              // - SELF_TEST 1회 전송

protected:
    void onRiskTxSuspendedChanged(bool suspended) override;                            // - 재개 시 100ms를 기다리지 않고 즉시 1회 송신

private slots:
    void handleReadyRead();                                                            // - 수신 바이트 누적 및 프레임 파싱
    void handleWatchdogTxTimer();                                                      // - 100ms마다 마지막 위험도로 SET_RISK 재전송
    void handleHeartbeatWatchTimer();                                                  // - 마지막 수신 이후 600ms 초과 시 FPGA 연결 끊김 처리
    void handleSerialError(QSerialPort::SerialPortError error);                        // - 포트 오류 시 재연결 예약
    void handleReconnectTimer();                                                       // - 대기 시간이 지난 뒤 포트 열기 재시도
    void openPort();                                                                   // - 포트 열기 시도 (실패 시 재연결 예약)

private:
    bool sendFrame(quint8 command, quint8 data);                                       // - [0xAA][command][data][checksum] 4바이트 전송 (성공 여부 반환)
    void processFrame(const QByteArray &frame);                                        // - checksum 검증 완료된 5바이트 수신 프레임 처리
    void scheduleReconnect();                                                          // - 3초 뒤 openPort() 재시도 (RtspVideoSource 패턴과 동일)

    QString m_portName;                                                                // - 시리얼 포트 이름 (예: /dev/serial0)
    qint32 m_baudRate;                                                                 // - 보레이트 (예: 115200)

    QSerialPort m_port;                                                                // - 시리얼 포트 객체
    QByteArray m_rxBuffer;                                                             // - 수신 바이트 누적 버퍼 (0x55 헤더 재동기화용)

    RiskTypes::RiskLevel m_lastRiskLevel = RiskTypes::RiskLevel::Safe;                 // - watchdog 타이머가 반복 전송할 마지막 위험도 값
    int m_lastTransmittedRiskLevel = -1;                                               // - 실제 송신된 마지막 위험도 값 (T3 계측용)

    QTimer m_watchdogTxTimer;                                                          // - 100ms 송신 타이머 (FPGA watchdog 타임아웃 500ms 대비 5배 여유)
    QTimer m_heartbeatWatchTimer;                                                      // - 무수신 감시 폴링 타이머 (100ms 간격 점검)
    QTimer m_reconnectTimer;                                                           // - 재연결 대기 타이머 (타이머가 1개라 예약도 항상 최대 1건)
    QElapsedTimer m_lastRxTimer;                                                       // - 마지막 정상 수신 프레임 이후 경과 시간 측정

    bool m_intentionalDisconnect = false;                                              // - stop() 호출로 인한 정지인지 (재연결 억제용)
};
