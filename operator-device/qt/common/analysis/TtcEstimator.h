#pragma once

// ============================================================================
// TtcEstimator -- 예측 실험(experiment)용 클래스입니다. 실제 안전 판정에 쓰지 않습니다.
//
// 무엇을 계산하는가
//   카메라에 잡힌 사람의 bbox 높이가 시간에 따라 커지는 속도(looming, 확대율)로
//   "몇 초 뒤에 부딪히나(TTC, Time-To-Contact)"를 추정합니다.
//   핀홀 카메라 모델에서는 bbox 크기가 거리에 반비례하므로, 정면으로 다가오는
//   대상은 bbox 높이 s와 그 변화율 ds/dt만으로
//       TTC ≈ s / (ds/dt)
//   를 구할 수 있습니다. 실제 거리(m), 카메라 캘리브레이션, 호모그래피가 전혀
//   필요 없습니다.
//
// 전제 (assumptions)
//   - 카메라가 고정돼 있고, 대상이 카메라를 향해 정면으로 다가온다고 가정합니다.
//   - bbox 높이가 거리에 반비례한다고 가정합니다 (핀홀 카메라 근사).
//   - 등속 접근을 보장할 순 없지만, 짧은 관측 구간에서는 속도가 거의 일정하다고
//     보고, 그 구간의 최소제곱 회귀 직선 기울기를 순간 변화율의 근사로 씁니다.
//
// 알려진 한계 (limitations)
//   - bbox가 흔들리면(검출기 jitter) 기울기 추정이 크게 흔들립니다. confidence로
//     어느 정도 걸러내지만 완전히 막지는 못합니다.
//   - 옆으로 스쳐 지나가는(lateral) 움직임에는 안 맞습니다. 옆으로 움직이면
//     높이가 거의 안 변해서 ds/dt가 0에 가까워지고, 실제로 위험해도 TTC가
//     무효로 나오거나 크게 과대평가됩니다.
//   - 스케일 보정이 없습니다. bbox 높이는 화면 대비 비율(0~1)일 뿐 실제 미터
//     단위가 아니라서, 카메라 화각/설치 높이가 다르면 같은 물리적 상황도 다른
//     곡선을 그립니다.
//   - 창(window) 안에서 곡선을 직선으로 근사하기 때문에, 실제 곡선(높이 ∝
//     1/거리)이 휘어 있는 정도에 따라 TTC를 구조적으로 과대추정하는 편향이
//     있습니다 (회귀는 창의 "중간 시점" 기울기를 대표값으로 주는데, 그 값을
//     "최신 시점" 높이에 적용하기 때문). tests/test_ttc_estimator.cpp의 정확도
//     테스트에서 이 편향을 실제 수치로 확인합니다.
//
// 안전 판정과의 관계
//   이 클래스는 그 무엇과도 연결돼 있지 않습니다. MetadataDistributor, 위험
//   등급(RiskLevel), 경고 장치(FPGA) 등 실제 안전 판정 경로에는 절대 연결하지
//   마세요. 순수 계산 클래스로만 두고, 필요하면 나중에 참고용 실험 화면에서만
//   읽어가는 방식으로 씁니다.
// ============================================================================

#include <QVector>
#include <QtGlobal>

// bbox 높이 변화율(looming) 기반 TTC 추정기.
// QVector/qint64 같은 Qt Core 값 타입만 쓰고 QObject/QML 의존성이 전혀 없어서
// GUI/이벤트 루프 없이 그대로 단위 테스트할 수 있음.
class TtcEstimator
{
public:
    // ---- 튜닝 가능한 상수 (근거는 옆 주석 참고) ----

    static constexpr int kDefaultWindowSize = 8; // - 회귀에 쓸 기본 샘플 개수: 서버 하트비트 200ms(kServerHeartbeatMs) 기준 최근 약 1.4초 구간

    static constexpr int kMinSampleCount = 3; // - 점이 2개면 직선이 항상 완벽히 지나가서(R²=1) 신뢰도가 허수로 나옴 -> 적합도가 의미 있으려면 최소 3개 필요

    static constexpr int kServerHeartbeatMs = 200; // - RiskEventSource.cpp의 kServerHeartbeatMs와 동일 값: 서버가 이 주기로 위험 판정을 내보내므로 bbox 샘플도 대략 이 주기로 들어옴

    static constexpr int kStaleSampleMultiplier = 3; // - RiskEventSource.cpp의 kWatchdogMultiplier와 같은 논리: 하트비트를 몇 박자 놓쳐야 "정말 오래됐다"로 볼지의 여유(지터/한두 번 유실은 봐줌)

