#include "ServerConnectionService.h"

ServerConnectionService::ServerConnectionService(QObject *parent)
    : QObject(parent)                                                         // - 생성자: 부모 객체 지정 및 서비스 초기화
{
}

void ServerConnectionService::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)                                            // - 중복 변경 방지: 현재 상태와 동일한 경우 처리 생략
        return;
    m_connectionState = state;                                                 // - 상태값 갱신: 내부 연결 상태 변수 업데이트
    emit connectionStateChanged();                                             // - 신호 발생: 서버 연결 상태 변경 알림
}
