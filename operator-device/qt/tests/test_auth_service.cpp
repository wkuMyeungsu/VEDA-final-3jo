#include <QtTest>

#include "models/OperatorAccount.h"
#include "services/AuthService.h"

class TestAuthService : public QObject
{
    Q_OBJECT

private slots:
    void successfulOperatorLogin();
    void successfulSupervisorLogin();
    void failedLoginWrongPin();
    void failedLoginUnknownId();
    void lockoutAfterMaxAttempts();
    void lockedRejectsAttempts();
    void lockoutExpires();
    void logoutResetsState();
    void pinHashingWithSalt();
    void emptyOperatorsList();
};

namespace {
QVector<OperatorAccount> makeSampleOperators()
{
    OperatorAccount op;
    op.operatorId = QStringLiteral("OP01");
    op.displayName = QStringLiteral("김조작");
    op.role = OperatorRole::Operator;
    op.pinHash = AuthService::hashPin(QStringLiteral("OP01"), QStringLiteral("1234"));

    OperatorAccount sv;
    sv.operatorId = QStringLiteral("hanwha");
    sv.displayName = QStringLiteral("한화비전 관리자");
    sv.role = OperatorRole::Supervisor;
    sv.pinHash = AuthService::hashPin(QStringLiteral("hanwha"), QStringLiteral("5hanwha!"));

    return {op, sv};
}
}

void TestAuthService::successfulOperatorLogin()
{
    AuthService auth(makeSampleOperators());
    QSignalSpy loggedInSpy(&auth, &AuthService::loggedInChanged);

    QVERIFY(auth.hasOperators());
    QCOMPARE(auth.operatorCount(), 2);
    QVERIFY(!auth.loggedIn());

    const bool ok = auth.login(QStringLiteral("OP01"), QStringLiteral("1234"));
    QVERIFY(ok);
    QVERIFY(auth.loggedIn());
    QCOMPARE(auth.currentOperatorId(), QStringLiteral("OP01"));
    QCOMPARE(auth.currentOperatorName(), QStringLiteral("김조작"));
    QCOMPARE(auth.currentRole(), QStringLiteral("operator"));
    QCOMPARE(loggedInSpy.count(), 1);
}

void TestAuthService::successfulSupervisorLogin()
{
    AuthService auth(makeSampleOperators());
    const bool ok = auth.login(QStringLiteral("hanwha"), QStringLiteral("5hanwha!"));
    QVERIFY(ok);
    QVERIFY(auth.loggedIn());
    QCOMPARE(auth.currentOperatorId(), QStringLiteral("hanwha"));
    QCOMPARE(auth.currentOperatorName(), QStringLiteral("한화비전 관리자"));
    QCOMPARE(auth.currentRole(), QStringLiteral("supervisor"));
}

void TestAuthService::failedLoginWrongPin()
{
    AuthService auth(makeSampleOperators());
    QSignalSpy failedSpy(&auth, &AuthService::loginFailed);

    const bool ok = auth.login(QStringLiteral("OP01"), QStringLiteral("0000"));
    QVERIFY(!ok);
    QVERIFY(!auth.loggedIn());
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.at(0).at(0).toString(), QStringLiteral("아이디 또는 PIN이 올바르지 않습니다"));
}

void TestAuthService::failedLoginUnknownId()
{
    AuthService auth(makeSampleOperators());
    QSignalSpy failedSpy(&auth, &AuthService::loginFailed);

    const bool ok = auth.login(QStringLiteral("UNKNOWN"), QStringLiteral("1234"));
    QVERIFY(!ok);
    QVERIFY(!auth.loggedIn());
    QCOMPARE(failedSpy.count(), 1);
    // 보안을 위해 ID 부재와 PIN 오답 메시지를 동일하게 유지하는지 확인
    QCOMPARE(failedSpy.at(0).at(0).toString(), QStringLiteral("아이디 또는 PIN이 올바르지 않습니다"));
}

