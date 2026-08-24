#pragma once

#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

Q_DECLARE_LOGGING_CATEGORY(lcLatency)

// - 단말 내부 지연 계측 싱글톤 서비스
//   T1 (MQTT 수신) -> T2 (QML 첫 렌더 frameSwapped) -> T3 (FPGA 100ms 주기 첫 실송신)
class LatencyTracker : public QObject
{
    Q_OBJECT

public:
    static LatencyTracker &instance();

    // - CSV 경로 지정 및 계측 활성화 (미지정 시 비활성화)
    void initialize(const QString &csvPath);
    bool isEnabled() const;

    // - T1: RiskEventSource에서 위험도 변경 이벤트 수신 시 호출
    void onT1RiskEventReceived(int riskLevel);

public slots:
    // - Scene Graph 속성 동기화 완료 시점 (렌더 스레드에서 직접 호출)
    void onAfterSynchronizing();

    // - T2: 속성 동기화 후 화면에 실제 표출된 첫 frameSwapped 시점에 호출 (렌더 스레드에서 직접 호출)
    void onT2FrameSwapped();

    // - T3: SerialWarningDevice에서 변경된 위험도가 100ms 주기로 실제 송신되는 첫 시점에 호출
    void onT3RiskTransmitted(int riskLevel);

    // - T3 예외 1: 서버 두절 등으로 송신이 의도적으로 중단된 경우 호출 (이유: TX_SUSPENDED)
    void onT3RiskTransmitSuspended();

    // - T3 예외 2: FPGA 미연결/시리얼 포트 닫힘/write 실패로 송신하지 못한 경우 호출 (이유: PORT_CLOSED)
    void onT3RiskTransmitPortUnavailable();

    // - 잔여 측정값 일괄 디스크 플러시
    void flush();

private slots:
    void flushPendingRows();

private:
    LatencyTracker();
    ~LatencyTracker() override;

    struct Measurement {
        quint64 seq = 0;
        int riskLevel = 0;
        qint64 t1_ms = 0;
        qint64 t2_ms = -1;
        qint64 t3_ms = -1;
        bool t2Done = false;
        bool t3Done = false;
        QString t3Status = QStringLiteral("INCOMPLETE");
    };

    void checkAndEnqueueLocked();
    void writeRowsToDisk(const QVector<Measurement> &rows);

    mutable QMutex m_mutex;
    mutable QMutex m_fileMutex;
    QElapsedTimer m_timer;
    QString m_csvPath;
    QFile m_file;
    QTimer m_diskFlushTimer;
    bool m_enabled = false;

    quint64 m_seqCounter = 0;
    int m_lastRiskLevel = -1;
    bool m_awaitingSync = false;
    bool m_t2Armed = false;
    bool m_t3Armed = false;
    Measurement m_current;
    QVector<Measurement> m_pendingRows;
};
