#include "SerialWarningDevice.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcFpga, "safety.network.fpga")

// - Pi -> FPGA 명령 코드 (PROTOCOL.md 1절)
constexpr quint8 kCmdSetRisk = 0x01;
constexpr quint8 kCmdClearError = 0x02;
constexpr quint8 kCmdSelfTest = 0x03;
constexpr quint8 kTxHeader = 0xAA;

// - FPGA -> Pi 이벤트 코드 (PROTOCOL.md 2.1/2.2절)
constexpr quint8 kRxHeader = 0x55;
constexpr quint8 kEventWatchdogTimeout = 0x05;
constexpr quint8 kEventCommRecovered = 0x06;
constexpr quint8 kEventEstopActive = 0x07;
constexpr quint8 kEventEstopCleared = 0x08;
constexpr quint8 kEventHeartbeat = 0x0E;

// - heartbeat event_detail[2:0] = warning_fsm.v의 현재 상태값
constexpr quint8 kWarningStateCommError = 4;   // - warning_fsm.v STATE_COMM_ERROR
constexpr quint8 kWarningStateEstop     = 5;   // - warning_fsm.v STATE_ESTOP

constexpr int kRxFrameSize = 5;
constexpr int kWatchdogTxIntervalMs = 100;  // - PROTOCOL.md 1.2절: FPGA watchdog 500ms 타임아웃 대비 5배 여유
constexpr int kHeartbeatPollIntervalMs = 100;
constexpr int kHeartbeatTimeoutMs = 600;    // - PROTOCOL.md 4절: heartbeat 3주기(200ms x 3) 무수신 시 연결 끊김 판단
constexpr int kReconnectDelayMs = 3000;     // - RtspVideoSource::scheduleReconnect()와 동일한 재시도 간격
}

SerialWarningDevice::SerialWarningDevice(QString portName, qint32 baudRate, QObject *parent)
    : IWarningDevice(parent)
    , m_portName(std::move(portName))
    , m_baudRate(baudRate)
{
    connect(&m_port, &QSerialPort::readyRead, this, &SerialWarningDevice::handleReadyRead); // - 수신 바이트 도착 시 파싱
    connect(&m_port, &QSerialPort::errorOccurred, this, &SerialWarningDevice::handleSerialError); // - 포트 오류 시 재연결
    connect(&m_watchdogTxTimer, &QTimer::timeout, this, &SerialWarningDevice::handleWatchdogTxTimer); // - 100ms 송신
    connect(&m_heartbeatWatchTimer, &QTimer::timeout, this, &SerialWarningDevice::handleHeartbeatWatchTimer); // - 무수신 감시

    m_reconnectTimer.setSingleShot(true);                                                     // - 1회성 설정: 발화 후 스스로 정지
    connect(&m_reconnectTimer, &QTimer::timeout, this, &SerialWarningDevice::handleReconnectTimer); // - 대기 시간 후 재시도
}

SerialWarningDevice::~SerialWarningDevice()
{
    stop();
}

void SerialWarningDevice::start()
{
    m_intentionalDisconnect = false;
    openPort();
}

void SerialWarningDevice::stop()
{
    m_intentionalDisconnect = true; // - 이후 포트 오류가 나도 재연결 예약하지 않음
    m_watchdogTxTimer.stop();
    m_heartbeatWatchTimer.stop();
    m_reconnectTimer.stop();        // - 예약된 재시도 취소
    if (m_port.isOpen())
        m_port.close();
    setFpgaConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void SerialWarningDevice::openPort()
{
    if (m_port.isOpen())
        m_port.close();

    m_port.setPortName(m_portName);
    m_port.setBaudRate(m_baudRate);
    m_port.setDataBits(QSerialPort::Data8);
    m_port.setParity(QSerialPort::NoParity);
    m_port.setStopBits(QSerialPort::OneStop);
    m_port.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        qCWarning(lcFpga) << "failed to open" << m_portName << ":" << m_port.errorString();
        scheduleReconnect();
        return;
    }

    qCInfo(lcFpga) << "opened" << m_portName << "at" << m_baudRate << "baud, waiting for first heartbeat";
    m_rxBuffer.clear();
    m_lastRxTimer.start();                                      // - 이후 600ms 안에 첫 프레임이 안 오면 무수신 타임아웃으로 처리
    setFpgaConnectionState(RiskTypes::ConnectionState::Connecting); // - 첫 프레임을 받기 전까지는 Connected로 올리지 않음

    m_watchdogTxTimer.start(kWatchdogTxIntervalMs);
    m_heartbeatWatchTimer.start(kHeartbeatPollIntervalMs);
    handleWatchdogTxTimer();                                    // - 다음 100ms 주기까지 기다리지 않고 연결 즉시 1회 선전송
}

