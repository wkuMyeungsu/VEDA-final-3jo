#include <QtTest>

#include "analysis/TtcEstimator.h"

// TtcEstimator 단위 테스트.
// 세 가지 테스트 기법을 명시적으로 구분해서 씀 (그룹마다 위에 한글 주석으로 표시):
//   1) 동등분할(equivalence partitioning)   -- 입력을 몇 가지 대표 클래스로 나눠 각각 하나씩만 확인
//   2) 경계값 분석(boundary value analysis) -- 판정이 뒤집히는 경계선 바로 위/아래/정확히를 확인
//   3) 상태전이(state transition)           -- 호출 순서에 따라 상태가 어떻게 바뀌는지 확인
// 마지막으로 물리적으로 정답을 아는 시나리오를 만들어 정확도를, 흔들리는 입력으로
// confidence(신뢰도) 공식을 각각 검증함.
class TestTtcEstimator : public QObject
{
    Q_OBJECT

private slots:
    // 1) 동등분할
    void equivalencePartitions_data();
    void equivalencePartitions();

    // 2) 경계값 분석
    void minSampleCountBoundary_data();
    void minSampleCountBoundary();
    void staleThresholdBoundary_data();
    void staleThresholdBoundary();
    void rateClampBoundary_data();
    void rateClampBoundary();

    // 3) 상태전이
    void resetClearsValidState();
    void approachThenRecedeInvalidates();
    void windowSlideClearsDegenerateSample();

    // 정확도 + 신뢰도(confidence) 공식 검증
    void accuracyConstantVelocityApproach();
    void confidenceReflectsFitQuality();
};

namespace {
constexpr qint64 kBaseMs = 1'000'000;                      // - 테스트 공용 임의의 시작 시각(ms). 절대값은 의미 없고 간격만 중요
constexpr int kStepMs = TtcEstimator::kServerHeartbeatMs;  // - 실제 서버 하트비트 주기(200ms)와 맞춰서 씀

// - heights를 kStepMs 간격으로 순서대로 넣어 추정기를 만드는 공용 헬퍼
TtcEstimator makeEstimator(const QVector<double> &heights, int windowSize = TtcEstimator::kDefaultWindowSize)
{
    TtcEstimator estimator(windowSize);
    for (int i = 0; i < heights.size(); ++i)
        estimator.addSample(kBaseMs + i * qint64(kStepMs), heights[i]);
    return estimator;
}

// - heights의 마지막 샘플 시각 기준으로 offsetMs만큼 지난 시각 (신선도 계산용 "지금")
qint64 nowAfterLastSample(int sampleCount, qint64 offsetMs)
{
    return kBaseMs + (sampleCount - 1) * qint64(kStepMs) + offsetMs;
}

constexpr double kAccuracyToleranceRatio = 0.10; // - 정확도 테스트 허용 오차(참값 대비 10%)
// 왜 0(완벽)이 아니라 10%인가: 이 알고리즘은 창(window) 안의 곡선을 직선으로 근사함.
// 실제 물리는 bbox 높이가 거리에 반비례(비선형)라서, 창이 대표하는 "평균 기울기"는
// 창의 중간 시점 근처의 순간 기울기에 가깝고, 그걸 "최신 시점" 높이에 적용하면 구조적으로
// TTC를 과대추정하게 됨(TtcEstimator.h 상단 "알려진 한계" 참고). 아래 accuracyConstantVelocityApproach()의
// 시나리오(초기 거리 40m, 속도 2m/s)로 직접 재 보면 오차가 약 7%라서, 여유를 두고 10%로 잡음.
}

