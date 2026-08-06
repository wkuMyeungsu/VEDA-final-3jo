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
    QString serverHost = QStringLiteral("127.0.0.1");                                   // - 서버 주소: 단말 통신 대상 IP
    quint16 riskPort = 9000;                                                            // - 위험 채널 포트: 위험 판정 데이터 수신 포트
    quint16 handoverPort = 9001;                                                        // - 제어 채널 포트: 카메라 전환 핸드오버 제어 포트
    QString metadataSourceType = QStringLiteral("mock");                               // - 데이터 소스 유형: 가상(mock) 또는 통신(tcp)
    QString terminalId;                                                                 // - 단말 식별자: 본인 명령 필터링용 단말 ID
};
