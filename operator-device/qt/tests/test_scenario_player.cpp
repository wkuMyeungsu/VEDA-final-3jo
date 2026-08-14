#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include "analysis/TtcEstimator.h"
#include "scenario/ScenarioPlayer.h"

// ScenarioPlayer 단위 테스트.
// test_ttc_estimator.cpp와 같은 방식으로 기법을 명시적으로 구분해서 씀:
//   1) 동등분할(equivalence partitioning)   -- 로드 실패 사유별 대표 입력 하나씩
//   2) 경계값 분석(boundary value analysis) -- t_ms 단조성의 "같음/역행" 경계, 키프레임 범위의 양끝
//   3) 상태전이(state transition)           -- 로드 실패 후 상태, start() 전 호출, 종료/루프
// 그리고 이 클래스의 존재 이유인 두 가지를 별도로 증명함:
//   - 결정성(determinism): 같은 파일을 두 번 재생하면 완전히 같은 이벤트 시퀀스가 나옴
//   - TtcEstimator 통합: MockMetadataSource와 달리 bbox 높이가 실제로 자라서 유효한 TTC가 나옴
class TestScenarioPlayer : public QObject
{
    Q_OBJECT

private slots:
    // 1) 동등분할
    void loadFailsForMissingFile();
    void loadFailsForMalformedJson();
    void loadFailsForEmptyKeyframes();
    void loadFailsForUnknownCameraId();
    void loadFailsForInvalidEnumValue();

    // 2) 경계값 분석
    void loadFailsForDuplicateTimestamp();
    void loadFailsForOutOfOrderTimestamp();
    void interpolationHoldsAtAndBeyondEdges();

    // 3) 상태전이
    void failedReloadClearsPreviousScenario();
    void startWithoutLoadedScenarioIsNoop();
    void finishedSignalFiresOnceAtScenarioEnd();
    void loopingScenarioNeverFinishes();
    void networkLossScenarioTogglesConnectionState();

    // 결정성
    void determinismSameFileProducesIdenticalSequence();

    // 보간 정확도
    void interpolationMidpointIsLinearAverage();

    // TtcEstimator 통합 (이 스위트의 핵심 -- Mock의 personH 상수 한계가 실제로 해소됐는지 증명)
    void approachHeadOnScenarioProducesValidTtc();
    void lateralPassScenarioDoesNotProduceValidTtc();
};

namespace {

QVector<CameraInfo> scenarioCameras()
{
    CameraInfo cam1;
    cam1.cameraId = QStringLiteral("CAM_01");
    cam1.zone = QStringLiteral("ZONE_A");
    CameraInfo cam2;
    cam2.cameraId = QStringLiteral("CAM_02");
    cam2.zone = QStringLiteral("ZONE_B");
    CameraInfo cam3;
    cam3.cameraId = QStringLiteral("CAM_03");
    cam3.zone = QStringLiteral("ZONE_C");
    return {cam1, cam2, cam3};
}

// - QByteArray JSON을 임시 디렉터리에 파일로 써서 경로를 돌려줌 (dir은 호출부가 스코프 안에서 살아있게 해야 함)
QString writeScenarioFile(QTemporaryDir &dir, const QByteArray &json, const QString &name = QStringLiteral("scenario.json"))
{
    const QString path = dir.filePath(name);
    QFile file(path);
    const bool opened = file.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    file.write(json);
    file.close();
    return path;
}

// - 실제로 저장소에 커밋된 시나리오 파일 경로 (tests/CMakeLists.txt가 SAFETY_SCENARIOS_DIR로 소스 경로를 넘겨줌)
QString shippedScenarioPath(const QString &fileName)
{
    return QStringLiteral(SAFETY_SCENARIOS_DIR "/") + fileName;
}

// - RiskMetadata는 operator==가 없어서 스크립트 가능한 필드 전부를 직접 비교
//   (RiskMetadata::toJson()은 bbox/distance_valid를 안 실어서 결정성 검증엔 못 씀)
void compareMetadataExactly(const RiskMetadata &a, const RiskMetadata &b, const char *context)
{
    QVERIFY2(a.cameraId() == b.cameraId(), context);
    QVERIFY2(a.zone() == b.zone(), context);
    QVERIFY2(a.riskLevel() == b.riskLevel(), context);
    QVERIFY2(a.exceptionState() == b.exceptionState(), context);
    QVERIFY2(a.distanceM() == b.distanceM(), context);
    QVERIFY2(a.distanceValid() == b.distanceValid(), context);
    QVERIFY2(a.personBBox() == b.personBBox(), context);
    QVERIFY2(a.forkliftBBox() == b.forkliftBBox(), context);
    QVERIFY2(a.utcTime() == b.utcTime(), context);
}

// - 씬 전체를 끝까지(여유 있게) 진행시키기 위한 틱 수. 정확한 배수가 아니어도 안전하게 남 씀.
int ticksToCoverDuration(const ScenarioPlayer &player, int tickIntervalMs)
{
    return static_cast<int>(player.durationMs() / tickIntervalMs) + 5;
}

} // namespace

