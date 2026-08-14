#include "TtcExperiment.h"

#include <QDateTime>

#include "../models/RiskMetadata.h"

namespace {

// TtcEstimator::Status -> 화면에 보여줄 짧은 한글 사유 (DetectionOverlay가 그대로 씀)
QString statusToReason(TtcEstimator::Status status)
{
    switch (status) {
    case TtcEstimator::Status::Valid: return QString(); // - 유효할 땐 reason 대신 숫자를 보여주므로 안 씀
    case TtcEstimator::Status::InsufficientSamples: return QStringLiteral("표본 부족");
    case TtcEstimator::Status::DegenerateBBox: return QStringLiteral("박스 이상");
    case TtcEstimator::Status::NonMonotonicTimestamp: return QStringLiteral("시간 이상");
    case TtcEstimator::Status::Stale: return QStringLiteral("측정 지연");
    case TtcEstimator::Status::Receding: return QStringLiteral("정지·후진");
    case TtcEstimator::Status::RateTooSmall: return QStringLiteral("변화 미미");
    }
    return QStringLiteral("알 수 없음"); // - 방어용: 위 case가 전부 처리하므로 이론상 안 옴
}

} // namespace

TtcExperiment::TtcExperiment(QObject *parent)
    : QObject(parent)
{
}

TtcResult TtcExperiment::resultFor(const QString &cameraId) const
{
    return m_results.value(cameraId); // - 없으면 TtcResult{} 기본값(valid=false) 반환
}

// RiskMetadata 1건이 들어올 때마다 호출됨 -- personBBox 높이 + utcTime을 해당
// 카메라 추정기에 넣고 즉시 재추정. metadata 자체는 절대 안 건드림(읽기만 함)
void TtcExperiment::handleMetadata(const RiskMetadata &metadata)
{
    const QString cameraId = metadata.cameraId();
    if (cameraId.isEmpty()) // - 카메라 ID 없는 이벤트(방어용, 정상 경로에선 안 옴)는 무시
        return;

    TtcEstimator &estimator = m_estimators[cameraId]; // - 없으면 QHash가 기본 생성자로 새로 만듦

    const BBox box = metadata.personBBox();
    if (box.isValid()) {
        // utcTime 출처: RiskMetadata.utcTime() (RiskEventSource.cpp가 서버 payload의
        // utc_time을 파싱해 채움, MockMetadataSource는 생성 시점의 로컬 UTC 벽시계).
        // 단조 시계(monotonic clock)가 아니라 시스템 시계가 튈 수 있지만, 관측 가능한
        // 시각 정보 중 가장 신뢰할 수 있는 소스라 그대로 씀. utc_time 파싱 실패(무효)
        // 시에는 RiskEventSource.cpp와 같은 방식으로 수신 시각을 대체값으로 씀
        const qint64 timestampMs = metadata.utcTime().isValid()
            ? metadata.utcTime().toMSecsSinceEpoch()
            : QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();
        estimator.addSample(timestampMs, box.height());
    } else {
        // 사람이 화면에서 사라짐 -- 창을 비워서 재등장 시 예전 궤적과 안 섞이게 함
        estimator.reset();
    }

    const qint64 nowMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch(); // - 신선도 판정 기준: 실제 "지금"
    const TtcEstimator::Result estimate = estimator.estimate(nowMs);

    TtcResult &cached = m_results[cameraId];
    cached.setValid(estimate.valid);
    cached.setTtcSeconds(estimate.ttcSeconds);
    cached.setConfidence(estimate.confidence);
    cached.setReason(statusToReason(estimate.status));

    emit resultUpdated(cameraId);
}
