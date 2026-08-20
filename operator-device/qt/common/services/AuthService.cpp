#include "AuthService.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QProcess>
#include <QUrl>

namespace {
Q_LOGGING_CATEGORY(lcAuth, "safety.auth")
}

AuthService::AuthService(QVector<OperatorAccount> operators, QObject *parent, int maxAttempts, int lockSeconds)
    : QObject(parent)
    , m_operators(std::move(operators))
    , m_maxAttempts(maxAttempts)
    , m_lockSeconds(lockSeconds)
{
    m_lockTimer.setInterval(1000);
    connect(&m_lockTimer, &QTimer::timeout, this, &AuthService::tickLock);
    qCInfo(lcAuth) << "AuthService initialized with" << m_operators.size() << "registered operators";
}

QString AuthService::hashPin(const QString &operatorId, const QString &pin)
{
    const QByteArray salted = (operatorId + QStringLiteral(":") + pin).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(salted, QCryptographicHash::Sha256).toHex());
}

bool AuthService::login(const QString &operatorId, const QString &pin)
{
    if (locked()) {
        qCWarning(lcAuth) << "Rejected login attempt while locked for operatorId:" << operatorId;
        emit loginFailed(QStringLiteral("잠금 상태 -- %1초 후 다시 시도하세요").arg(lockRemainingSeconds()));
        return false;
    }

    int foundIndex = -1;
    for (int i = 0; i < m_operators.size(); ++i) {
        if (m_operators.at(i).operatorId == operatorId) {
            foundIndex = i;
            break;
        }
    }

    // 아이디가 없을 때와 PIN이 틀렸을 때 결과를 구분하지 않음 -- 실패 메시지
    // 차이로 유효한 아이디를 알아낼 수 없게 하기 위함
    const bool matches = foundIndex >= 0 && hashPin(operatorId, pin) == m_operators.at(foundIndex).pinHash;
    if (!matches) {
        ++m_failedAttempts;
        qCWarning(lcAuth) << "Failed login attempt for operatorId:" << operatorId
                          << "(failed attempts:" << m_failedAttempts << "/" << m_maxAttempts << ")";
        if (m_failedAttempts >= m_maxAttempts) {
            m_failedAttempts = 0;
            m_lockUntil = QDateTime::currentDateTime().addSecs(m_lockSeconds);
            m_lockTimer.start();
            qCWarning(lcAuth) << "Max login attempts exceeded! Console locked for" << m_lockSeconds << "seconds";
            emit lockedChanged();
            emit lockRemainingSecondsChanged();
            emit loginFailed(QStringLiteral("실패 횟수 초과 -- %1초간 잠깁니다").arg(m_lockSeconds));
        } else {
            emit loginFailed(QStringLiteral("아이디 또는 PIN이 올바르지 않습니다"));
        }
        return false;
    }

    m_failedAttempts = 0;
    m_currentIndex = foundIndex;
    const OperatorAccount &acc = m_operators.at(foundIndex);
    qCInfo(lcAuth) << "Operator logged in:" << acc.operatorId << "(" << acc.displayName
                   << ", role:" << operatorRoleToString(acc.role) << ")";
    emit loggedInChanged();
    return true;
}

void AuthService::logout()
{
    if (m_currentIndex < 0)
        return;
    const QString loggedOutId = m_operators.at(m_currentIndex).operatorId;
    m_currentIndex = -1;
    qCInfo(lcAuth) << "Operator logged out:" << loggedOutId;
    emit loggedInChanged();
}

QString AuthService::currentOperatorName() const
{
    return m_currentIndex >= 0 ? m_operators.at(m_currentIndex).displayName : QString();
}

QString AuthService::currentOperatorId() const
{
    return m_currentIndex >= 0 ? m_operators.at(m_currentIndex).operatorId : QString();
}

QString AuthService::currentRole() const
{
    return m_currentIndex >= 0 ? operatorRoleToString(m_operators.at(m_currentIndex).role) : QString();
}

int AuthService::lockRemainingSeconds() const
{
    if (!m_lockUntil.isValid())
        return 0;
    return static_cast<int>(QDateTime::currentDateTime().secsTo(m_lockUntil));
}

void AuthService::tickLock()
{
    if (QDateTime::currentDateTime() >= m_lockUntil) {
        m_lockUntil = QDateTime();
        m_lockTimer.stop();
        emit lockedChanged();
    }
    emit lockRemainingSecondsChanged();
}

#include <QQuickWindow>
#include <QImage>
#include <QThreadPool>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

QString AuthService::takeSnapshot(QQuickWindow *window)
{
    if (!window) {
        qWarning(lcAuth) << "takeSnapshot failed: window is null";
        return {};
    }

    const QString capturesDir = defaultCapturesPath();
    QDir(capturesDir).mkpath(QStringLiteral("."));

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    const QString filename = QStringLiteral("snapshot_%1.png").arg(timestamp);
    const QString fullPath = QDir(capturesDir).filePath(filename);

    const QImage img = window->grabWindow();
    if (img.isNull()) {
        qWarning(lcAuth) << "takeSnapshot failed: grabWindow returned null image";
        return {};
    }

    // 비동기 스레드 풀에서 디스크 쓰기 수행 (메인 GUI 스레드 멈칫 0.0초)
    QThreadPool::globalInstance()->start([img, fullPath]() {
        const bool ok = img.save(fullPath, "PNG");
        if (ok) {
            qInfo(lcAuth) << "Snapshot successfully saved to:" << fullPath;
        } else {
            qWarning(lcAuth) << "Failed to save snapshot to:" << fullPath;
        }
    });

    return filename;
}

void AuthService::openCapturesFolder()
{
    const QString path = defaultCapturesPath();
    QDir(path).mkpath(QStringLiteral("."));

#ifdef Q_OS_WIN
    // 윈도우 표준 explore 동사로 100% 확실하게 탐색기 폴더 창 팝업
    HINSTANCE res = ShellExecuteW(nullptr, L"explore", path.toStdWString().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(res) <= 32) {
        ShellExecuteW(nullptr, L"open", path.toStdWString().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
#endif
}

QString AuthService::defaultCapturesPath()
{
    const QString path = QStringLiteral("C:/VEDA_Final_project/captures");
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return QDir::toNativeSeparators(dir.absolutePath());
}


