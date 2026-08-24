#include "LatencyTracker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

Q_LOGGING_CATEGORY(lcLatency, "safety.latency")

LatencyTracker &LatencyTracker::instance()
{
    static LatencyTracker s_instance;
    return s_instance;
}

LatencyTracker::LatencyTracker()
{
    m_timer.start();
}

LatencyTracker::~LatencyTracker()
{
    flush();
    QMutexLocker fileLocker(&m_fileMutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
}

void LatencyTracker::initialize(const QString &csvPath)
{
    QMutexLocker locker(&m_mutex);
    const QString rawPath = csvPath.trimmed();
    if (rawPath.isEmpty()) {
        m_enabled = false;
        qCInfo(lcLatency) << "latency logging disabled (no latency_log_path configured)";
        return;
    }

    QFileInfo fileInfo(rawPath);
    if (fileInfo.isRelative()) {
        m_csvPath = fileInfo.absoluteFilePath();
        qCWarning(lcLatency) << "latency_log_path is relative (" << rawPath
                             << ") -- resolved to absolute path:" << m_csvPath;
    } else {
        m_csvPath = fileInfo.absoluteFilePath();
    }

    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const bool fileExists = fileInfo.exists() && fileInfo.size() > 0;
    m_file.setFileName(m_csvPath);
    {
        QMutexLocker fileLocker(&m_fileMutex);
        if (m_file.isOpen()) {
            m_file.close();
        }
        if (m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            if (!fileExists) {
                QTextStream stream(&m_file);
                stream << "seq,risk_level,t1_ms,t2_ms,t3_ms,t1_to_t2_ms,t1_to_t3_ms,t3_status\n";
                m_file.flush();
            }
            m_enabled = true;
            qCInfo(lcLatency) << "latency logging initialized -> absolute path:" << m_csvPath;
        } else {
            qCWarning(lcLatency) << "failed to open latency log file:" << m_csvPath << m_file.errorString();
            m_enabled = false;
            return;
        }
    }

    if (!m_diskFlushTimer.isActive()) {
        m_diskFlushTimer.setInterval(1000); // 1초마다 주기적 일괄 플러시
        connect(&m_diskFlushTimer, &QTimer::timeout, this, &LatencyTracker::flushPendingRows);
        m_diskFlushTimer.start();
    }
}

bool LatencyTracker::isEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_enabled;
}

void LatencyTracker::onT1RiskEventReceived(int riskLevel)
{
    // 락 대기 시간 오염 방지: QElapsedTimer(단조 시계) 읽기 전용 타임스탬프를 락 획득 전에 선행 기록
    const qint64 now = m_timer.elapsed();

    QMutexLocker locker(&m_mutex);
    if (!m_enabled)
        return;

    // 위험도 변경이 발생한 경우에만 계측 착수
    if (riskLevel == m_lastRiskLevel)
        return;

    // 이전 미완료 계측이 있다면 큐에 강제 등록
    if (m_current.seq > 0) {
        m_pendingRows.append(m_current);
        m_current = Measurement();
    }

    m_lastRiskLevel = riskLevel;
    m_seqCounter++;

    m_current.seq = m_seqCounter;
    m_current.riskLevel = riskLevel;
    m_current.t1_ms = now;
    m_current.t2_ms = -1;
    m_current.t3_ms = -1;
    m_current.t2Done = false;
    m_current.t3Done = false;
    m_current.t3Status = QStringLiteral("INCOMPLETE");

    m_awaitingSync = true;
    m_t2Armed = false;
    m_t3Armed = true;

    qCInfo(lcLatency) << QStringLiteral("[T1: MQTT Recv] seq=%1, risk_level=%2, t1=%3 ms")
                             .arg(m_current.seq)
                             .arg(m_current.riskLevel)
                             .arg(m_current.t1_ms);
}

void LatencyTracker::onAfterSynchronizing()
{
    QMutexLocker locker(&m_mutex);
    if (!m_enabled || !m_awaitingSync || m_current.seq == 0)
        return;

    m_awaitingSync = false;
    m_t2Armed = true;
}

void LatencyTracker::onT2FrameSwapped()
{
    // 렌더 스레드 발생 시점의 순수 타임스탬프 선행 획득
    const qint64 now = m_timer.elapsed();

    QMutexLocker locker(&m_mutex);
    if (!m_enabled || !m_t2Armed || m_current.seq == 0)
        return;

    m_current.t2_ms = now;
    m_current.t2Done = true;
    m_t2Armed = false;

    // 결함 B 해결: 렌더 스레드 내 동기 I/O(qCInfo -> std::fflush) 방지를 위해 로그 제거 (CSV에 기록됨)

    checkAndEnqueueLocked();
}

