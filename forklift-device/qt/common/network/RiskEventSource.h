#pragma once

#include <QByteArray>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include "../models/RiskMetadata.h"
#include "IMetadataSource.h"

// - 위험 판정 데이터 수신 클래스 (개행 구분 JSON 수신, 오래된 데이터 필터링 및 자동 재연결 지원)
class RiskEventSource : public IMetadataSource
{
    Q_OBJECT

public:
    RiskEventSource(QString host, quint16 port, QObject *parent = nullptr);  // - 생성자: 서버 주소, 포트, 부모 객체 지정 및 초기화

    void start() override;                                                   // - 수신 시작: 서버 접속 및 데이터 수신 개시
    void stop() override;                                                    // - 수신 정지: 수동 종료 처리 및 자동 재연결 차단

private slots:
    void handleConnected();                                                  // - 접속 완료 처리: 서버 연결 성공 이벤트 핸들러
    void handleDisconnected();                                               // - 연결 끊김 처리: 서버 연결 해제 이벤트 핸들러
    void handleReadyRead();                                                  // - 데이터 수신 처리: 서버 데이터 수신 이벤트 핸들러
    void handleError(QAbstractSocket::SocketError error);                    // - 통신 오류 처리: 소켓 에러 발생 이벤트 핸들러
    void handleWatchdogTimeout();                                            // - 무수신 감지 처리: 워치독 만료 시 통신 끊김 상태 통지

private:
    void processLine(const QByteArray &line);                                // - 수신 데이터 해석: 한 줄(JSON) 검증, 오래된 데이터 필터링 및 신호 전달
    void scheduleReconnect();                                                // - 재연결 예약: 일정 시간 후 접속 재시도

    QString m_host;                                                          // - 접속 주소: 재연결용 서버 주소 보관
    quint16 m_port;                                                          // - 접속 포트: 재연결용 서버 포트 보관
    QTcpSocket m_socket;                                                     // - 통신 소켓: 서버 통신용 TCP 소켓 객체
    QByteArray m_buffer;                                                     // - 수신 버퍼: 미완성 데이터 누적용 임시 저장소
    bool m_stopRequested = false;                                            // - 수동 종료 상태: 사용자 요청에 의한 종료 여부 (자동 재연결 차단용)
    bool m_reconnectPending = false;                                         // - 재연결 진행 상태: 중복 타이머 생성 방지용 플래그
    int m_reconnectDelayMs = 0;                                              // - 다음 재연결 대기 시간: 실패마다 2배로 증가(exponential backoff)
    QTimer m_watchdogTimer;                                                  // - 무수신 감지 타이머: 데이터 수신마다 재시작, 만료 시 통신 끊김 판단
    QString m_lastCameraId;                                                  // - 마지막 수신 카메라 ID: 워치독 만료 시 어느 카드에 표시할지 판단용
};