// ==========================================================================
// 1) 동등분할: 로드 실패 사유마다 대표 입력 하나씩 -- "크래시 없이, 조용히 반쪽
//    재생하지 않고, 정확한 사유를 queryable하게 남긴다"를 매번 확인
// ==========================================================================

void TestScenarioPlayer::loadFailsForMissingFile()
{
    ScenarioPlayer player(scenarioCameras());
    const bool ok = player.loadScenario(QStringLiteral("C:/definitely/does/not/exist/scenario.json"));

    QVERIFY(!ok);
    QVERIFY(!player.isLoaded());
    QCOMPARE(int(player.lastError()), int(ScenarioPlayer::LoadError::FileNotFound));
    QVERIFY(!player.lastErrorMessage().isEmpty()); // - 사유가 사람이 읽을 수 있는 문자열로 남아야 함
}

void TestScenarioPlayer::loadFailsForMalformedJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeScenarioFile(dir, QByteArrayLiteral("{ this is not valid json"));

    ScenarioPlayer player(scenarioCameras());
    const bool ok = player.loadScenario(path);

    QVERIFY(!ok);
    QVERIFY(!player.isLoaded());
    QCOMPARE(int(player.lastError()), int(ScenarioPlayer::LoadError::MalformedJson));
}

void TestScenarioPlayer::loadFailsForEmptyKeyframes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeScenarioFile(dir, QByteArrayLiteral(R"({ "keyframes": [] })"));

    ScenarioPlayer player(scenarioCameras());
    const bool ok = player.loadScenario(path);

    QVERIFY(!ok);
    QCOMPARE(int(player.lastError()), int(ScenarioPlayer::LoadError::EmptyKeyframes));
}

void TestScenarioPlayer::loadFailsForUnknownCameraId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "keyframes": [
            { "t_ms": 0, "camera_id": "CAM_99", "risk_level": "SAFE" }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras()); // - CAM_99는 이 목록에 없음
    const bool ok = player.loadScenario(path);

    QVERIFY(!ok);
    QCOMPARE(int(player.lastError()), int(ScenarioPlayer::LoadError::UnknownCameraId));
}

void TestScenarioPlayer::loadFailsForInvalidEnumValue()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "keyframes": [
            { "t_ms": 0, "camera_id": "CAM_01", "risk_level": "EXTREME" }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras());
    const bool ok = player.loadScenario(path);

    QVERIFY(!ok);
    QCOMPARE(int(player.lastError()), int(ScenarioPlayer::LoadError::InvalidEnumValue));
}

// ==========================================================================
// 2) 경계값 분석
// ==========================================================================

// t_ms 단조성 검사의 "경계"는 "이전 값과 같음(==)" -- 이 경우 OutOfOrder가 아니라
// 별도 사유(DuplicateTimestamp)로 구분돼야 함을 확인
void TestScenarioPlayer::loadFailsForDuplicateTimestamp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "keyframes": [
            { "t_ms": 1000, "camera_id": "CAM_01", "risk_level": "SAFE" },
            { "t_ms": 1000, "camera_id": "CAM_01", "risk_level": "CAUTION" }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras());
    QVERIFY(!player.loadScenario(path));
    QCOMPARE(int(player.lastError()), int(ScenarioPlayer::LoadError::DuplicateTimestamp));
}

