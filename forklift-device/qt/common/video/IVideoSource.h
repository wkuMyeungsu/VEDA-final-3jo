#pragma once

#include <QImage>
#include <QObject>

#include "../models/Types.h"

// - 영상 수신 인터페이스 (카메라 프레임 및 상태 수신을 위한 추상 클래스)
class IVideoSource : public QObject
{
    Q_OBJECT

public:
    explicit IVideoSource(QObject *parent = nullptr) : QObject(parent) {}       // - 생성자: 부모 객체 지정 및 초기화
    ~IVideoSource() override = default;                                       // - 소멸자: 기본 소멸자 지정

    virtual void start() = 0;                                                 // - 재생 시작: 영상 수신 및 프레임 생성 개시
    virtual void stop() = 0;                                                  // - 재생 정지: 영상 수신 중단 및 리소스 정리

    virtual void setSimulatedConnected(bool connected) { Q_UNUSED(connected); } // - 시뮬레이션 설정: 테스트용 가상 연결 상태 설정

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; } // - 연결 상태 조회: 현재 영상 소스 접속 상태 반환

signals:
    void frameReady(const QImage &frame);                                      // - 프레임 준비 알림: 새 영상 프레임 생성 시 전달
    void connectionStateChanged(RiskTypes::ConnectionState state);             // - 상태 변경 알림: 영상 접속 상태 변경 시 발생

    void onvifMetadataReceived(const QByteArray &xml);                         // - 메타데이터 수신 알림: ONVIF 메타데이터(XML) 수신 시 전달

protected:
    void setConnectionState(RiskTypes::ConnectionState state)                 // - 연결 상태 갱신: 상태 값 변경 처리 함수
    {
        if (m_connectionState == state)                                        // - 중복 변경 방지: 현재 상태와 동일한 경우 처리 생략
            return;
        m_connectionState = state;                                             // - 상태값 갱신: 내부 상태 변수 업데이트
        emit connectionStateChanged(state);                                    // - 신호 발생: 상태 변경 이벤트 외부 전달
    }

private:
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected; // - 영상 연결 상태: 접속 상태 보관 (기본값: Disconnected)
};
