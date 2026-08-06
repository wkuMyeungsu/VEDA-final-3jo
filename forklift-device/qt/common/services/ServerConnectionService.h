#pragma once

#include <QObject>

#include "../models/Types.h"

// - 서버 연결 상태 서비스 클래스 (상단 상태바 등에 표출할 서버 접속 상태 보관 및 QML 전달)
class ServerConnectionService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState WRITE setConnectionState NOTIFY
                   connectionStateChanged)                                    // - 연결 상태 속성: 서버 접속 상태 전달 및 변경 알림

public:
    explicit ServerConnectionService(QObject *parent = nullptr);              // - 생성자: 부모 객체 지정 및 서비스 초기화

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; } // - 상태 조회: 현재 서버 연결 상태 반환
    void setConnectionState(RiskTypes::ConnectionState state);                 // - 상태 설정: 서버 연결 상태 갱신 및 변경 신호 발생

signals:
    void connectionStateChanged();                                            // - 상태 변경 알림: 서버 연결 상태 변경 시 발생

private:
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Connected; // - 서버 연결 상태: 현재 접속 상태 보관 (기본값: Connected)
};
