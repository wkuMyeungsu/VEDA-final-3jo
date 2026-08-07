#include "HandoverClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>

namespace {
Q_LOGGING_CATEGORY(lcHandoverClient, "safety.handover.client")                  // - 로깅 카테고리 정의: 통신 로그 분류용 이름 지정

constexpr int kReconnectDelayMs = 3000;                                         // - 재연결 대기 시간 설정 (3초): 서버 부하 방지 및 빠른 복구 목적
}

HandoverClient::HandoverClient(QObject *parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, &HandoverClient::handleConnected);       // - 연결 성공 이벤트 연결
    connect(&m_socket, &QTcpSocket::disconnected, this, &HandoverClient::handleDisconnected); // - 연결 끊김 이벤트 연결
    connect(&m_socket, &QTcpSocket::readyRead, this, &HandoverClient::handleReadyRead);       // - 데이터 수신 이벤트 연결
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &HandoverClient::handleError);       // - 통신 오류 이벤트 연결
}

void HandoverClient::connectToServer(const QString &host, quint16 port)
{
    m_host = host;                                                              // - 접속 주소 저장: 재연결용 호스트 정보 보관
    m_port = port;                                                              // - 접속 포트 저장: 재연결용 포트 정보 보관
    m_intentionalDisconnect = false;                                          // - 수동 종료 플래그 초기화: 자동 재연결 활성화

    setConnectionState(RiskTypes::ConnectionState::Connecting);                // - 상태 변경: 연결 진행 중 상태로 설정
    m_socket.connectToHost(host, port);                                        // - 서버 접속 시도: 지정 주소 및 포트로 연결 요청
}

void HandoverClient::disconnectFromServer()
{
    m_intentionalDisconnect = true;                                            // - 수동 종료 설정: 자동 재연결 동작 차단
    m_socket.disconnectFromHost();                                             // - 연결 해제: 서버와의 소켓 연결 종료
}

void HandoverClient::handleConnected()
{
    setConnectionState(RiskTypes::ConnectionState::Connected);                 // - 상태 갱신: 연결 상태를 '연결됨'으로 변경
    sendHello();                                                               // - 식별 정보 전송: 서버에 단말 ID 알림 함수 호출
}

void HandoverClient::sendHello()
{
    if (m_terminalId.isEmpty())                                                // - ID 확인: 단말 ID 미설정 시 전송 생략
        return;

    QJsonObject obj;                                                            // - 메시지 객체 생성: 전송용 JSON 객체 생성
    obj[QStringLiteral("type")] = QStringLiteral("hello");                      // - 메시지 유형 설정: 'hello' 타입 지정
    obj[QStringLiteral("terminal_id")] = m_terminalId;                           // - 단말 ID 설정: 현재 단말 식별자 지정

    m_socket.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));          // - 데이터 전송: JSON 메시지 변환 및 전송
    m_socket.write("\n");                                                       // - 줄바꿈 전송: 메시지 구분용 개행 문자 전송
}

void HandoverClient::handleDisconnected()
{
    m_buffer.clear();                                                          // - 버퍼 초기화: 수신 데이터 임시 저장소 비우기
    setConnectionState(RiskTypes::ConnectionState::Disconnected);               // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
    scheduleReconnect();                                                       // - 재연결 예약: 일정 시간 후 재접속 시도
}

void HandoverClient::handleError(QAbstractSocket::SocketError error)
{
    qCWarning(lcHandoverClient) << "소켓 오류:" << error << m_socket.errorString(); // - 경고 로그 출력: 오류 내용 및 상세 원인 기록
    setConnectionState(RiskTypes::ConnectionState::Disconnected);               // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
    scheduleReconnect();                                                       // - 재연결 예약: 접속 실패 시 자동 복구 재시도
}

void HandoverClient::scheduleReconnect()
{
    if (m_intentionalDisconnect || m_reconnectPending || m_host.isEmpty())      // - 중복 시도 차단: 수동 종료, 예약 중, 주소 미설정 시 생략
        return;

    m_reconnectPending = true;                                                 // - 예약 상태 설정: 중복 타이머 생성 방지

    QTimer::singleShot(kReconnectDelayMs, this, [this]() {                     // - 지연 실행 예약: 3초 후 재접속 작업 진행
        m_reconnectPending = false;                                             // - 예약 상태 해제: 대기 상태 초기화

        if (m_intentionalDisconnect)                                            // - 수동 종료 확인: 대기 중 종료 요청 시 중단
            return;

        if (m_socket.state() != QAbstractSocket::UnconnectedState)              // - 소켓 상태 확인: 다른 접속 시도 중인 경우 중단
            return;

        m_socket.connectToHost(m_host, m_port);                                 // - 재접속 시도: 저장된 주소 및 포트로 접속 요청
    });
}

void HandoverClient::handleReadyRead()
{
    m_buffer += m_socket.readAll();                                             // - 데이터 누적: 수신 데이터를 버퍼에 연속 저장

    int newlineIndex;
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {                    // - 줄 단위 처리: 개행 문자(\n) 검색 및 반복 처리
        const QByteArray line = m_buffer.left(newlineIndex);                   // - 한 줄 추출: 개행 문자 전까지 데이터 잘라내기
        m_buffer.remove(0, newlineIndex + 1);                                  // - 버퍼 정리: 처리된 데이터 및 개행 문자 제거

        if (!line.trimmed().isEmpty())                                         // - 유효 검증: 공백이 아닌 경우 해석 함수 호출
            processLine(line);
    }
}

void HandoverClient::processLine(const QByteArray &line)
{
    const QJsonObject obj = QJsonDocument::fromJson(line).object();             // - JSON 변환: 수신 문장 해석 및 객체 변환

    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("camera_assignment")) { // - 종류 검증: 카메라 할당 메시지 확인
        qCWarning(lcHandoverClient) << "알 수 없는 메시지 무시:" << line;     // - 경고 로그: 알 수 없는 메시지 기록
        return;
    }

    if (!m_terminalId.isEmpty()) {                                             // - 단말 ID 확인: 단말 ID가 설정된 경우 검증 진행
        if (!obj.contains(QStringLiteral("terminal_id"))) {                   // - ID 존재 확인: 수신 대상 ID 누락 시 무시
            qCWarning(lcHandoverClient) << "terminal_id 없는 camera_assignment 무시:" << line;
            return;
        }

        const QString msgTerminalId = obj.value(QStringLiteral("terminal_id")).toString();
        if (msgTerminalId != m_terminalId) {                                   // - ID 일치 확인: 타 단말 대상 메시지인 경우 무시
            qCWarning(lcHandoverClient) << "다른 단말(" << msgTerminalId << ") 대상 명령 무시:" << line;
            return;
        }
    }

    const QString cameraId = obj.value(QStringLiteral("camera_id")).toString(); // - 카메라 ID 추출: 할당된 카메라 식별자 추출
    if (cameraId.isEmpty()) {                                                  // - ID 누락 확인: 카메라 ID가 빈 값인 경우 무시
        qCWarning(lcHandoverClient) << "camera_assignment에 camera_id 없음:" << line;
        return;
    }

    emit cameraHandoverRequested(cameraId);                                    // - 화면 전환 요청: 카메라 핸드오버 신호 발생
}

void HandoverClient::setConnectionState(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)                                            // - 중복 변경 방지: 현재 상태와 동일한 경우 생략
        return;

    m_connectionState = state;                                                 // - 상태값 갱신: 내부 상태 변수 업데이트
    emit connectionStateChanged(state);                                        // - 신호 발생: 상태 변경 이벤트 외부 전달
}