// 경계 바로 반대편(역행, <): 별도 사유(OutOfOrderTimestamp)여야 함
void TestScenarioPlayer::loadFailsForOutOfOrderTimestamp()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "keyframes": [
            { "t_ms": 1000, "camera_id": "CAM_01", "risk_level": "SAFE" },
            { "t_ms": 500,  "camera_id": "CAM_01", "risk_level": "CAUTION" }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras());
    QVERIFY(!player.loadScenario(path));
    QCOMPARE(int(player.lastError()), int(ScenarioPlayer::LoadError::OutOfOrderTimestamp));
}

// 키프레임 범위의 양끝(첫 키프레임 이전 / 마지막 키프레임 이후)은 보간하지 않고
// 값을 고정(hold)해야 함 -- 범위 밖으로 나가서 외삽(extrapolate)하면 안 됨
void TestScenarioPlayer::interpolationHoldsAtAndBeyondEdges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "keyframes": [
            { "t_ms": 1000, "camera_id": "CAM_01", "risk_level": "SAFE", "distance_m": 10.0 },
            { "t_ms": 2000, "camera_id": "CAM_01", "risk_level": "SAFE", "distance_m": 6.0 }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras());
    QVERIFY(player.loadScenario(path));

    // - 첫 키프레임(t_ms=1000) 이전 시각을 물어도 첫 값으로 고정
    QCOMPARE(player.sampleForTesting(QStringLiteral("CAM_01"), 0).distanceM(), 10.0);
    // - 정확히 첫 키프레임 시각
    QCOMPARE(player.sampleForTesting(QStringLiteral("CAM_01"), 1000).distanceM(), 10.0);
    // - 정확히 마지막 키프레임 시각
    QCOMPARE(player.sampleForTesting(QStringLiteral("CAM_01"), 2000).distanceM(), 6.0);
    // - 마지막 키프레임 이후 시각을 물어도 마지막 값으로 고정 (외삽 금지)
    QCOMPARE(player.sampleForTesting(QStringLiteral("CAM_01"), 5000).distanceM(), 6.0);
}

// ==========================================================================
// 3) 상태전이
// ==========================================================================

// 정상 로드 -> 이어서 실패하는 로드 -> isLoaded()가 false로 떨어지고 이전 데이터가
// 남아있지 않아야 함 ("절반만 로드된 시나리오를 조용히 재생하지 않는다"의 핵심 증명)
void TestScenarioPlayer::failedReloadClearsPreviousScenario()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray validJson = R"({
        "keyframes": [ { "t_ms": 0, "camera_id": "CAM_01", "risk_level": "SAFE" } ]
    })";
    const QString validPath = writeScenarioFile(dir, validJson, QStringLiteral("valid.json"));

    ScenarioPlayer player(scenarioCameras());
    QVERIFY(player.loadScenario(validPath));
    QVERIFY(player.isLoaded());

    const QString malformedPath = writeScenarioFile(dir, QByteArrayLiteral("not json"), QStringLiteral("bad.json"));
    QVERIFY(!player.loadScenario(malformedPath));
    QVERIFY(!player.isLoaded()); // - 이전 로드 성공 상태가 남아있으면 안 됨

    QSignalSpy spy(&player, &ScenarioPlayer::metadataReceived);
    player.start(); // - 로드 실패 후 start()가 이전 시나리오를 몰래 재생하면 안 됨
    QCOMPARE(spy.count(), 0);
}

void TestScenarioPlayer::startWithoutLoadedScenarioIsNoop()
{
    ScenarioPlayer player(scenarioCameras()); // - loadScenario()를 아예 안 부름
    QSignalSpy spy(&player, &ScenarioPlayer::metadataReceived);

    player.start(); // - 크래시하면 안 됨, 아무 것도 emit하면 안 됨
    QCOMPARE(spy.count(), 0);
}

