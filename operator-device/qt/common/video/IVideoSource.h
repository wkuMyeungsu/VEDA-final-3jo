#pragma once

#include <QImage>
#include <QObject>

#include "../models/Types.h"

// 카메라 1대의 영상 프레임을 얻어오는 방법을 감춘 추상 인터페이스
// - QML/UI는 이 인터페이스만 알고, 실제 구현(Mock/LocalFile/Rtsp)은 모름
// - 어떤 구현체를 쓸지는 VideoSourceManager가 cameras.json 보고 결정
class IVideoSource : public QObject
{
    Q_OBJECT

public:
    explicit IVideoSource(QObject *parent = nullptr) : QObject(parent) {}
    ~IVideoSource() override = default;

    // 연결 시작 (Mock: 타이머 시작, Rtsp: 파이프라인 구성+재생)
    virtual void start() = 0;
    // 연결 종료 (재생 중지, 리소스 정리)
    virtual void stop() = 0;

    // 데모 전용: 카메라가 물리적으로 뽑힌 상황을 흉내냄
    // 실제 카메라(Rtsp)는 이 호출을 무시함
    virtual void setSimulatedConnected(bool connected) { Q_UNUSED(connected); }

    // 마지막으로 emit된 connectionStateChanged 값 (polling용)
    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    // 새 프레임 1장이 준비될 때마다 emit
    void frameReady(const QImage &frame);
    // 연결 상태가 바뀔 때마다 emit (setConnectionState가 호출)
    void connectionStateChanged(RiskTypes::ConnectionState state);

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
