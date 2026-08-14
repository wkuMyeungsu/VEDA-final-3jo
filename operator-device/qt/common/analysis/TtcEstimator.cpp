#include "TtcEstimator.h"

#include <algorithm> // - std::max, std::clamp

TtcEstimator::TtcEstimator(int windowSize)
    : m_windowSize(std::max(windowSize, kMinSampleCount)) // - 너무 작게 주면 최소 샘플 수 밑으로 못 내려가게 방어
{
}

void TtcEstimator::addSample(qint64 timestampMs, double bboxHeight)
{
    m_samples.append(Sample{timestampMs, bboxHeight}); // - 새 샘플을 뒤에 추가

    while (m_samples.size() > m_windowSize) // - 창 크기를 넘으면
        m_samples.removeFirst();            // - 제일 오래된 것부터 밀어냄 (슬라이딩 윈도우)
}

void TtcEstimator::reset()
{
    m_samples.clear(); // - 창 비우기: 처음 상태로 되돌림
}

TtcEstimator::Result TtcEstimator::estimate(qint64 nowMs) const
{
    Result result; // - 기본값: invalid / InsufficientSamples (아래에서 실제 사유로 덮어씀)

    if (m_samples.size() < kMinSampleCount) { // - 1) 최소 샘플 수 검사 (동등분할: 부족한 입력)
        result.status = Status::InsufficientSamples;
        return result;
    }

    for (const Sample &sample : m_samples) { // - 2) 높이 유효성 검사: 창 안에 0 이하 높이가 하나라도 있으면 전체 무효
        if (sample.bboxHeight <= 0.0) {       //   (검출 실패/오검출 프레임이 섞이면 회귀 자체가 의미 없음)
            result.status = Status::DegenerateBBox;
            return result;
        }
    }

    for (int i = 1; i < m_samples.size(); ++i) { // - 3) 시간 단조성 검사: 역행/중복 타임스탬프 방어 (0으로 나누기 예방)
        if (m_samples[i].timestampMs <= m_samples[i - 1].timestampMs) {
            result.status = Status::NonMonotonicTimestamp;
            return result;
        }
    }

    const qint64 newestTimestampMs = m_samples.last().timestampMs; // - 최신 샘플 시각
    if (nowMs - newestTimestampMs > kStaleThresholdMs) {           // - 4) 신선도 검사: 너무 오래된 데이터로 계산하지 않음
        result.status = Status::Stale;
        return result;
    }

    const RegressionFit fit = computeRegression(); // - 5) 최소제곱 회귀로 기울기/절편/적합도 계산
    if (!fit.ok) {                                  //   방어용: 이론상 3)을 통과하면 여기 안 걸림 (분산 0)
        result.status = Status::NonMonotonicTimestamp;
        return result;
    }

    result.confidence = std::clamp(fit.rSquared, 0.0, 1.0); // - R²를 신뢰도로 사용, 음수 R²(평균보다 못한 적합)는 0으로 잘라냄

    if (fit.slopePerSec <= 0.0) { // - 6) 멀어지거나 정지 (동등분할: receding/static) -- TTC가 의미 없음
        result.status = Status::Receding;
        return result;
    }

    // - 분자(s)도 회귀 직선이 예측한 "지금" 높이를 씀. 원본 최신 샘플값을 그대로 쓰면
    //   분자는 노이즈가 낀 값, 분모(기울기)는 노이즈가 걸러진 값이라 기준이 안 맞음.
    const double t0ToNewestSec = (newestTimestampMs - m_samples.first().timestampMs) / 1000.0; // - 회귀 기준(t=0)에서 최신 샘플까지 경과(초)
    double fittedNewestHeight = fit.interceptAtT0 + fit.slopePerSec * t0ToNewestSec;
    if (fittedNewestHeight <= 0.0)                          // - 잡음이 심해 회귀가 음수로 나오면(드묾) 원본 최신 높이로 대체
        fittedNewestHeight = m_samples.last().bboxHeight;

    const double rawTtcSeconds = fittedNewestHeight / fit.slopePerSec; // - looming TTC 공식: TTC ≈ s / (ds/dt)

    if (rawTtcSeconds > kMaxReportableTtcSeconds) { // - 7) 기울기가 너무 작아 TTC가 상한을 넘김: 발산 방지 (동등분할: 사실상 무한대)
        result.status = Status::RateTooSmall;
        result.ttcSeconds = kMaxReportableTtcSeconds; // - 숫자는 상한으로 클램프해서 돌려줌 (valid=false라 액션에는 못 씀)
        return result;
    }

    result.valid = true; // - 8) 여기까지 왔으면 정상 추정 (동등분할: approaching)
    result.status = Status::Valid;
    result.ttcSeconds = rawTtcSeconds;
    return result;
}

