#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QTcpSocket>

#include "../models/Types.h"

// - 서버 제어 채널 통신 클래스 (개행 문자 구분 JSON 메시지 수신 및 카메라 전환 신호 전달)
class HandoverClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged) // - 연결 상태 감시용 속성 정의

public:
    explicit HandoverClient(QObject *parent = nullptr);                         // - 생성자: 부모 객체 지정 및 초기화

    void setTerminalId(const QString &terminalId) { m_terminalId = terminalId; } // - 단말 ID 설정: 서버 접속 전 단말 식별자 지정
    void connectToServer(const QString &host, quint16 port);                    // - 서버 접속 요청: 접속 정보 보관 및 연결 시도
    void disconnectFromServer();                                                // - 서버 연결 종료: 수동 종료 처리 및 자동 재연결 차단

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; } // - 현재 상태 조회: 연결 상태 값 반환

signals:
    void connectionStateChanged(RiskTypes::ConnectionState state);             // - 상태 변경 알림: 서버 연결 상태 변경 시 발생
    void cameraHandoverRequested(const QString &cameraId);                     // - 카메라 전환 요청: 새 카메라 배정 시 발생 (카메라 ID 전달)

private slots:
    void handleConnected();                                                     // - 접속 완료 처리: 서버 연결 성공 이벤트 핸들러
    void handleDisconnected();                                                  // - 연결 끊김 처리: 서버 연결 해제 이벤트 핸들러
    void handleReadyRead();                                                     // - 데이터 수신 처리: 서버 데이터 수신 이벤트 핸들러
    void handleError(QAbstractSocket::SocketError socketError);                 // - 통신 오류 처리: 소켓 에러 발생 이벤트 핸들러

private:
    void setConnectionState(RiskTypes::ConnectionState state);                  // - 연결 상태 갱신: 내부 상태 변경 및 알림 신호 발생
    void processLine(const QByteArray &line);                                   // - 수신 데이터 해석: 수신된 한 줄(JSON) 검증 및 처리
    void scheduleReconnect();                                                   // - 재연결 예약: 일정 시간(3초) 후 접속 재시도
    void sendHello();                                                           // - 식별 정보 전송: 접속 직후 서버에 단말 ID 알림

    QTcpSocket m_socket;                                                        // - 통신 소켓: 서버 통신용 TCP 소켓 객체
    QByteArray m_buffer;                                                        // - 수신 버퍼: 미완성 데이터 누적용 임시 저장소
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected; // - 현재 연결 상태 보관 (기본값: Disconnected)

    QString m_host;                                                             // - 접속 주소: 재연결용 서버 주소 보관
    quint16 m_port = 0;                                                         // - 접속 포트: 재연결용 서버 포트 보관
    QString m_terminalId;                                                       // - 단말 식별자: 메시지 수신 대상 필터링용 단말 ID

    bool m_intentionalDisconnect = false;                                      // - 수동 종료 상태: 사용자 요청에 의한 종료 여부
    bool m_reconnectPending = false;                                            // - 재연결 진행 상태: 중복 타이머 생성 방지용 플래그
    int m_reconnectDelayMs = 0;                                                 // - 다음 재연결 대기 시간: 실패마다 2배로 증가(exponential backoff)
};
