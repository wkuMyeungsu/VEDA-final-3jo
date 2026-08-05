#pragma once

#include <QByteArray>  // 버퍼(m_buffer) 타입
#include <QObject>     // Q_OBJECT 등 Qt 객체 기본 기능
#include <QTcpSocket>  // 소켓 통신

#include "../models/RiskMetadata.h"  // 위험 판정 데이터 타입
#include "IMetadataSource.h"          // 이 클래스가 구현하는 인터페이스

// 서버(danger_judgment_engine / ResultPublisher, server/judgment/README.md)가 보내는
// 위험 판정(NDJSON, 개행 구분)을 수신하는 IMetadataSource 구현체.
// - HandoverClient(제어 채널, camera_assignment)와는 별개 소켓/별개 포트 -- 서버 쪽도
//   ResultPublisher가 별도 프로세스/포트(9000)로 listen함
// - 프레이밍/버퍼링은 HandoverClient와 동일한 패턴 재사용 (README 참고)
// - HandoverClient와 달리 이 채널은 안전 신호를 실어나르므로 자동 재연결이 필수.
//   HandoverClient.cpp의 현재 develop 상태에는 재연결이 없어서(직접 확인함,
//   feature/forklift-device/handover-client-reconnect 브랜치에만 있고 미머지),
//   대신 이미 develop에 있는 RtspVideoSource::scheduleReconnect() 패턴을 이식함
// - 접속 직후 서버 큐(최대 100건)가 한꺼번에 도착하는 구간: 각 줄의 utc_time이
//   오래됐으면 emit하지 않고 로그만 남김 (버저가 과거 경보로 우는 것 방지)
class RiskEventSource : public IMetadataSource
{
    Q_OBJECT

public:
    RiskEventSource(QString host, quint16 port, QObject *parent = nullptr);  // host/port 받는 생성자

    void start() override;  // 접속 시작
    void stop() override;   // 접속 종료

private slots:
    void handleConnected();                              // 접속 성공 시
    void handleDisconnected();                            // 연결 끊길 시
    void handleReadyRead();                                // 데이터 도착 시
    void handleError(QAbstractSocket::SocketError error);  // 에러 발생 시

private:
    // 완성된 한 줄(JSON 1건)을 RiskMetadata로 파싱 + 오래된 메시지 걸러낸 뒤 emit
    void processLine(const QByteArray &line);
    // 에러/끊김 시 일정 시간 뒤 재접속 시도 예약 (RtspVideoSource::scheduleReconnect() 패턴)
    void scheduleReconnect();

    QString m_host;      // 서버 주소
    quint16 m_port;       // 서버 포트
    QTcpSocket m_socket;  // 실제 연결 소켓
    QByteArray m_buffer;  // 받다 만 데이터 임시 보관
    bool m_stopRequested = false;    // stop()으로 의도적으로 끊었으면 재연결 안 함
    bool m_reconnectPending = false; // errorOccurred+disconnected 중복 발생 시 재연결 타이머 중복 예약 방지
};
