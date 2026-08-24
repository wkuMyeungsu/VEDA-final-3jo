#pragma once

#include <QObject>

#include "../models/RiskMetadata.h"
#include "../models/Types.h"

// 중앙 서버로부터 위험 감지 이벤트를 받는 방법을 감춘 추상 인터페이스
// - IVideoSource(video/)의 네트워크 버전 짝꿍 클래스
// - MetadataDistributor는 이 인터페이스만 알고, 실제 구현(Mock/Tcp)은 모름
// - 지금은 MockMetadataSource 하나뿐, 나중에 RiskEventSource로 교체 가능
class IMetadataSource : public QObject
{
    Q_OBJECT

public:
    explicit IMetadataSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IMetadataSource() override = default;

    // 서버 연결/수신 시작
    virtual void start() = 0;
    // 서버 연결 종료
    virtual void stop() = 0;

    // 마지막으로 emit된 connectionStateChanged 값 (polling용)
    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    // 카메라 1대의 새 이벤트 1건이 생길 때마다 emit -- MetadataDistributor::handleMetadata()가 받음
    void metadataReceived(const RiskMetadata &metadata);
    // 연결 상태가 바뀔 때마다 emit (setConnectionState가 호출)
    void connectionStateChanged(RiskTypes::ConnectionState state);
    // 워치독 등에 의해 통신 두절(무수신) 감지 시 emit -- MetadataDistributor가 전 채널로 팬아웃
    void linkLost();

protected:
    // 값이 바뀐 경우에만 신호를 냄 (중복 emit 방지)
    void setConnectionState(RiskTypes::ConnectionState state)
    {
        if (m_connectionState == state)
            return;
        m_connectionState = state;
        emit connectionStateChanged(state);
    }

private:
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
};