// ==========================================================================
// 1) 동등분할 (Equivalence Partitioning)
// 입력을 "접근/멀어짐/정지/훼손된 bbox" 네 클래스로 나눔. 클래스마다 대표값
// 하나씩만 확인해도 그 클래스 전체를 대표한다고 보는 것이 동등분할의 핵심.
// ==========================================================================
void TestTtcEstimator::equivalencePartitions_data()
{
    QTest::addColumn<int>("pattern");         // - 0=접근 1=멀어짐 2=정지 3=훼손(bbox 높이<=0 섞임)
    QTest::addColumn<int>("expectedStatus");
    QTest::addColumn<bool>("expectValid");

    QTest::newRow("접근(approaching) -- 정상 추정돼야 함")      << 0 << int(TtcEstimator::Status::Valid) << true;
    QTest::newRow("멀어짐(receding) -- ds/dt<=0, 무의미해야 함") << 1 << int(TtcEstimator::Status::Receding) << false;
    QTest::newRow("정지(static) -- ds/dt==0, 역시 무의미")      << 2 << int(TtcEstimator::Status::Receding) << false;
    QTest::newRow("훼손된 bbox(degenerate) -- 높이<=0 섞임")    << 3 << int(TtcEstimator::Status::DegenerateBBox) << false;
}

void TestTtcEstimator::equivalencePartitions()
{
    QFETCH(int, pattern);
    QFETCH(int, expectedStatus);
    QFETCH(bool, expectValid);

    QVector<double> heights;
    switch (pattern) {
    case 0: heights = {0.10, 0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24}; break; // - 접근: 꾸준히 커짐
    case 1: heights = {0.24, 0.22, 0.20, 0.18, 0.16, 0.14, 0.12, 0.10}; break; // - 멀어짐: 꾸준히 작아짐
    case 2: heights = {0.15, 0.15, 0.15, 0.15, 0.15, 0.15, 0.15, 0.15}; break; // - 정지: 안 변함
    case 3: heights = {0.10, 0.12, 0.14, 0.0,  0.18, 0.20, 0.22, 0.24}; break; // - 훼손: 중간에 검출 실패(높이 0)
    }

    TtcEstimator estimator = makeEstimator(heights);
    const TtcEstimator::Result result = estimator.estimate(nowAfterLastSample(heights.size(), 50));

    QCOMPARE(int(result.status), expectedStatus);
    QCOMPARE(result.valid, expectValid);
}

// ==========================================================================
// 2) 경계값 분석 (Boundary Value Analysis)
// ==========================================================================

// 최소 샘플 수(kMinSampleCount) 경계: 하나 모자라면 계산 자체를 시작하면 안 되고,
// 정확히 채워지면 계산이 시작돼야 함.
void TestTtcEstimator::minSampleCountBoundary_data()
{
    QTest::addColumn<int>("sampleCount");
    QTest::addColumn<int>("expectedStatus");

    QTest::newRow("최소치-1 (경계 미만) -- 샘플 부족")   << (TtcEstimator::kMinSampleCount - 1) << int(TtcEstimator::Status::InsufficientSamples);
    QTest::newRow("최소치 정확히 (경계)  -- 계산 시작돼야 함") << TtcEstimator::kMinSampleCount       << int(TtcEstimator::Status::Valid);
}

void TestTtcEstimator::minSampleCountBoundary()
{
    QFETCH(int, sampleCount);
    QFETCH(int, expectedStatus);

    QVector<double> heights;
    for (int i = 0; i < sampleCount; ++i)
        heights.append(0.10 + 0.02 * i); // - 항상 접근(증가) 패턴이라 샘플 수만 경계를 넘나드는 변수가 됨

    TtcEstimator estimator = makeEstimator(heights);
    const TtcEstimator::Result result = estimator.estimate(nowAfterLastSample(qMax(sampleCount, 1), 50));

    QCOMPARE(int(result.status), expectedStatus);
}

