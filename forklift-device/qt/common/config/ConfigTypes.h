#pragma once

#include <QString>

// - 관제 센터 설정 구조체 (control_center.json 데이터 보관)
struct ControlCenterConfig {
    QString systemName = QStringLiteral("Forklift Blind-Spot Safety - Control Center"); // - 시스템 이름: 관제 화면 표시용 시스템 명칭
    QString serverHost = QStringLiteral("127.0.0.1");                                   // - 서버 주소: 관제 센터 접속 대상 IP
    quint16 serverPort = 9000;                                                         // - 서버 포트: 관제 센터 접속 대상 포트
    QString metadataSourceType = QStringLiteral("mock");                               // - 데이터 소스 유형: 가상(mock) 또는 통신(tcp)
    int eventLogMaxEntries = 200;                                                      // - 최대 로그 수: 목록에 보관할 최대 이벤트 수
};

// - 단말 설정 구조체 (terminal.json 데이터 보관)
struct TerminalConfig {
    QString defaultCameraId;                                                            // - 기본 카메라 ID: 시작 시 초기 표출 카메라
    QString serverHost = QStringLiteral("127.0.0.1");                                   // - 서버 주소: HandoverClient 제어 채널(TCP) 접속 대상 IP
    quint16 handoverPort = 9001;                                                        // - 제어 채널 포트: 카메라 전환 핸드오버 제어 포트
    QString mqttBrokerHost = QStringLiteral("127.0.0.1");                               // - MQTT 브로커 주소: RiskEventSource 위험 판정 데이터 구독 대상
    quint16 mqttBrokerPort = 1883;                                                      // - MQTT 브로커 포트: RiskEventSource 위험 판정 데이터 구독 포트
    QString metadataSourceType = QStringLiteral("mock");                               // - 데이터 소스 유형: 가상(mock) 또는 통신(mqtt)
    QString terminalId;                                                                 // - 단말 식별자: 본인 명령 필터링 및 MQTT 구독 토픽 구성용 단말 ID
    QString fpgaSerialPort = QStringLiteral("/dev/serial0");                            // - FPGA 경고 보드 UART 포트: Pi GPIO 8/10번 핀(TXD/RXD) 직결 기준 기본값
    qint32 fpgaBaudRate = 115200;                                                        // - FPGA 경고 보드 UART 보레이트: gpio-control/src/top.v 기본값과 일치
    QString warningDeviceType = QStringLiteral("noop");                                 // - 경고 장치 유형: 비활성(noop) 또는 실장치(serial)
};
