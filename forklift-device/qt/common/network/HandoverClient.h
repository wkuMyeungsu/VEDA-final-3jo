#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include "../models/Types.h"

struct mosquitto;
struct mosquitto_message;

// - 서버 제어 채널 통신 클래스 (MQTT forklift/assignment/<terminal_id> 구독 및 카메라 전환 신호 전달)
class HandoverClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)

public:
    explicit HandoverClient(QObject *parent = nullptr);
    ~HandoverClient() override;

    void setTerminalId(const QString &terminalId) { m_terminalId = terminalId; }
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    void connectionStateChanged(RiskTypes::ConnectionState state);
    void cameraHandoverRequested(const QString &streamId);

private:
    void setConnectionState(RiskTypes::ConnectionState state);
    void processPayload(const QByteArray &payload);

    static void onConnect(struct mosquitto *mosq, void *obj, int rc);
    static void onDisconnect(struct mosquitto *mosq, void *obj, int rc);
    static void onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

    struct mosquitto *m_mosq = nullptr;
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;

    QString m_host;
    quint16 m_port = 1883;
    QString m_terminalId;
    QString m_topic;
};