// 신선도(kStaleThresholdMs=600ms) 경계: 정확히 600ms까지는 써도 되고, 601ms부터는
// 버려야 함. RiskEventSource.cpp의 워치독과 같은 논리(TtcEstimator.h 주석 참고)를
// 직접 숫자로 확인.
void TestTtcEstimator::staleThresholdBoundary_data()
{
    QTest::addColumn<qint64>("nowOffsetMs");
    QTest::addColumn<int>("expectedStatus");

    QTest::newRow("임계값-1ms (경계 미만) -- 아직 신선함")   << (TtcEstimator::kStaleThresholdMs - 1) << int(TtcEstimator::Status::Valid);
    QTest::newRow("임계값 정확히 (경계)   -- 아직 신선함")   << TtcEstimator::kStaleThresholdMs        << int(TtcEstimator::Status::Valid);
    QTest::newRow("임계값+1ms (경계 초과) -- 이제 오래됨")   << (TtcEstimator::kStaleThresholdMs + 1) << int(TtcEstimator::Status::Stale);
}

void TestTtcEstimator::staleThresholdBoundary()
{
    QFETCH(qint64, nowOffsetMs);
    QFETCH(int, expectedStatus);

    const QVector<double> heights = {0.10, 0.12, 0.14}; // - 딱 최소 샘플 수만큼, 깨끗한 접근 데이터
    TtcEstimator estimator = makeEstimator(heights);

    const TtcEstimator::Result result = estimator.estimate(nowAfterLastSample(heights.size(), nowOffsetMs));

    QCOMPARE(int(result.status), expectedStatus);
}

// ds/dt 클램프 경계: TTC = s/(ds/dt)가 정확히 kMaxReportableTtcSeconds가 되는
// 기울기를 기준(m0)으로 잡고, 그보다 살짝 가파르면(TTC<상한) 유효, 살짝 완만하면
// (TTC>상한) 무효+클램프여야 함.
//
// "정확히 경계(1.00)" 행에 얽힌 실측 주의점: m0을 수학적으로 역산해서 넣어도, 실제
// 계산은 평균/분산을 여러 단계 거치는 최소제곱 회귀라 반올림 오차가 누적됨. 그래서
// "이론상 정확히 30.0"이어야 할 TTC가 이 빌드에서는 30.000000000000014초로, 아주
// 살짝(약 1e-14초) 상한을 넘겨서 Valid 대신 RateTooSmall + 클램프값으로 나옴 --
// 컴파일러/최적화 수준이 바뀌면 반대쪽(딱 30.0 이하)으로 나올 수도 있음. 즉 이
// 지점에서는 Valid/RateTooSmall 둘 다 "정답"이라, 상태 대신 "값이 상한에 붙어
// 있는지"만 확인함. 진짜 상태 판정 검증은 명확히 위/아래로 벗어난 나머지 두 행이 함.
void TestTtcEstimator::rateClampBoundary_data()
{
    QTest::addColumn<double>("slopeMultiplier"); // - 경계 기울기(m0) 대비 배율
    QTest::addColumn<bool>("exactlyAtBoundary");  // - true면 부동소수점 반올림 때문에 상태가 어느 쪽으로 나와도 정답인 경우

    QTest::newRow("경계 미만(기울기 살짝 완만 -> TTC>상한)") << 0.99 << false;
    QTest::newRow("경계 정확히(TTC==상한, 이론상)")           << 1.00 << true;
    QTest::newRow("경계 초과(기울기 살짝 가파름 -> TTC<상한)") << 1.01 << false;
}