    static constexpr qint64 kStaleThresholdMs = qint64(kServerHeartbeatMs) * kStaleSampleMultiplier; // - 최신 샘플이 이보다 오래되면 계산하지 않음 (200ms * 3 = 600ms)

    static constexpr double kMaxReportableTtcSeconds = 30.0; // - 기울기가 아주 작으면 TTC가 무한대로 발산하므로 두는 상한. 이보다 긴 TTC는 "당장 위협 아님"과 실질적으로 구분이 안 되고, 그 사이 새 샘플이 여러 번 들어와 추정치가 다시 계산될 것이므로 그대로 보고할 실익이 없음

    static constexpr double kMinVarianceEpsilon = 1e-9; // - 이 값 이하의 분산은 부동소수점 오차로 보고 0 나누기 방지용으로 취급 (초 단위 시간차 최소 1ms=0.001초 기준으로도 훨씬 여유 있는 값)

    // - 추정 결과가 유효하지 않을 때의 사유 (Valid면 무효 사유 없음)
    enum class Status {
        Valid,                 // - 정상 추정
        InsufficientSamples,   // - 창에 든 샘플 수가 kMinSampleCount 미만
        DegenerateBBox,        // - 창 안에 높이가 0 이하인 샘플이 하나라도 섞여 있음
        NonMonotonicTimestamp, // - 타임스탬프가 역행했거나 중복됨
        Stale,                 // - 최신 샘플이 기준 시각(nowMs)보다 너무 오래됨
        Receding,               // - 기울기(ds/dt)가 0 이하 (멀어지거나 정지 -- TTC가 의미 없음)
        RateTooSmall             // - 기울기가 0보다 크지만 너무 작아 TTC가 상한(kMaxReportableTtcSeconds)을 넘김
    };

    // - estimate() 호출 결과
    struct Result {
        bool valid = false;                          // - 아래 ttcSeconds를 그대로 믿어도 되는지
        double ttcSeconds = 0.0;                      // - 추정 TTC(초). valid=false면 참고용이거나 클램프된 값
        double confidence = 0.0;                      // - 회귀 적합도 기반 신뢰도 [0,1] (TtcEstimator.cpp의 computeRegression 주석 참고)
        Status status = Status::InsufficientSamples;  // - 무효 사유(또는 Valid)
    };

    explicit TtcEstimator(int windowSize = kDefaultWindowSize); // - windowSize: 회귀에 쓸 최근 샘플 개수 상한 (kMinSampleCount 미만으로는 못 내려감)

    void addSample(qint64 timestampMs, double bboxHeight); // - 샘플 1개 추가 (창이 꽉 차면 제일 오래된 샘플을 밀어냄)
    void reset();                                            // - 모든 샘플 비우기 (처음 상태로 되돌림)

    // - nowMs 시점 기준으로 TTC를 추정. 내부 상태를 바꾸지 않고 매번 새로 계산하므로
    //   같은 샘플에 대해 여러 번 불러도 결과는 같음 (nowMs만 다르면 Stale 판정만 달라짐).
    Result estimate(qint64 nowMs) const;

    int sampleCount() const { return m_samples.size(); } // - 테스트/디버그 편의용: 현재 창에 든 샘플 수

private:
    // - 추정에 쓰이는 원본 샘플 하나 (시각 + 정규화 bbox 높이)
    struct Sample {
        qint64 timestampMs = 0;  // - 샘플 시각(ms). 절대 기준은 호출자 마음대로(epoch ms 권장), 단조 증가만 하면 됨
        double bboxHeight = 0.0; // - 정규화 bbox 높이(0~1). BBox.h와 같은 정규화 좌표계를 그대로 사용
    };

    // - 최소제곱 선형회귀 결과. 시간축은 창의 가장 오래된 샘플을 0초로 놓고 상대
    //   초 단위로 씀 (epoch ms 원값을 그대로 제곱하면 double 정밀도가 깨짐).
    struct RegressionFit {
        bool ok = false;             // - false면 분산이 0에 가까워 계산 불가 (단조성 검사를 통과하면 이론상 안 일어남, 방어용)
        double slopePerSec = 0.0;    // - ds/dt 추정치 (초당 정규화 높이 변화량)
        double interceptAtT0 = 0.0;  // - t=0(창의 첫 샘플 시각)에서의 예측 높이
        double rSquared = 0.0;       // - 결정계수 R² (적합도)
    };

    RegressionFit computeRegression() const; // - 현재 창 전체로 최소제곱 회귀 계산

    QVector<Sample> m_samples; // - 슬라이딩 윈도우 (인덱스 0이 가장 오래된 샘플)
    int m_windowSize;          // - 창 최대 샘플 수 (kMinSampleCount 미만이면 생성자에서 올려 잡음)
};
