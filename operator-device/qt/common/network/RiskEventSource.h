#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include "../models/RiskMetadata.h"
#include "IMetadataSource.h"

struct mosquitto;
struct mosquitto_message;

// - 위험 판정 데이터 수신 클래스 (MQTT 구독)
// forklift-device/qt/common/network/RiskEventSource에서 이식 (클라이언트 ID 접두어만 다름)
class RiskEventSource : public IMetadataSource
{
    Q_OBJECT

public:
    RiskEventSource(QString brokerHost, quint16 brokerPort, QString terminalId, QObject *parent = nullptr); // - 생성자
    ~RiskEventSource() override; // - 소멸자

    void start() override; // - 수신 시작
    void stop() override;  // - 수신 정지

private:
    void processPayload(const QByteArray &payload); // - 수신 데이터 파싱
    void handleWatchdogTimeout();                    // - 무수신 폴링 처리

    // - mosquitto 콜백: libmosquitto 내부 네트워크 스레드에서 호출
    static void onConnect(struct mosquitto *mosq, void *obj, int rc);
    static void onDisconnect(struct mosquitto *mosq, void *obj, int rc);
    static void onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

    QString m_brokerHost;             // - 브로커 주소
    quint16 m_brokerPort;             // - 브로커 포트
    QString m_terminalId;             // - 단말 ID
    QString m_topic;                  // - 구독 토픽
    struct mosquitto *m_mosq = nullptr; // - mosquitto 클라이언트 핸들
    QTimer m_watchdogTimer;           // - 워치독 폴링 타이머
    QElapsedTimer m_lastMessageTimer; // - 마지막 수신 후 경과 시간
    QString m_lastCameraId;           // - 마지막 수신 카메라 ID
};
