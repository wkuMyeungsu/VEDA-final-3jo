#include "HandoverClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>

namespace {
// - 로깅 카테고리 정의: 통신 로그 분류용 이름 지정
Q_LOGGING_CATEGORY(lcHandoverClient, "safety.handover.client")

// 재연결 대기 시간 설정 (3초)
// - 서버 부하 방지: 연속 재시도로 인한 서버 및 로그 과부하 차단
// - 신속한 복구: 빠른 재연결을 위한 적정 대기 시간 지정
// - 영상 채널 동기화: 영상 수신 모듈(RtspVideoSource)과 동일한 대기 시간 적용
constexpr int kReconnectDelayMs = 3000;
}

// 생성자
HandoverClient::HandoverClient(QObject *parent)
    : QObject(parent)
{
    // - 연결 성공 이벤트 연결: 서버 접속 완료 시 처리 함수 지정
    connect(&m_socket, &QTcpSocket::connected, this, &HandoverClient::handleConnected);

    // - 연결 끊김 이벤트 연결: 서버 접속 해제 시 처리 함수 지정
    connect(&m_socket, &QTcpSocket::disconnected, this, &HandoverClient::handleDisconnected);

    // - 데이터 수신 이벤트 연결: 데이터 도착 시 처리 함수 지정
    connect(&m_socket, &QTcpSocket::readyRead, this, &HandoverClient::handleReadyRead);

    // - 오류 발생 이벤트 연결: 통신 에러 발생 시 처리 함수 지정
    connect(&m_socket, &QTcpSocket::errorOccurred, this, &HandoverClient::handleError);
}

// 서버 접속 요청
void HandoverClient::connectToServer(const QString &host, quint16 port)
{
    // - 접속 주소 저장: 재연결에 사용할 호스트 정보 보관
    m_host = host;

    // - 접속 포트 저장: 재연결에 사용할 포트 정보 보관
    m_port = port;

    // - 수동 종료 플래그 초기화: 자동 재연결 기능 활성화
    m_intentionalDisconnect = false;

    // - 상태 변경: 연결 진행 중 상태로 설정
    setConnectionState(RiskTypes::ConnectionState::Connecting);

    // - 서버 접속 시도: 지정된 주소 및 포트로 연결 요청
    m_socket.connectToHost(host, port);
}

// 서버 연결 종료
void HandoverClient::disconnectFromServer()
{
    // - 수동 종료 설정: 자동 재연결 작동 차단
    m_intentionalDisconnect = true;

    // - 연결 해제: 서버와의 소켓 연결 종료
    m_socket.disconnectFromHost();
}

// 서버 접속 성공 처리
void HandoverClient::handleConnected()
{
    // - 상태 갱신: 연결 상태를 '연결됨'으로 변경
    setConnectionState(RiskTypes::ConnectionState::Connected);

    // - 식별 정보 전송: 서버에 단말 정보 전송 함수 호출
    sendHello();
}

// 서버에 단말 식별 정보(terminal_id) 전송
void HandoverClient::sendHello()
{
    // - ID 확인: 단말 ID 미설정 시 전송 생략
    if (m_terminalId.isEmpty())
        return;

    // - 메시지 객체 생성: 전송용 JSON 객체 생성
    QJsonObject obj;

    // - 메시지 유형 설정: 'hello' 타입 지정
    obj[QStringLiteral("type")] = QStringLiteral("hello");

    // - 단말 ID 설정: 현재 단말 식별자 지정
    obj[QStringLiteral("terminal_id")] = m_terminalId;

    // - 데이터 전송: JSON 메시지 바이너리 변환 및 전송
    m_socket.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));

    // - 줄바꿈 전송: 메시지 구분용 개행 문자 전송
    m_socket.write("\n");
}

// 서버 연결 끊김 처리
void HandoverClient::handleDisconnected()
{
    // - 버퍼 초기화: 수신 데이터 임시 저장소 비우기
    m_buffer.clear();

    // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
    setConnectionState(RiskTypes::ConnectionState::Disconnected);

    // - 재연결 예약: 일정 시간 후 재접속 시도
    scheduleReconnect();
}

// 소켓 오류 발생 처리
void HandoverClient::handleError(QAbstractSocket::SocketError error)
{
    // - 경고 로그 출력: 오류 코드 및 상세 원인 메시지 기록
    qCWarning(lcHandoverClient) << "소켓 오류:" << error << m_socket.errorString();

    // - 상태 갱신: 연결 상태를 '연결 끊김'으로 변경
    setConnectionState(RiskTypes::ConnectionState::Disconnected);

    // - 재연결 예약: 최초 접속 실패 시에도 복구되도록 재시도
    scheduleReconnect();
}

