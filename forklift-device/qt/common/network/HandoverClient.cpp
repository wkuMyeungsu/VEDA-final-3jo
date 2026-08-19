#include "HandoverClient.h"

#if __has_include(<mosquitto/libmosquitto.h>)
#include <mosquitto/libmosquitto.h>
#else
#include <mosquitto.h>
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcHandoverClient, "safety.handover.client")
}

HandoverClient::HandoverClient(QObject *parent)
    : QObject(parent)
{
    mosquitto_lib_init();
}

HandoverClient::~HandoverClient()
{
    disconnectFromServer();
    mosquitto_lib_cleanup();
}

void HandoverClient::connectToServer(const QString &host, quint16 port)
{
    disconnectFromServer();

    m_host = host;
    m_port = port;
    m_topic = QStringLiteral("forklift/assignment/%1").arg(m_terminalId.isEmpty() ? QStringLiteral("+") : m_terminalId);

    const QString clientId = QStringLiteral("forklift_handover_%1_%2")
                                 .arg(m_terminalId.isEmpty() ? QStringLiteral("anon") : m_terminalId)
                                 .arg(reinterpret_cast<quintptr>(this));

    m_mosq = mosquitto_new(clientId.toUtf8().constData(), true, this);
    if (!m_mosq) {
        qCWarning(lcHandoverClient) << "failed to create mosquitto instance for handover";
        setConnectionState(RiskTypes::ConnectionState::Disconnected);
        return;
    }

    mosquitto_connect_callback_set(m_mosq, &HandoverClient::onConnect);
    mosquitto_disconnect_callback_set(m_mosq, &HandoverClient::onDisconnect);
    mosquitto_message_callback_set(m_mosq, &HandoverClient::onMessage);

    // libmosquitto 자체 reconnect backoff 설정 (3s ~ 30s)
    mosquitto_reconnect_delay_set(m_mosq, 3, 30, true);

    setConnectionState(RiskTypes::ConnectionState::Connecting);

    const int rc = mosquitto_connect_async(m_mosq, m_host.toUtf8().constData(), m_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        qCWarning(lcHandoverClient) << "mosquitto_connect_async failed, rc=" << rc;
        setConnectionState(RiskTypes::ConnectionState::Disconnected);
        return;
    }

    mosquitto_loop_start(m_mosq);
}

void HandoverClient::disconnectFromServer()
{
    if (m_mosq) {
        mosquitto_loop_stop(m_mosq, true);
        mosquitto_disconnect(m_mosq);
        mosquitto_destroy(m_mosq);
        m_mosq = nullptr;
    }
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void HandoverClient::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged(m_connectionState);
}

void HandoverClient::processPayload(const QByteArray &payload)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcHandoverClient) << "malformed assignment JSON:" << error.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type != QStringLiteral("camera_assignment")) {
        qCDebug(lcHandoverClient) << "ignoring non-assignment message type:" << type;
        return;
    }

    QString streamId = obj.value(QStringLiteral("stream_id")).toString();
    if (streamId.isEmpty()) {
        streamId = obj.value(QStringLiteral("camera_id")).toString();
    }

    if (streamId.isEmpty()) {
        qCWarning(lcHandoverClient) << "assignment message missing stream_id and camera_id";
        return;
    }

    qCInfo(lcHandoverClient) << "camera handover requested to stream:" << streamId;
    emit cameraHandoverRequested(streamId);
}

void HandoverClient::onConnect(struct mosquitto *mosq, void *obj, int rc)
{
    auto *self = static_cast<HandoverClient *>(obj);
    if (rc == 0) {
        // QoS 1로 구독 (이벤트 유실 방지)
        mosquitto_subscribe(mosq, nullptr, self->m_topic.toUtf8().constData(), 1);
        QMetaObject::invokeMethod(self, [self] {
            self->setConnectionState(RiskTypes::ConnectionState::Connected);
            qCInfo(lcHandoverClient) << "connected to handover broker, subscribed to" << self->m_topic;
        }, Qt::QueuedConnection);
    } else {
        QMetaObject::invokeMethod(self, [self] {
            self->setConnectionState(RiskTypes::ConnectionState::Disconnected);
        }, Qt::QueuedConnection);
    }
}

void HandoverClient::onDisconnect(struct mosquitto *mosq, void *obj, int rc)
{
    Q_UNUSED(mosq);
    Q_UNUSED(rc);
    auto *self = static_cast<HandoverClient *>(obj);
    QMetaObject::invokeMethod(self, [self] {
        self->setConnectionState(RiskTypes::ConnectionState::Disconnected);
    }, Qt::QueuedConnection);
}

void HandoverClient::onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
    Q_UNUSED(mosq);
    if (!message || message->payloadlen <= 0)
        return;

    auto *self = static_cast<HandoverClient *>(obj);
    const QByteArray payload(static_cast<const char *>(message->payload), message->payloadlen);

    QMetaObject::invokeMethod(self, [self, payload] {
        self->processPayload(payload);
    }, Qt::QueuedConnection);
}