void TestTtcEstimator::rateClampBoundary()
{
    QFETCH(double, slopeMultiplier);
    QFETCH(bool, exactlyAtBoundary);

    constexpr double kAnchorHeight = 0.5; // - "지금"(마지막 샘플) 시점의 bbox 높이 기준점 (값 자체는 임의)
    const double m0 = kAnchorHeight / TtcEstimator::kMaxReportableTtcSeconds; // - TTC가 정확히 상한이 되는 기울기
    const double slope = m0 * slopeMultiplier;
    const double stepSec = kStepMs / 1000.0;

    // - 3개 샘플이 s(t) = s0 + slope*t 위에 정확히 놓이도록 구성 (마지막 샘플이 kAnchorHeight)
    TtcEstimator estimator(TtcEstimator::kMinSampleCount);
    for (int i = 0; i < TtcEstimator::kMinSampleCount; ++i) {
        const double tRelSec = (i - (TtcEstimator::kMinSampleCount - 1)) * stepSec; // - 마지막 샘플 기준 상대 시각(<=0)
        estimator.addSample(kBaseMs + i * qint64(kStepMs), kAnchorHeight + slope * tRelSec);
    }

    const TtcEstimator::Result result = estimator.estimate(nowAfterLastSample(TtcEstimator::kMinSampleCount, 50));
    constexpr double kFpTolerance = 1e-6; // - 평균/분산을 거치는 회귀 계산에서 생기는 부동소수점 오차 허용치

    if (exactlyAtBoundary) {
        // - 위 주석 참고: 상태는 Valid/RateTooSmall 둘 다 허용, 값만 상한 근처인지 확인
        QVERIFY(int(result.status) == int(TtcEstimator::Status::Valid)
                || int(result.status) == int(TtcEstimator::Status::RateTooSmall));
        QVERIFY(qAbs(result.ttcSeconds - TtcEstimator::kMaxReportableTtcSeconds) < kFpTolerance);
    } else if (slopeMultiplier < 1.0) {
        QCOMPARE(int(result.status), int(TtcEstimator::Status::RateTooSmall));
        QVERIFY(!result.valid);
        QCOMPARE(result.ttcSeconds, TtcEstimator::kMaxReportableTtcSeconds); // - 무효여도 값은 상한으로 클램프돼 있어야 함 (상수 대입이라 오차 없이 일치)
    } else {
        QCOMPARE(int(result.status), int(TtcEstimator::Status::Valid));
        QVERIFY(result.valid);
        QVERIFY(qAbs(result.ttcSeconds - kAnchorHeight / slope) < kFpTolerance); // - 유효하면 TTC == s/(ds/dt) 그대로여야 함
    }
}

// ==========================================================================
// 3) 상태전이 (State Transition)
// ==========================================================================

// Valid 상태 -> reset() -> 처음 상태(샘플 없음)로 돌아가는지
void TestTtcEstimator::resetClearsValidState()
{
    const QVector<double> heights = {0.10, 0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24};
    TtcEstimator estimator = makeEstimator(heights);

    const TtcEstimator::Result before = estimator.estimate(nowAfterLastSample(heights.size(), 50));
    QCOMPARE(int(before.status), int(TtcEstimator::Status::Valid)); // - reset 전: 정상 추정 상태여야 함

    estimator.reset();
    QCOMPARE(estimator.sampleCount(), 0); // - reset 직후: 샘플이 하나도 없어야 함

    const TtcEstimator::Result after = estimator.estimate(nowAfterLastSample(heights.size(), 50));
    QCOMPARE(int(after.status), int(TtcEstimator::Status::InsufficientSamples)); // - reset 후: 처음 상태와 같은 사유로 무효
    QVERIFY(!after.valid);
}

// 접근 중이던 대상이 방향을 바꿔 멀어지기 시작하면, 창이 새 데이터로 다 채워진
// 뒤에는 판정이 Valid에서 Receding으로 뒤집혀야 함.
void TestTtcEstimator::approachThenRecedeInvalidates()
{
    TtcEstimator estimator(TtcEstimator::kDefaultWindowSize); // - 창 크기 8

    for (int i = 0; i < 8; ++i)                                          // - 1단계: 접근 데이터로 창을 가득 채움
        estimator.addSample(kBaseMs + i * qint64(kStepMs), 0.10 + 0.02 * i);
    const TtcEstimator::Result approaching = estimator.estimate(nowAfterLastSample(8, 50));
    QCOMPARE(int(approaching.status), int(TtcEstimator::Status::Valid)); // - 아직은 접근 중 -> 유효

    const qint64 recedeStartMs = kBaseMs + 8 * qint64(kStepMs);
    for (int i = 0; i < 8; ++i)                                          // - 2단계: 창 크기만큼 멀어지는 데이터를 더 넣어 기존 접근 데이터를 전부 밀어냄
        estimator.addSample(recedeStartMs + i * qint64(kStepMs), 0.26 - 0.02 * i);
    const qint64 nowAfterRecede = recedeStartMs + 7 * qint64(kStepMs) + 50;
    const TtcEstimator::Result receding = estimator.estimate(nowAfterRecede);

    QCOMPARE(int(receding.status), int(TtcEstimator::Status::Receding)); // - 창이 멀어지는 데이터로 다 채워진 뒤엔 무효로 뒤집혀야 함
    QVERIFY(!receding.valid);
}