void TestAuthService::lockoutAfterMaxAttempts()
{
    // maxAttempts: 3, lockSeconds: 2
    AuthService auth(makeSampleOperators(), nullptr, 3, 2);
    QSignalSpy lockedSpy(&auth, &AuthService::lockedChanged);
    QSignalSpy failedSpy(&auth, &AuthService::loginFailed);

    QVERIFY(!auth.locked());
    QCOMPARE(auth.lockRemainingSeconds(), 0);

    // 1차 실패
    QVERIFY(!auth.login(QStringLiteral("OP01"), QStringLiteral("wrong1")));
    QVERIFY(!auth.locked());

    // 2차 실패
    QVERIFY(!auth.login(QStringLiteral("OP01"), QStringLiteral("wrong2")));
    QVERIFY(!auth.locked());

    // 3차 실패 -> 잠금 발생
    QVERIFY(!auth.login(QStringLiteral("OP01"), QStringLiteral("wrong3")));
    QVERIFY(auth.locked());
    QCOMPARE(lockedSpy.count(), 1);
    QVERIFY(auth.lockRemainingSeconds() > 0);
    QCOMPARE(failedSpy.last().at(0).toString(), QStringLiteral("실패 횟수 초과 -- 2초간 잠깁니다"));
}

void TestAuthService::lockedRejectsAttempts()
{
    AuthService auth(makeSampleOperators(), nullptr, 2, 2);
    auth.login(QStringLiteral("OP01"), QStringLiteral("wrong1"));
    auth.login(QStringLiteral("OP01"), QStringLiteral("wrong2")); // 잠김

    QVERIFY(auth.locked());

    QSignalSpy failedSpy(&auth, &AuthService::loginFailed);
    // 잠금 상태에서는 올바른 PIN이라도 즉시 거부
    const bool ok = auth.login(QStringLiteral("OP01"), QStringLiteral("1234"));
    QVERIFY(!ok);
    QVERIFY(!auth.loggedIn());
    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(failedSpy.at(0).at(0).toString().contains(QStringLiteral("잠금 상태")));
}

void TestAuthService::lockoutExpires()
{
    // 1초 잠금
    AuthService auth(makeSampleOperators(), nullptr, 1, 1);
    auth.login(QStringLiteral("OP01"), QStringLiteral("wrong"));
    QVERIFY(auth.locked());

    // 1.5초 대기 후 잠금 해제 확인
    QTest::qWait(1500);

    QVERIFY(!auth.locked());
    QCOMPARE(auth.lockRemainingSeconds(), 0);

    // 잠금 해제 후 정상 로그인
    const bool ok = auth.login(QStringLiteral("OP01"), QStringLiteral("1234"));
    QVERIFY(ok);
    QVERIFY(auth.loggedIn());
}

void TestAuthService::logoutResetsState()
{
    AuthService auth(makeSampleOperators());
    auth.login(QStringLiteral("OP01"), QStringLiteral("1234"));
    QVERIFY(auth.loggedIn());

    QSignalSpy loggedInSpy(&auth, &AuthService::loggedInChanged);
    auth.logout();

    QVERIFY(!auth.loggedIn());
    QVERIFY(auth.currentOperatorId().isEmpty());
    QVERIFY(auth.currentOperatorName().isEmpty());
    QVERIFY(auth.currentRole().isEmpty());
    QCOMPARE(loggedInSpy.count(), 1);
}

void TestAuthService::pinHashingWithSalt()
{
    // 동일한 PIN("9999")이어도 operatorId가 다르면 해시가 달라야 함 (Salt = operatorId)
    const QString hashA = AuthService::hashPin(QStringLiteral("USER_A"), QStringLiteral("9999"));
    const QString hashB = AuthService::hashPin(QStringLiteral("USER_B"), QStringLiteral("9999"));

    QVERIFY(!hashA.isEmpty());
    QVERIFY(!hashB.isEmpty());
    QVERIFY(hashA != hashB);

    // 동일 ID와 PIN이면 항상 결정적으로 동일한 해시 반환
    const QString hashA2 = AuthService::hashPin(QStringLiteral("USER_A"), QStringLiteral("9999"));
    QCOMPARE(hashA, hashA2);
}

void TestAuthService::emptyOperatorsList()
{
    AuthService auth({});
    QVERIFY(!auth.hasOperators());
    QCOMPARE(auth.operatorCount(), 0);
    QVERIFY(!auth.login(QStringLiteral("OP01"), QStringLiteral("1234")));
}

QTEST_MAIN(TestAuthService)
#include "test_auth_service.moc"
