#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVector>

#include "../models/OperatorAccount.h"

// Gates console access behind operator ID + PIN. Authenticates entirely
// against the local config/operators.json -- there is no backend, so this
// exists to keep an audit trail of who is at the console and to keep the
// demo/injection panel out of reach of anyone without a supervisor account,
// not to withstand a real attacker.
class AuthService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    Q_PROPERTY(QString currentOperatorName READ currentOperatorName NOTIFY loggedInChanged)
    Q_PROPERTY(QString currentOperatorId READ currentOperatorId NOTIFY loggedInChanged)
    Q_PROPERTY(QString currentRole READ currentRole NOTIFY loggedInChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(int lockRemainingSeconds READ lockRemainingSeconds NOTIFY lockRemainingSecondsChanged)
    Q_PROPERTY(bool hasOperators READ hasOperators CONSTANT)

public:
    // maxAttempts/lockSeconds are constructor-injected (not hardcoded) so
    // tests can use a short lockout instead of waiting on the real 30s.
    explicit AuthService(QVector<OperatorAccount> operators, QObject *parent = nullptr, int maxAttempts = 5,
                          int lockSeconds = 30);

    Q_INVOKABLE bool login(const QString &operatorId, const QString &pin);
    Q_INVOKABLE void logout();

    bool loggedIn() const { return m_currentIndex >= 0; }
    QString currentOperatorName() const;
    QString currentOperatorId() const;
    QString currentRole() const;
    bool locked() const { return m_lockUntil.isValid(); }
    int lockRemainingSeconds() const;
    bool hasOperators() const { return !m_operators.isEmpty(); }
    int operatorCount() const { return m_operators.size(); }

    // Salts with operatorId so two accounts sharing a PIN don't end up with
    // identical stored hashes. Public + static so config/operators.json
    // entries can be generated without a dedicated tool.
    static QString hashPin(const QString &operatorId, const QString &pin);

signals:
    void loggedInChanged();
    void loginFailed(const QString &reason);
    void lockedChanged();
    void lockRemainingSecondsChanged();

private:
    void tickLock();

    QVector<OperatorAccount> m_operators;
    int m_currentIndex = -1;
    int m_failedAttempts = 0;
    const int m_maxAttempts;
    const int m_lockSeconds;
    QDateTime m_lockUntil;
    QTimer m_lockTimer;
};