// 창(window) 안에 훼손된 샘플이 하나 섞여 있으면 무효 -> 그 뒤로 정상 샘플을
// 창 크기만큼 더 넣어서 훼손된 샘플이 밀려 나가면 다시 유효로 돌아와야 함.
// 정확히 몇 번째 추가에서 뒤집히는지까지 확인해서 슬라이딩 윈도우가 FIFO로
// 동작한다는 것을 직접 증명함.
void TestTtcEstimator::windowSlideClearsDegenerateSample()
{
    TtcEstimator estimator(TtcEstimator::kDefaultWindowSize); // - 창 크기 8

    // - 초기 8개: 인덱스 3(4번째)이 훼손된 샘플(높이 0)
    const QVector<double> initialHeights = {0.10, 0.12, 0.14, 0.0, 0.18, 0.20, 0.22, 0.24};
    for (int i = 0; i < initialHeights.size(); ++i)
        estimator.addSample(kBaseMs + i * qint64(kStepMs), initialHeights[i]);

    TtcEstimator::Result result = estimator.estimate(nowAfterLastSample(initialHeights.size(), 50));
    QCOMPARE(int(result.status), int(TtcEstimator::Status::DegenerateBBox)); // - 훼손된 샘플이 창 안에 있는 동안은 계속 무효

    const qint64 extraStartMs = kBaseMs + initialHeights.size() * qint64(kStepMs);
    const QVector<double> extraHeights = {0.26, 0.28, 0.30, 0.32}; // - 계속 접근하는 정상 데이터

    for (int i = 0; i < 3; ++i) {                                  // - 3개까지는 훼손 샘플(원래 인덱스3)이 아직 창 안에 남아 있음
        estimator.addSample(extraStartMs + i * qint64(kStepMs), extraHeights[i]);
        result = estimator.estimate(extraStartMs + i * qint64(kStepMs) + 50);
        QCOMPARE(int(result.status), int(TtcEstimator::Status::DegenerateBBox));
    }

    estimator.addSample(extraStartMs + 3 * qint64(kStepMs), extraHeights[3]); // - 4번째를 넣는 순간 훼손 샘플이 창에서 밀려남 (FIFO)
    result = estimator.estimate(extraStartMs + 3 * qint64(kStepMs) + 50);

    QCOMPARE(int(result.status), int(TtcEstimator::Status::Valid)); // - 이제 창엔 정상 데이터만 남아 유효로 뒤집힘
    QVERIFY(result.valid);
}

// ==========================================================================
// 정확도 테스트: 정답을 알 수 있는 물리 시나리오
// ==========================================================================

