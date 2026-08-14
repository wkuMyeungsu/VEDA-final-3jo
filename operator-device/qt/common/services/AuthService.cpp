#include "AuthService.h"

#include <QCryptographicHash>
#include <QLoggingCategory>

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