void TestScenarioPlayer::finishedSignalFiresOnceAtScenarioEnd()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "loop": false,
        "keyframes": [
            { "t_ms": 0,   "camera_id": "CAM_01", "risk_level": "SAFE" },
            { "t_ms": 400, "camera_id": "CAM_01", "risk_level": "SAFE" }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras(), 200); // - 200ms 틱: 0,200,400 세 번이면 끝
    QVERIFY(player.loadScenario(path));

    QSignalSpy finishedSpy(&player, &ScenarioPlayer::finished);
    player.start();               // - t=0 emit
    QVERIFY(!player.isFinished());
    player.advanceForTesting(1);  // - t=200 emit
    QVERIFY(!player.isFinished());
    player.advanceForTesting(1);  // - t=400 emit -> 시계가 duration을 넘어서면서 종료 처리

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(player.isFinished());

    // - 이미 끝난 뒤에 더 진행시켜도 추가 이벤트/추가 finished가 없어야 함(중복 방지)
    QSignalSpy metaSpy(&player, &ScenarioPlayer::metadataReceived);
    player.advanceForTesting(3);
    QCOMPARE(metaSpy.count(), 0);
    QCOMPARE(finishedSpy.count(), 1);
}

void TestScenarioPlayer::loopingScenarioNeverFinishes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "loop": true,
        "keyframes": [
            { "t_ms": 0,   "camera_id": "CAM_01", "risk_level": "SAFE", "distance_m": 10.0 },
            { "t_ms": 400, "camera_id": "CAM_01", "risk_level": "SAFE", "distance_m": 6.0 }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras(), 200);
    QVERIFY(player.loadScenario(path));
    QVERIFY(player.looping()); // - JSON의 loop:true가 그대로 반영돼야 함

    QSignalSpy finishedSpy(&player, &ScenarioPlayer::finished);
    player.start();
    player.advanceForTesting(20); // - duration(400ms)의 몇 배를 지나도 안 끝나야 함

    QCOMPARE(finishedSpy.count(), 0);
    QVERIFY(!player.isFinished());
}

// network_loss.json이 실제로 상단 연결 배지(connectionStateChanged)와 카메라별
// exception_state 둘 다를 함께 바꾸는지 확인 -- IMetadataSource 전역 상태 + 카메라별
// RiskMetadata라는 서로 다른 두 경로가 한 시나리오 파일로 같이 움직임을 증명
void TestScenarioPlayer::networkLossScenarioTogglesConnectionState()
{
    ScenarioPlayer player(scenarioCameras());
    QVERIFY(player.loadScenario(shippedScenarioPath(QStringLiteral("network_loss.json"))));

    QSignalSpy metaSpy(&player, &ScenarioPlayer::metadataReceived);

    player.start(); // - Connected로 시작(초기 Disconnected -> Connected 전환이 여기서 한 번 emit됨)

    // - start()의 최초 Connected 전환은 일부러 스펴 생성 전에 흘려보냄 -- 여기서부턴
    //   시나리오가 만드는 전환(Disconnected@4000, Connected@7200) 두 개만 잡음
    QSignalSpy connectionSpy(&player, &ScenarioPlayer::connectionStateChanged);
    player.advanceForTesting(ticksToCoverDuration(player, ScenarioPlayer::kDefaultTickIntervalMs));

    QCOMPARE(connectionSpy.count(), 2);
    QCOMPARE(int(connectionSpy.at(0).at(0).value<RiskTypes::ConnectionState>()), int(RiskTypes::ConnectionState::Disconnected));
    QCOMPARE(int(connectionSpy.at(1).at(0).value<RiskTypes::ConnectionState>()), int(RiskTypes::ConnectionState::Connected));

    // - 끊긴 구간 동안 CAM_02 이벤트 중 NETWORK_DISCONNECTED 예외가 실제로 섞여 나왔는지
    bool sawNetworkDisconnected = false;
    for (int i = 0; i < metaSpy.count(); ++i) {
        const RiskMetadata meta = metaSpy.at(i).at(0).value<RiskMetadata>();
        if (meta.exceptionState() == RiskTypes::ExceptionState::NetworkDisconnected)
            sawNetworkDisconnected = true;
    }
    QVERIFY(sawNetworkDisconnected);
}