// - 열기 실패 1회에 예약이 두 번 걸리는 것을 막는 게 핵심이다.
//   m_port.open()이 실패하면 그 안에서 errorOccurred가 바로 발생해 handleSerialError()가 먼저 여기를 부르고,
//   open()이 false를 돌려준 뒤 openPort()가 또 한 번 여기를 부른다.
//   예전처럼 QTimer::singleShot을 쓰면 취소가 안 돼서 예약이 매번 2개로 불어나고,
//   그 2개가 각각 다시 2개를 만들어 3초마다 배로 늘어났다(실측: 로그의 오류 쌍이 1 -> 2 -> 4 -> 8개).
//   멤버 타이머는 start()를 다시 불러도 재시작될 뿐이라 예약이 항상 최대 1건으로 유지된다.
void SerialWarningDevice::scheduleReconnect()
{
    setFpgaConnectionState(RiskTypes::ConnectionState::Disconnected);
    m_watchdogTxTimer.stop();
    m_heartbeatWatchTimer.stop();

    if (m_intentionalDisconnect)    // - 수동 정지 확인: stop()으로 멈춘 장치는 되살리지 않음
        return;

    m_reconnectTimer.start(kReconnectDelayMs); // - 재시도 예약: 이미 돌고 있으면 재시작될 뿐 중복 예약 안 됨
}

void SerialWarningDevice::handleReconnectTimer()
{
    if (m_intentionalDisconnect)    // - 수동 정지 확인: 대기 중 정지 요청이 들어온 경우 중단
        return;

    openPort();                     // - 포트 재시도: 실패하면 다시 한 건만 예약됨
}

void SerialWarningDevice::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;
    qCWarning(lcFpga) << "serial port error:" << m_port.errorString();
    scheduleReconnect();
}

void SerialWarningDevice::setRiskLevel(RiskTypes::RiskLevel level)
{
    m_lastRiskLevel = level; // - 실제 전송은 handleWatchdogTxTimer가 100ms마다 전담 (PROTOCOL.md 1.2절)
}

void SerialWarningDevice::sendClearError()
{
    sendFrame(kCmdClearError, 0x00);
}

void SerialWarningDevice::sendSelfTest(int mode)
{
    sendFrame(kCmdSelfTest, static_cast<quint8>(mode & 0x03));
}

void SerialWarningDevice::handleWatchdogTxTimer()
{
    // - 서버 링크 두절 구간에서는 보내지 않는다.
    //   여기서 SET_RISK(0)을 계속 보내면 FPGA watchdog이 갱신돼 자율 경고가 안 걸린다.
    if (riskTxSuspended())
        return;
    sendFrame(kCmdSetRisk, static_cast<quint8>(m_lastRiskLevel));
}

void SerialWarningDevice::onRiskTxSuspendedChanged(bool suspended)
{
    if (suspended) {
        qCWarning(lcFpga) << "server link lost -- suspending SET_RISK so the FPGA watchdog can fire";
        return;                                                   // - 타이머는 계속 돌린다. 재개 시 바로 복귀하기 위함
    }
    qCInfo(lcFpga) << "server link restored -- resuming SET_RISK";
    handleWatchdogTxTimer();                                       // - 다음 100ms 주기를 기다리지 않고 즉시 1회 송신
}

void SerialWarningDevice::sendFrame(quint8 command, quint8 data)
{
    if (!m_port.isOpen())
        return;

    const quint8 checksum = kTxHeader ^ command ^ data;
    const char bytes[4] = {static_cast<char>(kTxHeader), static_cast<char>(command), static_cast<char>(data),
                            static_cast<char>(checksum)};
    m_port.write(bytes, sizeof(bytes));
}