// 자동 재연결 예약
void HandoverClient::scheduleReconnect()
{
    // - 중복 시도 차단: 수동 종료, 이미 예약됨, 주소 미설정 시 처리 생략
    if (m_intentionalDisconnect || m_reconnectPending || m_host.isEmpty())
        return;

    // - 예약 상태 설정: 중복 타이머 생성 방지
    m_reconnectPending = true;

    // - 지연 실행 예약: 설정 시간(3초) 후 재접속 작업 진행
    QTimer::singleShot(kReconnectDelayMs, this, [this]() {
        // - 예약 상태 해제: 대기 상태 초기화
        m_reconnectPending = false;

        // - 수동 종료 여부 확인: 대기 중 종료 요청 발생 시 중단
        if (m_intentionalDisconnect)
            return;

        // - 소켓 상태 확인: 이미 다른 접속 시도 중인 경우 중단
        if (m_socket.state() != QAbstractSocket::UnconnectedState)
            return;

        // - 재접속 시도: 저장된 주소 및 포트로 접속 요청
        m_socket.connectToHost(m_host, m_port);
    });
}

// 데이터 수신 처리
void HandoverClient::handleReadyRead()
{
    // - 데이터 누적: 수신된 데이터를 임시 버퍼에 누적 저장
    m_buffer += m_socket.readAll();

    int newlineIndex;
    // - 줄 단위 처리: 개행 문자(\n) 검색 및 반복 처리
    while ((newlineIndex = m_buffer.indexOf('\n')) != -1) {
        // - 한 줄 추출: 개행 문자 전까지의 데이터 잘라내기
        const QByteArray line = m_buffer.left(newlineIndex);

        // - 버퍼 정리: 처리된 데이터 및 개행 문자 제거
        m_buffer.remove(0, newlineIndex + 1);

        // - 유효 데이터 검증: 공백이 아닌 경우 해석 함수 호출
        if (!line.trimmed().isEmpty())
            processLine(line);
    }
}

// 수신된 JSON 데이터 해석 처리
void HandoverClient::processLine(const QByteArray &line)
{
    // - JSON 객체 변환: 수신 문장 해석 및 객체 변환
    const QJsonObject obj = QJsonDocument::fromJson(line).object();

    // - 메시지 종류 확인: 카메라 할당(camera_assignment) 메시지 검증
    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("camera_assignment")) {
        // - 경고 로그: 알 수 없는 메시지 기록 및 무시
        qCWarning(lcHandoverClient) << "알 수 없는 메시지 무시:" << line;
        return;
    }

    // - 수신 단말 검증: 단말 ID가 설정된 경우 메시지 대상 확인
    if (!m_terminalId.isEmpty()) {
        // - ID 존재 여부 확인: 수신 대상 ID 누락 시 무시
        if (!obj.contains(QStringLiteral("terminal_id"))) {
            qCWarning(lcHandoverClient) << "terminal_id 없는 camera_assignment 무시:" << line;
            return;
        }

        // - ID 일치 여부 확인: 타 단말 대상 메시지인 경우 무시
        const QString msgTerminalId = obj.value(QStringLiteral("terminal_id")).toString();
        if (msgTerminalId != m_terminalId) {
            qCWarning(lcHandoverClient) << "다른 단말(" << msgTerminalId << ") 대상 명령 무시:" << line;
            return;
        }
    }

    // - 카메라 ID 추출: 할당된 카메라 식별자 추출
    const QString cameraId = obj.value(QStringLiteral("camera_id")).toString();

    // - ID 누락 확인: 카메라 ID가 빈 값인 경우 무시
    if (cameraId.isEmpty()) {
        qCWarning(lcHandoverClient) << "camera_assignment에 camera_id 없음:" << line;
        return;
    }

    // - 화면 전환 요청: 카메라 핸도버 신호 발생
    emit cameraHandoverRequested(cameraId);
}

// 연결 상태 변경 및 신호 발생
void HandoverClient::setConnectionState(RiskTypes::ConnectionState state)
{
    // - 중복 변경 방지: 현재 상태와 동일한 경우 처리 생략
    if (m_connectionState == state)
        return;

    // - 상태값 갱신: 내부 상태 변수 업데이트
    m_connectionState = state;

    // - 신호 발생: 상태 변경 이벤트 외부 전달
    emit connectionStateChanged(state);
}