TtcEstimator::RegressionFit TtcEstimator::computeRegression() const
{
    RegressionFit fit;

    const qint64 t0Ms = m_samples.first().timestampMs; // - 창의 첫 샘플 시각을 0초 기준으로 삼음 (epoch ms를 그대로 제곱하면 정밀도 손실)
    const int n = m_samples.size();

    double sumT = 0.0; // - 시간(초) 합
    double sumS = 0.0; // - 높이 합
    for (const Sample &sample : m_samples) {
        sumT += (sample.timestampMs - t0Ms) / 1000.0;
        sumS += sample.bboxHeight;
    }
    const double meanT = sumT / n; // - 시간 평균(초)
    const double meanS = sumS / n; // - 높이 평균

    double sxy = 0.0; // - 공분산 합 (시간편차 * 높이편차)
    double sxx = 0.0; // - 시간 분산 합
    double sst = 0.0; // - 높이 분산 합 (SStot)
    for (const Sample &sample : m_samples) {
        const double t = (sample.timestampMs - t0Ms) / 1000.0 - meanT;
        const double s = sample.bboxHeight - meanS;
        sxy += t * s;
        sxx += t * t;
        sst += s * s;
    }

    if (sxx <= kMinVarianceEpsilon) // - 시간 분산이 0에 가까움 = 사실상 같은 시각뿐 (단조성 검사를 통과했으면 이론상 안 옴, 방어용)
        return fit;                 //   ok=false로 반환

    fit.slopePerSec = sxy / sxx;                        // - 최소제곱 기울기: Sxy / Sxx
    fit.interceptAtT0 = meanS - fit.slopePerSec * meanT; // - 절편: 회귀선이 평균점(meanT, meanS)을 지나도록 역산

    double ssRes = 0.0; // - 잔차 제곱합 (회귀선과 실제값의 차이, SSres)
    for (const Sample &sample : m_samples) {
        const double tRel = (sample.timestampMs - t0Ms) / 1000.0;
        const double predicted = fit.interceptAtT0 + fit.slopePerSec * tRel;
        const double residual = sample.bboxHeight - predicted;
        ssRes += residual * residual;
    }

    // R² = 1 - SSres/SStot : 회귀 직선이 실제 값의 분산을 얼마나 설명하는지를 나타내는
    // 결정계수. 1이면 점들이 직선 위에 완벽히 있다는 뜻(적합도 최고), 0이면 그냥
    // 평균값으로 예측하는 것과 다를 바 없다는 뜻. bbox가 흔들리면(jitter) 점들이
    // 직선에서 벗어나 SSres가 커지므로 R²가, 곧 confidence가 낮게 나옴.
    // SStot(sst)이 0에 가까우면(=높이가 거의 안 변함) 나누기 방지하고 0으로 둠 --
    // 다만 그런 경우 기울기도 거의 0이라 estimate()에서 어차피 Receding으로 먼저 걸러짐.
    fit.rSquared = (sst > kMinVarianceEpsilon) ? (1.0 - ssRes / sst) : 0.0;
    fit.ok = true;
    return fit;
}
