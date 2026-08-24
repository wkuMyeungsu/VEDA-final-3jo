#include <QtTest>

#include "network/SerialWarningDevice.h"

class TestSerialWarningDevice : public QObject
{
    Q_OBJECT

private slots:
    void heartbeatEstopRestored();
    void heartbeatCommErrorRestoredAndPreservedDuringEstop();
    void movementCutoffActiveRestoredFromByte3Bit6();
    void riskTxSuspendedPropertyAndSignal();

private:
    static QByteArray makeFrame(quint8 eventCode, quint8 eventDetail, quint8 statusByte)
    {
        const quint8 header = 0x55;
        const quint8 checksum = header ^ eventCode ^ eventDetail ^ statusByte;
        QByteArray frame;
        frame.append(static_cast<char>(header));
        frame.append(static_cast<char>(eventCode));
        frame.append(static_cast<char>(eventDetail));
        frame.append(static_cast<char>(statusByte));
        frame.append(static_cast<char>(checksum));
        return frame;
    }
};

void TestSerialWarningDevice::heartbeatEstopRestored()
{
    SerialWarningDevice dev(QStringLiteral("/dev/null"), 115200);

    QCOMPARE(dev.estopActive(), false);

    // 1) ESTOP이 걸려있는 상태의 Heartbeat(0x0E), warning_state = 5 (ESTOP) 수신
    // eventDetail = 0x05 (warning_state=5)
    dev.processFrame(makeFrame(0x0E, 0x05, 0x00));
    QCOMPARE(dev.estopActive(), true);

    // 2) ESTOP 해제 후 정상 Heartbeat, warning_state = 0 (SAFE) 수신
    dev.processFrame(makeFrame(0x0E, 0x00, 0x00));
    QCOMPARE(dev.estopActive(), false);
}

void TestSerialWarningDevice::heartbeatCommErrorRestoredAndPreservedDuringEstop()
{
    SerialWarningDevice dev(QStringLiteral("/dev/null"), 115200);

    QCOMPARE(dev.commError(), false);
    QCOMPARE(dev.estopActive(), false);

    // 1) 통신 오류 상태의 Heartbeat, warning_state = 4 (COMM_ERROR) 수신
    dev.processFrame(makeFrame(0x0E, 0x04, 0x00));
    QCOMPARE(dev.commError(), true);
    QCOMPARE(dev.estopActive(), false);

    // 2) COMM_ERROR 상태에서 ESTOP(5) 발생: warning_state = 5 (ESTOP) 수신
    // ESTOP(5)으로 덮여도 COMM_ERROR(true)가 거짓으로 꺼지지 않고 유지되어야 함
    dev.processFrame(makeFrame(0x0E, 0x05, 0x00));
    QCOMPARE(dev.estopActive(), true);
    QCOMPARE(dev.commError(), true); // 보존 확인!

    // 3) 정상 상태(0) 수신 시 둘 다 해제
    dev.processFrame(makeFrame(0x0E, 0x00, 0x00));
    QCOMPARE(dev.estopActive(), false);
    QCOMPARE(dev.commError(), false);
}

void TestSerialWarningDevice::movementCutoffActiveRestoredFromByte3Bit6()
{
    SerialWarningDevice dev(QStringLiteral("/dev/null"), 115200);

    QCOMPARE(dev.movementCutoffActive(), false);

    // statusByte bit6 (0x40) = movement_cutoff_active
    dev.processFrame(makeFrame(0x0E, 0x00, 0x40));
    QCOMPARE(dev.movementCutoffActive(), true);

    dev.processFrame(makeFrame(0x0E, 0x00, 0x00));
    QCOMPARE(dev.movementCutoffActive(), false);
}

void TestSerialWarningDevice::riskTxSuspendedPropertyAndSignal()
{
    SerialWarningDevice dev(QStringLiteral("/dev/null"), 115200);

    QCOMPARE(dev.riskTxSuspended(), false);

    QSignalSpy spy(&dev, &IWarningDevice::riskTxSuspendedChanged);

    dev.setRiskTxSuspended(true);
    QCOMPARE(dev.riskTxSuspended(), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), true);

    // 중복 설정 시 시그널 미발생 확인
    dev.setRiskTxSuspended(true);
    QCOMPARE(spy.count(), 0);

    dev.setRiskTxSuspended(false);
    QCOMPARE(dev.riskTxSuspended(), false);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
}

QTEST_MAIN(TestSerialWarningDevice)
#include "test_serial_warning_device.moc"