// 카메라를 향해 등속(v)으로 다가오는 물체의 거리 d(t) = d0 - v*t.
// 핀홀 카메라 근사에서 bbox 높이는 거리에 반비례하므로 s(t) = k/d(t).
// 등속이므로 "지금" 시점의 실제 TTC = 남은 거리 / 속도 로 정확히 알 수 있음
// (이 값이 곧 정답/ground truth). 추정기는 창 안의 곡선을 직선으로 근사하므로
// 정확히 일치하진 않지만, kAccuracyToleranceRatio 안에는 들어와야 함.
void TestTtcEstimator::accuracyConstantVelocityApproach()
{
    constexpr double kInitialDistanceM = 40.0; // - "지금"(마지막 샘플) 시점의 남은 거리(m). kMaxReportableTtcSeconds(30s)보다 작은 TTC가 나오도록 고름
    constexpr double kClosingSpeedMps = 2.0;   // - 등속 접근 속도(m/s), 사람 빠른 걸음 정도
    constexpr double kScaleConstant = 1.0;     // - 카메라 화각/초점 상수. TTC = s/(ds/dt) 비율에는 영향 없음(분자/분모에서 상쇄됨)

    const int sampleCount = TtcEstimator::kDefaultWindowSize; // - 기본 창 크기(8) 그대로 사용
    const double stepSec = kStepMs / 1000.0;

    TtcEstimator estimator;
    for (int i = 0; i < sampleCount; ++i) {
        const double tRelToNowSec = (i - (sampleCount - 1)) * stepSec;        // - "지금"(마지막 샘플) 기준 상대 시각, 과거는 음수
        const double distanceM = kInitialDistanceM - kClosingSpeedMps * tRelToNowSec; // - d(t) = d0 - v*t
        estimator.addSample(kBaseMs + i * qint64(kStepMs), kScaleConstant / distanceM);
    }

    const qint64 nowMs = nowAfterLastSample(sampleCount, 0); // - 마지막 샘플과 같은 시각을 "지금"으로 씀 (신선도 여유 확인은 다른 테스트에서 이미 함)
    const double groundTruthTtcSeconds = kInitialDistanceM / kClosingSpeedMps; // - 등속이므로 정답은 나눗셈 한 번으로 정확히 나옴

    const TtcEstimator::Result result = estimator.estimate(nowMs);

    QCOMPARE(int(result.status), int(TtcEstimator::Status::Valid));
    QVERIFY(result.valid);
    QVERIFY(result.confidence > 0.99); // - 곡률이 완만한 구간이라 직선 근사가 잘 맞음 -> 신뢰도도 높게 나와야 함

    const double allowedErrorSeconds = groundTruthTtcSeconds * kAccuracyToleranceRatio;
    QVERIFY2(qAbs(result.ttcSeconds - groundTruthTtcSeconds) <= allowedErrorSeconds,
             qPrintable(QStringLiteral("ttcEst=%1 ttcTrue=%2 tolerance=%3")
                            .arg(result.ttcSeconds)
                            .arg(groundTruthTtcSeconds)
                            .arg(allowedErrorSeconds)));
}

// confidence 공식(R² 기반) 자체를 검증: 평균적인 기울기는 같아도, 점들이 직선에서
// 많이 벗어나 있으면(jitter) confidence가 낮게 나와야 함.
void TestTtcEstimator::confidenceReflectsFitQuality()
{
    const QVector<double> cleanHeights = {0.10, 0.12, 0.14, 0.16, 0.18, 0.20, 0.22, 0.24};   // - 완벽한 직선
    const QVector<double> jitteryHeights = {0.10, 0.14, 0.11, 0.17, 0.13, 0.21, 0.16, 0.24}; // - 같은 시작/끝, 들쭉날쭉

    TtcEstimator clean = makeEstimator(cleanHeights);
    TtcEstimator jittery = makeEstimator(jitteryHeights);

    const TtcEstimator::Result cleanResult = clean.estimate(nowAfterLastSample(cleanHeights.size(), 50));
    const TtcEstimator::Result jitteryResult = jittery.estimate(nowAfterLastSample(jitteryHeights.size(), 50));

    QCOMPARE(int(cleanResult.status), int(TtcEstimator::Status::Valid));
    QCOMPARE(int(jitteryResult.status), int(TtcEstimator::Status::Valid)); // - 들쭉날쭉해도 평균 추세는 여전히 접근이라 유효는 유효

    QVERIFY(cleanResult.confidence > 0.95);            // - 완벽한 직선 -> R²≈1
    QVERIFY(jitteryResult.confidence < 0.80);           // - 흔들리는 데이터 -> R²가 뚜렷하게 낮아야 함
    QVERIFY(jitteryResult.confidence < cleanResult.confidence);
}

QTEST_MAIN(TestTtcEstimator)
#include "test_ttc_estimator.moc"
