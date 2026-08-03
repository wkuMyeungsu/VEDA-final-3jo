#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QTcpSocket>

#include "../models/Types.h"

// Control channel to the central server: receives camera-assignment
// (handover) commands and forwards them to whoever owns
// ActiveCameraController::setActiveCameraId() -- see main.cpp once wired up
// and docs/INTEGRATION.md §3.
//
// The message schema handled in processLine() is a placeholder to unblock
// this wiring -- it is NOT the agreed protocol yet. Still open with the
// handover-logic owner: push vs. request direction, message framing,
// retry-on-failure behavior, and whether the server needs an ack.

class NetworkClient : public QObject 
{
    Q_OBJECT
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)

public:
    explicit NetworkClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    void connectionStateChanged(RiskTypes::ConnectionState state);
    void cameraHandoverRequested(const QString &cameraId);

private slots:
    void handleConnected();
    void handleDisconnected();
    void handleReadyRead();
    void handleError(QAbstractSocket::SocketError socketError);

private:
    void setConnectionState(RiskTypes::ConnectionState state);
    void processLine(const QByteArray &line);

    QTcpSocket m_socket;
    QByteArray m_buffer;
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
};
