#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QTcpSocket>

#include "../models/Types.h"

// 중앙 서버와의 "제어 채널" 클래스.
//
// [역할] 서버가 보내는 카메라 배정(핸드오버) 명령을 TCP로 받아서,
//        cameraHandoverRequested 신호로 흘려보낸다. main.cpp에서 이 신호를
//        ActiveCameraController::setActiveCameraId()에 연결한다
//        (docs/INTEGRATION.md §3 참고).
//
// [이름의 근거] 위험/거리 데이터를 받는 TcpMetadataSource도 소켓 클라이언트라,
//        두루뭉술한 "NetworkClient"로는 둘을 구분하기 어렵다. 그래서 이 클래스가
//        실제로 나르는 것(서버의 핸드오버 명령)을 이름에 담았다.
//
// [주고받는 것]
//   - 수신: 서버 -> 이 클래스. 현재는 한 줄에 JSON 하나(개행 구분) 형식으로
//           {"type":"camera_assignment","camera_id":"..."} 를 받는다고 가정한다
//           (processLine 참고).
//   - 송출(신호): cameraHandoverRequested(cameraId) / connectionStateChanged(state)
//
// [미해결] processLine()의 메시지 스키마는 배선을 풀려고 넣은 임시값이며 아직
//        팀 합의된 최종 프로토콜이 아니다. 서버가 push할지 요청-응답일지, 메시지
//        구분(프레이밍) 방식, 실패 시 재전송, ack 필요 여부는 핸드오버 로직
//        담당자와 확정해야 한다.

class HandoverClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)

public:
    explicit HandoverClient(QObject *parent = nullptr);

    // 단말 terminal_id 설정
    // 서버 연결 전 호출
    void setTerminalId(const QString &terminalId) { m_terminalId = terminalId; }

    // 서버에 접속한다. host/port는 자동 재연결 때 재사용하려고 저장해 둔다.
    //   host: 서버 호스트(예: "127.0.0.1")   port: 서버 포트(예: 9000)
    void connectToServer(const QString &host, quint16 port);
    // 서버 연결을 "의도적으로" 끊는다. 이 경우 자동 재연결하지 않는다.
    void disconnectFromServer();

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    // 연결 상태가 바뀔 때 발생. state: 바뀐 현재 상태(Disconnected/Connecting/Connected).
    void connectionStateChanged(RiskTypes::ConnectionState state);
    // 서버가 새 카메라를 배정했을 때 발생. cameraId: 앞으로 화면에 띄울 카메라 ID.
    void cameraHandoverRequested(const QString &cameraId);

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleReadyRead();
    void handleError(QAbstractSocket::SocketError socketError);

private:
    void setConnectionState(RiskTypes::ConnectionState state);
    void processLine(const QByteArray &line);
    void scheduleReconnect();
    // 접속 직후 자기 terminal_id를 서버에 알린다 (handleConnected()에서 호출).
    void sendHello();

    QTcpSocket m_socket;
    QByteArray m_buffer;
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;

    // connectToServer()가 저장해 둔 접속 대상. scheduleReconnect()가 같은 곳으로
    // 다시 시도할 때 쓴다. "첫" 접속 실패(서버가 아직 안 뜬 경우)도 재시도하므로,
    // RtspVideoSource처럼 서버보다 단말을 먼저 켜도 재실행 없이 알아서 붙는다.
    QString m_host;
    quint16 m_port = 0;
    // 필터링용 단말 ID
    QString m_terminalId;

    // disconnectFromServer()부터 다음 connectToServer()까지만 true. 의도적으로 끊은
    // 동안에는 자동 재연결을 막아, 일부러 끊은 연결이 되살아나지 않게 한다.
    bool m_intentionalDisconnect = false;
    // 한 번의 끊김에 errorOccurred와 disconnected가 둘 다 발생할 때, 재연결 타이머가
    // 두 개 겹쳐 걸리지 않도록 막는 플래그.
    bool m_reconnectPending = false;
};