void LatencyTracker::onT3RiskTransmitted(int riskLevel)
{
    // FPGA 실제 시리얼 전송 성공 시점의 타임스탬프 선행 획득
    const qint64 now = m_timer.elapsed();

    QMutexLocker locker(&m_mutex);
    if (!m_enabled || !m_t3Armed || m_current.seq == 0)
        return;

    m_current.t3_ms = now;
    m_current.t3Done = true;
    m_current.t3Status = QStringLiteral("OK");
    m_t3Armed = false;

    const qint64 diffT1T3 = m_current.t3_ms - m_current.t1_ms;
    qCInfo(lcLatency) << QStringLiteral("[T3: FPGA Transmit] seq=%1, t3=%2 ms (t1_to_t3=%3 ms)")
                             .arg(m_current.seq)
                             .arg(m_current.t3_ms)
                             .arg(diffT1T3);

    checkAndEnqueueLocked();
}

void LatencyTracker::onT3RiskTransmitSuspended()
{
    QMutexLocker locker(&m_mutex);
    if (!m_enabled || !m_t3Armed || m_current.seq == 0)
        return;

    m_current.t3_ms = -1;
    m_current.t3Done = true;
    m_current.t3Status = QStringLiteral("TX_SUSPENDED");
    m_t3Armed = false;

    qCInfo(lcLatency) << QStringLiteral("[T3: Skipped] seq=%1, reason=TX_SUSPENDED")
                             .arg(m_current.seq);

    checkAndEnqueueLocked();
}

void LatencyTracker::onT3RiskTransmitPortUnavailable()
{
    QMutexLocker locker(&m_mutex);
    if (!m_enabled || !m_t3Armed || m_current.seq == 0)
        return;

    m_current.t3_ms = -1;
    m_current.t3Done = true;
    m_current.t3Status = QStringLiteral("PORT_CLOSED");
    m_t3Armed = false;

    qCInfo(lcLatency) << QStringLiteral("[T3: Skipped] seq=%1, reason=PORT_CLOSED")
                             .arg(m_current.seq);

    checkAndEnqueueLocked();
}

void LatencyTracker::checkAndEnqueueLocked()
{
    // T2와 T3가 모두 확정되면 메모리 큐에 추가 (파일 I/O를 렌더 스레드 밖으로 분리)
    if (m_current.t2Done && m_current.t3Done && m_current.seq > 0) {
        m_pendingRows.append(m_current);
        m_current = Measurement();
    }
}

void LatencyTracker::flushPendingRows()
{
    QVector<Measurement> rowsToWrite;
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingRows.isEmpty())
            return;
        rowsToWrite.swap(m_pendingRows);
    }

    // 락 해제 후 디스크 I/O 수행
    writeRowsToDisk(rowsToWrite);
}

void LatencyTracker::flush()
{
    QVector<Measurement> rowsToWrite;
    {
        QMutexLocker locker(&m_mutex);
        if (m_current.seq > 0) {
            m_pendingRows.append(m_current);
            m_current = Measurement();
        }
        if (m_pendingRows.isEmpty())
            return;
        rowsToWrite.swap(m_pendingRows);
    }

    writeRowsToDisk(rowsToWrite);
}

void LatencyTracker::writeRowsToDisk(const QVector<Measurement> &rows)
{
    if (rows.isEmpty())
        return;

    QMutexLocker locker(&m_fileMutex);
    if (!m_file.isOpen()) {
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            qCWarning(lcLatency) << "failed to write pending latency rows:" << m_file.errorString();
            return;
        }
    }

    QTextStream stream(&m_file);
    for (const Measurement &m : rows) {
        const QString t1ToT2Str = (m.t2_ms >= 0) ? QString::number(m.t2_ms - m.t1_ms) : QStringLiteral("-1");
        const QString t1ToT3Str = (m.t3_ms >= 0) ? QString::number(m.t3_ms - m.t1_ms) : QStringLiteral("-1");

        stream << m.seq << ','
               << m.riskLevel << ','
               << m.t1_ms << ','
               << m.t2_ms << ','
               << m.t3_ms << ','
               << t1ToT2Str << ','
               << t1ToT3Str << ','
               << m.t3Status << '\n';
    }
    m_file.flush();
}
