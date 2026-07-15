#include "ServerConnectionService.h"

ServerConnectionService::ServerConnectionService(QObject *parent)
    : QObject(parent)
{
}

void ServerConnectionService::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged();
}