// ==========================================================================
// 결정성(determinism): 이 클래스가 존재하는 이유 그 자체.
// 서로 다른 두 ScenarioPlayer 인스턴스(별도 프로세스 재생을 흉내)가 같은 파일을
// 재생하면, utc_time을 포함해 emit되는 모든 값이 완전히 똑같아야 함.
// ==========================================================================
void TestScenarioPlayer::determinismSameFileProducesIdenticalSequence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "keyframes": [
            { "t_ms": 0,    "camera_id": "CAM_01", "risk_level": "SAFE",    "distance_m": 10.0, "person_bbox": { "x": 0.40, "y": 0.40, "w": 0.10, "h": 0.10 } },
            { "t_ms": 1000, "camera_id": "CAM_02", "risk_level": "CAUTION", "distance_m": 3.0,  "person_bbox": { "x": 0.30, "y": 0.30, "w": 0.15, "h": 0.15 } },
            { "t_ms": 2000, "camera_id": "CAM_01", "risk_level": "DANGER",  "distance_m": 0.5,  "person_bbox": { "x": 0.20, "y": 0.20, "w": 0.30, "h": 0.30 } }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);
    const QDateTime fixedBase = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00.000Z"), Qt::ISODateWithMs);

    ScenarioPlayer playerA(scenarioCameras());
    ScenarioPlayer playerB(scenarioCameras());
    QVERIFY(playerA.loadScenario(path));
    QVERIFY(playerB.loadScenario(path));
    playerA.setBaseUtcTimeForTesting(fixedBase);
    playerB.setBaseUtcTimeForTesting(fixedBase);

    QSignalSpy spyA(&playerA, &ScenarioPlayer::metadataReceived);
    QSignalSpy spyB(&playerB, &ScenarioPlayer::metadataReceived);

    playerA.start();
    playerA.advanceForTesting(ticksToCoverDuration(playerA, ScenarioPlayer::kDefaultTickIntervalMs));
    playerB.start();
    playerB.advanceForTesting(ticksToCoverDuration(playerB, ScenarioPlayer::kDefaultTickIntervalMs));

    QVERIFY(spyA.count() > 0);
    QCOMPARE(spyA.count(), spyB.count()); // - 이벤트 "개수"부터 완전히 같아야 함

    for (int i = 0; i < spyA.count(); ++i) {
        const RiskMetadata a = spyA.at(i).at(0).value<RiskMetadata>();
        const RiskMetadata b = spyB.at(i).at(0).value<RiskMetadata>();
        compareMetadataExactly(a, b, qPrintable(QStringLiteral("event #%1").arg(i)));
    }
}

// ==========================================================================
// 보간 정확도: 두 키프레임 사이 중간 시각을 물으면 정확히 산술 평균이어야 함
// ==========================================================================
void TestScenarioPlayer::interpolationMidpointIsLinearAverage()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray json = R"({
        "keyframes": [
            { "t_ms": 0,    "camera_id": "CAM_01", "risk_level": "SAFE", "distance_m": 10.0, "person_bbox": { "x": 0.20, "y": 0.30, "w": 0.10, "h": 0.10 } },
            { "t_ms": 1000, "camera_id": "CAM_01", "risk_level": "SAFE", "distance_m": 6.0,  "person_bbox": { "x": 0.40, "y": 0.50, "w": 0.20, "h": 0.30 } }
        ]
    })";
    const QString path = writeScenarioFile(dir, json);

    ScenarioPlayer player(scenarioCameras());
    QVERIFY(player.loadScenario(path));

    const RiskMetadata mid = player.sampleForTesting(QStringLiteral("CAM_01"), 500); // - 정확히 중간(t=500)
    QVERIFY(qFuzzyCompare(mid.distanceM(), 8.0));               // - (10.0+6.0)/2
    QVERIFY(qFuzzyCompare(mid.personBBox().x(), 0.30));         // - (0.20+0.40)/2
    QVERIFY(qFuzzyCompare(mid.personBBox().y(), 0.40));         // - (0.30+0.50)/2
    QVERIFY(qFuzzyCompare(mid.personBBox().width(), 0.15));     // - (0.10+0.20)/2
    QVERIFY(qFuzzyCompare(mid.personBBox().height(), 0.20));    // - (0.10+0.30)/2
}