void SerialWarningDevice::handleReadyRead()
{
    m_rxBuffer.append(m_port.readAll());

    // - 0x55 헤더로 재동기화하며 5바이트 프레임을 순서대로 뽑아낸다.
    //   깨진 바이트가 섞여도 다음 헤더를 찾아 스스로 복구한다 (packet_parser.v와 대칭되는 관용적 파싱).
    while (true) {
        const int headerIndex = m_rxBuffer.indexOf(static_cast<char>(kRxHeader));
        if (headerIndex < 0) {
            m_rxBuffer.clear();
            return;
        }
        if (headerIndex > 0)
            m_rxBuffer.remove(0, headerIndex);

        if (m_rxBuffer.size() < kRxFrameSize)
            return; // - 프레임이 아직 다 안 들어옴, 다음 readyRead를 기다린다

        const QByteArray frame = m_rxBuffer.left(kRxFrameSize);
        quint8 checksum = 0;
        for (int i = 0; i < kRxFrameSize - 1; ++i)
            checksum ^= static_cast<quint8>(frame.at(i));

        if (checksum != static_cast<quint8>(frame.at(kRxFrameSize - 1))) {
            // - 체크섬 불일치: 이 0x55는 진짜 헤더가 아니라 데이터 안에 우연히 낀 값이었을 수 있으므로
            //   1바이트만 버리고 다음 0x55를 다시 찾는다.
            m_rxBuffer.remove(0, 1);
            continue;
        }

        m_rxBuffer.remove(0, kRxFrameSize);
        processFrame(frame);
    }
}

void SerialWarningDevice::processFrame(const QByteArray &frame)
{
    m_lastRxTimer.start();
    if (fpgaConnectionState() != RiskTypes::ConnectionState::Connected)
        setFpgaConnectionState(RiskTypes::ConnectionState::Connected);

    const quint8 eventCode = static_cast<quint8>(frame.at(1));
    const quint8 eventDetail = static_cast<quint8>(frame.at(2));
    const quint8 statusByte = static_cast<quint8>(frame.at(3));

    // - 4번째 바이트 bit6: 모든 메시지(heartbeat 포함)에 실리는 전진 차단 릴레이 현재 상태 스냅샷 (PROTOCOL.md 2.1절)
    setMovementCutoffActive((statusByte & 0x40) != 0);

    switch (eventCode) {
    case kEventWatchdogTimeout:
        setCommError(true);
        break;
    case kEventCommRecovered:
        setCommError(false);
        break;
    case kEventEstopActive:
        setEstopActive(true);
        break;
    case kEventEstopCleared:
        setEstopActive(false);
        break;
    case kEventHeartbeat: {
        // - PROTOCOL.md 2.2절 heartbeat event_detail 비트맵: bit7/6/5 = 오류 누적 플래그
        const bool timeoutLatched = (eventDetail & 0x80) != 0;
        const bool protocolLatched = (eventDetail & 0x40) != 0;
        const bool checksumLatched = (eventDetail & 0x20) != 0;
        setErrorLatches(checksumLatched, protocolLatched, timeoutLatched);

        // - bit[2:0] = warning_fsm 현재 상태. 이벤트(0x07/0x08)를 놓친 뒤에도
        //   여기서 매 200ms 재동기화되므로, 앱만 재시작해도 래치 상태가 복원된다.
        const quint8 warningState = eventDetail & 0x07;

        // - ESTOP은 warning_fsm 최우선순위라 "5인가"만으로 켬/끔이 모두 정확하다.
        setEstopActive(warningState == kWarningStateEstop);

        // - COMM_ERROR는 반대 방향이 성립하지 않는다. ESTOP(5)이면 통신 오류가
        //   참이어도 5로 덮이므로, 5일 때는 판단을 보류하고 이벤트가 넣은 값을 유지한다.
        if (warningState == kWarningStateCommError)
            setCommError(true);
        else if (warningState != kWarningStateEstop)
            setCommError(false);
        break;
    }
    default:
        // - CHECKSUM/PROTOCOL/INTERBYTE_TIMEOUT/FRAMING 등 나머지 이벤트는 진단 로그로만 남긴다.
        //   오류 누적 플래그는 다음 heartbeat에서 반영되므로 여기서 별도 상태 갱신은 불필요하다.
        qCDebug(lcFpga) << "event 0x" << Qt::hex << eventCode << "detail 0x" << eventDetail;
        break;
    }
}

void SerialWarningDevice::handleHeartbeatWatchTimer()
{
    if (m_lastRxTimer.isValid() && m_lastRxTimer.elapsed() > kHeartbeatTimeoutMs)
        setFpgaConnectionState(RiskTypes::ConnectionState::Disconnected);
}
