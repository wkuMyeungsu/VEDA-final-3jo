#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QTcpSocket>

#include "../models/Types.h"

// 중앙 서버와의 "제어" 채널 (RiskMetadata 데이터 채널인 IMetadataSource와는 별개).
// 서버가 보내는 카메라 배정(handover) 명령을 받아서, 그 값을 실제로 쓰는 쪽
// (ActiveCameraController::setActiveCameraId())에 신호로 전달함 -- main.cpp에서
// cameraHandoverRequested를 그쪽에 연결해서 배선함 (docs/INTEGRATION.md §3 참고).
//
// 주의: processLine()이 처리하는 메시지 스키마는 아직 "확정된 프로토콜"이 아니라
// 배선을 막지 않기 위한 임시(placeholder) 규격임. handover 로직 담당자와 아직
// 논의 중인 것들: 서버가 먼저 push하는 방식인지 단말이 요청하는 방식인지,
// 메시지 구분(framing) 방식, 실패 시 재시도 방식, 서버가 ack를 원하는지 여부.
class HandoverClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)

public:
    explicit HandoverClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    void connectionStateChanged(RiskTypes::ConnectionState state);
    // 서버가 "이 카메라로 전환해라"라고 지시했을 때 emit -- cameraId를 그대로 실어 보냄
    void cameraHandoverRequested(const QString &cameraId);

private slots:
    void handleConnected();
    void handleDisconnected();
    // 소켓에 새 데이터가 도착할 때마다 호출 -- 버퍼에 쌓고 줄바꿈 단위로 처리
    void handleReadyRead();
    void handleError(QAbstractSocket::SocketError socketError);

private:
    void setConnectionState(RiskTypes::ConnectionState state);
    // 완성된 한 줄(JSON 1건)을 실제로 해석하는 곳
    void processLine(const QByteArray &line);

    QTcpSocket m_socket;
    QByteArray m_buffer;   // 아직 줄바꿈이 안 온 "미완성" 데이터 조각을 임시 보관
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
};