// ==========================================================================
// TtcEstimator 통합 -- 이 스위트의 존재 이유.
// MockMetadataSource.cpp의 personH는 상수(0.30)라서 ds/dt가 항상 0이고, TTC를
// 절대 낼 수 없었음. approach_head_on.json은 실제로 bbox 높이가 자라므로, 그
// 궤적을 그대로 TtcEstimator에 먹이면 Valid한 TTC가 나와야 함 -- 그게 이 테스트임.
// ==========================================================================
void TestScenarioPlayer::approachHeadOnScenarioProducesValidTtc()
{
    ScenarioPlayer player(scenarioCameras());
    QVERIFY(player.loadScenario(shippedScenarioPath(QStringLiteral("approach_head_on.json"))));

    QSignalSpy spy(&player, &ScenarioPlayer::metadataReceived);
    player.start();
    player.advanceForTesting(ticksToCoverDuration(player, ScenarioPlayer::kDefaultTickIntervalMs));
    QVERIFY(player.isFinished());
    QVERIFY(spy.count() >= TtcEstimator::kDefaultWindowSize); // - 추정기 창을 채우기 충분한 샘플 수여야 함

    // - TtcExperiment.cpp와 완전히 같은 방식으로 먹임: personBBox가 유효한 샘플만,
    //   시각은 utcTime(ms epoch), 순서는 emit된 그대로(=시나리오 시간순)
    TtcEstimator estimator;
    qint64 lastTimestampMs = 0;
    for (int i = 0; i < spy.count(); ++i) {
        const RiskMetadata meta = spy.at(i).at(0).value<RiskMetadata>();
        if (meta.cameraId() != QStringLiteral("CAM_01") || !meta.personBBox().isValid())
            continue;
        lastTimestampMs = meta.utcTime().toMSecsSinceEpoch();
        estimator.addSample(lastTimestampMs, meta.personBBox().height());
    }

    const TtcEstimator::Result result = estimator.estimate(lastTimestampMs);

    QCOMPARE(int(result.status), int(TtcEstimator::Status::Valid));
    QVERIFY(result.valid); // - Mock 경로로는 절대 나올 수 없던 상태 -- personH가 상수가 아니라서 가능해짐
    QVERIFY(result.ttcSeconds > 0.0);
}

// 음성(negative) 사례: 옆으로 스쳐 지나가는 움직임은 bbox 높이가 안 변하므로
// (ds/dt<=0) TtcEstimator가 Receding으로 무효 판정해야 함 -- TtcEstimator.h에
// 문서화된 "lateral 움직임에는 안 맞는다"는 한계를 실제로 보여줌
void TestScenarioPlayer::lateralPassScenarioDoesNotProduceValidTtc()
{
    ScenarioPlayer player(scenarioCameras());
    QVERIFY(player.loadScenario(shippedScenarioPath(QStringLiteral("lateral_pass.json"))));

    QSignalSpy spy(&player, &ScenarioPlayer::metadataReceived);
    player.start();
    player.advanceForTesting(ticksToCoverDuration(player, ScenarioPlayer::kDefaultTickIntervalMs));
    QVERIFY(spy.count() >= TtcEstimator::kDefaultWindowSize);

    TtcEstimator estimator;
    qint64 lastTimestampMs = 0;
    for (int i = 0; i < spy.count(); ++i) {
        const RiskMetadata meta = spy.at(i).at(0).value<RiskMetadata>();
        if (meta.cameraId() != QStringLiteral("CAM_03") || !meta.personBBox().isValid())
            continue;
        lastTimestampMs = meta.utcTime().toMSecsSinceEpoch();
        estimator.addSample(lastTimestampMs, meta.personBBox().height());
    }

    const TtcEstimator::Result result = estimator.estimate(lastTimestampMs);

    QVERIFY(!result.valid);
    QCOMPARE(int(result.status), int(TtcEstimator::Status::Receding)); // - 높이가 안 변함(ds/dt<=0) -> 접근도 후진도 아닌 "무의미"로 분류
}

QTEST_MAIN(TestScenarioPlayer)
#include "test_scenario_player.moc"
