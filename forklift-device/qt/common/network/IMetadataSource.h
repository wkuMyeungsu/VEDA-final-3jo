#pragma once

#include <QObject>

#include "../models/RiskMetadata.h"
#include "../models/Types.h"

// - 위험 감지 데이터 수신 인터페이스 (실제 구현부와 분리된 추상 클래스)
class IMetadataSource : public QObject
{
    Q_OBJECT

public:
    explicit IMetadataSource(QObject *parent = nullptr) : QObject(parent) {}   // - 생성자: 부모 객체 지정 및 초기화
    ~IMetadataSource() override = default;                                    // - 소멸자: 기본 소멸자 지정

    virtual void start() = 0;                                                 // - 수신 시작: 서버 연결 및 데이터 수신 개시
    virtual void stop() = 0;                                                  // - 수신 정지: 서버 연결 해제 및 데이터 수신 중단

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; } // - 현재 상태 조회: 연결 상태 값 반환

signals:
    void metadataReceived(const RiskMetadata &metadata);                       // - 데이터 수신 알림: 새 위험 감지 이벤트 발생 시 전달
    void connectionStateChanged(RiskTypes::ConnectionState state);             // - 상태 변경 알림: 서버 연결 상태 변경 시 발생
    void linkLost();                                                          // - 통신 두절 알림: 워치독 무수신 감지 시 전달

protected:
    void setConnectionState(RiskTypes::ConnectionState state)                 // - 연결 상태 갱신: 상태 값 변경 처리 함수
    {
        if (m_connectionState == state)                                        // - 중복 변경 방지: 현재 상태와 동일한 경우 생략
            return;
        m_connectionState = state;                                             // - 상태값 갱신: 내부 상태 변수 업데이트
        emit connectionStateChanged(state);                                    // - 신호 발생: 상태 변경 이벤트 외부 전달
    }

private:
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected; // - 현재 연결 상태 보관 (기본값: Disconnected)
};
