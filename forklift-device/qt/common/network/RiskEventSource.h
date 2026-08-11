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

// - 위험 판정 데이터 수신 클래스 (MQTT 구독, retain/오래된 데이터 필터링 및 무수신 워치독 지원)
class RiskEventSource : public IMetadataSource
{
    Q_OBJECT

public:
    RiskEventSource(QString brokerHost, quint16 brokerPort, QString terminalId, QObject *parent = nullptr); // - 생성자: 브로커 주소, 포트, 단말 ID, 부모 객체 지정 및 초기화
    ~RiskEventSource() override;                                              // - 소멸자: mosquitto 핸들 정리

    void start() override;                                                   // - 수신 시작: 브로커 접속 및 구독 개시
    void stop() override;                                                    // - 수신 정지: 연결 해제 및 워치독 정지

private:
    void processPayload(const QByteArray &payload);                          // - 수신 데이터 해석(메인 스레드): JSON 파싱, 오래된/retain 데이터 필터링 및 신호 전달
    void handleWatchdogTimeout();                                            // - 무수신 폴링 처리: 100ms마다 마지막 수신 이후 경과 시간 확인

    // - mosquitto 콜백: libmosquitto 내부 네트워크 스레드에서 호출되므로 여기서 직접
    //   Qt 상태를 건드리지 않고 QMetaObject::invokeMethod로 메인 스레드에 위임한다.
    static void onConnect(struct mosquitto *mosq, void *obj, int rc);
    static void onDisconnect(struct mosquitto *mosq, void *obj, int rc);
    static void onMessage(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message);

    QString m_brokerHost;                                                    // - 브로커 주소: MQTT 브로커 호스트
    quint16 m_brokerPort;                                                    // - 브로커 포트: MQTT 브로커 포트
    QString m_terminalId;                                                    // - 단말 ID: 구독 토픽 및 클라이언트 ID 구성용
    QString m_topic;                                                         // - 구독 토픽: forklift/risk/{terminal_id}
    struct mosquitto *m_mosq = nullptr;                                      // - mosquitto 클라이언트 핸들
    QTimer m_watchdogTimer;                                                  // - 무수신 폴링 타이머: 100ms 간격으로 워치독 상태 점검
    QElapsedTimer m_lastMessageTimer;                                        // - 마지막 수신(또는 마지막 워치독 통지) 이후 경과 시간 측정용
    QString m_lastCameraId;                                                  // - 마지막 수신 카메라 ID: 워치독 만료 시 어느 카드에 표시할지 판단용
};
