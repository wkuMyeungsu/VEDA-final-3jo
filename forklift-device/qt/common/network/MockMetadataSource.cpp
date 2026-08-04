#include "MockMetadataSource.h"

#include <QDateTime>
#include <QRandomGenerator>

namespace {
constexpr int kTickIntervalMs = 1200;
}

MockMetadataSource::MockMetadataSource(QVector<CameraInfo> cameras, QObject *parent)
    : IMetadataSource(parent)
{
    // 카메라 목록으로 초기 상태 맵 구성 (zone만 미리 채워두고 나머진 기본값)
    for (const CameraInfo &info : cameras) {
        CameraState state;
        state.zone = info.zone;
        m_states.insert(info.cameraId, state);
    }

    m_timer.setInterval(kTickIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &MockMetadataSource::tick);
}

void MockMetadataSource::start()
{
    setConnectionState(RiskTypes::ConnectionState::Connected);
    m_timer.start();
    tick(); // 첫 tick까지 안 기다리고 초기 이벤트를 바로 내보냄
}

void MockMetadataSource::stop()
{
    m_timer.stop();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void MockMetadataSource::setAutoPlay(bool enabled)
{
    m_autoPlay = enabled;
}

void MockMetadataSource::setRiskOverride(const QString &cameraId, RiskTypes::RiskLevel level, double distanceM,
                                          RiskTypes::ExceptionState exception)
{
    auto it = m_states.find(cameraId);
    if (it == m_states.end())
        return;

    it->overrideActive = true;
    it->overrideLevel = level;
    it->overrideDistanceM = distanceM;
    it->overrideException = exception;
    emitCameraUpdate(cameraId);
}

void MockMetadataSource::clearRiskOverride(const QString &cameraId)
{
    auto it = m_states.find(cameraId);
    if (it == m_states.end())
        return;

    it->overrideActive = false;
    if (m_autoPlay)
        emitCameraUpdate(cameraId);
}

void MockMetadataSource::setPersonBBoxOverride(const QString &cameraId, const BBox &box)
{
    auto it = m_states.find(cameraId);
    if (it == m_states.end())
        return;

    it->personBBoxOverride = box;
    emitCameraUpdate(cameraId);
}

void MockMetadataSource::setForkliftBBoxOverride(const QString &cameraId, const BBox &box)
{
    auto it = m_states.find(cameraId);
    if (it == m_states.end())
        return;

    it->forkliftBBoxOverride = box;
    emitCameraUpdate(cameraId);
}

void MockMetadataSource::clearBBoxOverrides(const QString &cameraId)
{
    auto it = m_states.find(cameraId);
    if (it == m_states.end())
        return;

    it->personBBoxOverride.reset();
    it->forkliftBBoxOverride.reset();
    emitCameraUpdate(cameraId);
}

// 1.2초(kTickIntervalMs)마다 전체 카메라를 순회
// - autoPlay가 꺼져있고 override도 없는 카메라는 건너뜀
//   (수동 시나리오 진행 중엔 안 건드린 카메라가 멋대로 안 바뀌게)
void MockMetadataSource::tick()
{
    for (auto it = m_states.begin(); it != m_states.end(); ++it) {
        if (!it->hasAnyOverride() && !m_autoPlay)
            continue;
        emit metadataReceived(generateForCamera(it.key(), it.value()));
    }
}

// override 값을 막 바꾼 직후 호출 -- 다음 tick(최대 1.2초 후)까지 안 기다리고
// 즉시 이벤트 1건을 내보내서 QML이 바로 반응하게 함
void MockMetadataSource::emitCameraUpdate(const QString &cameraId)
{
    auto it = m_states.find(cameraId);
    if (it == m_states.end())
        return;
    emit metadataReceived(generateForCamera(cameraId, it.value()));
}

// 이전 위치(current)에서 사방으로 최대 ±0.02(정규화 좌표) 랜덤 이동
// qBound로 0.05~0.80 범위에 가둬서 화면 밖으로 안 나가게 함
QPointF MockMetadataSource::wanderPosition(QPointF current)
{
    QRandomGenerator *rng = QRandomGenerator::global();
    const double dx = (rng->generateDouble() - 0.5) * 0.04;
    const double dy = (rng->generateDouble() - 0.5) * 0.04;
    const double x = qBound(0.05, current.x() + dx, 0.80);
    const double y = qBound(0.05, current.y() + dy, 0.80);
    return QPointF(x, y);
}

// 카메라 1대의 RiskMetadata 이벤트 1건을 조립 (override 우선, 없으면 랜덤 생성)
RiskMetadata MockMetadataSource::generateForCamera(const QString &cameraId, CameraState &state) const
{
    RiskMetadata meta;
    meta.setCameraId(cameraId);
    meta.setZone(state.zone);
    meta.setUtcTime(QDateTime::currentDateTimeUtc());

    RiskTypes::RiskLevel level;
    double distance;
    RiskTypes::ExceptionState exception;
    QRandomGenerator *rng = QRandomGenerator::global();

    if (state.overrideActive) {
        // 데모 시나리오로 강제 고정된 값 그대로 사용
        level = state.overrideLevel;
        distance = state.overrideDistanceM;
        exception = state.overrideException;
    } else {
        // 위험도: SAFE 70% / CAUTION 15% / DANGER 10% / EMERGENCY 5% 가중치
        // 위험도 높을수록 거리 범위도 가깝게 좁힘
        const int roll = rng->bounded(100);
        if (roll < 70) {
            level = RiskTypes::RiskLevel::Safe;
            distance = 3.0 + rng->generateDouble() * 5.0;
        } else if (roll < 85) {
            level = RiskTypes::RiskLevel::Caution;
            distance = 1.5 + rng->generateDouble() * 1.5;
        } else if (roll < 95) {
            level = RiskTypes::RiskLevel::Danger;
            distance = 0.5 + rng->generateDouble() * 1.0;
        } else {
            level = RiskTypes::RiskLevel::Emergency;
            distance = rng->generateDouble() * 0.5;
        }

        // 3% 확률로 예외 상태도 하나 얹음 (5종 중 랜덤)
        // UnconfirmedProximity는 목록에 없어 자동 생성 안 됨 -- 수동 지정 필요
        exception = RiskTypes::ExceptionState::None;
        if (rng->bounded(100) < 3) {
            switch (rng->bounded(5)) {
            case 0: exception = RiskTypes::ExceptionState::SensorFault; break;
            case 1: exception = RiskTypes::ExceptionState::DeadReckoning; break;
            case 2: exception = RiskTypes::ExceptionState::EmergencyImpact; break;
            case 3: exception = RiskTypes::ExceptionState::NetworkDisconnected; break;
            default: exception = RiskTypes::ExceptionState::CameraDisconnected; break;
            }
        }
    }

    meta.setRiskLevel(level);
    meta.setDistanceM(distance);
    meta.setExceptionState(exception);

    // bbox는 override로 지정됐거나 실제 위험 단계일 때만 그림
    const bool showDetections = state.personBBoxOverride.has_value() || state.forkliftBBoxOverride.has_value()
        || level != RiskTypes::RiskLevel::Safe;

    if (showDetections) {
        if (state.personCenter.isNull())
            state.personCenter = QPointF(0.30 + rng->generateDouble() * 0.40, 0.30 + rng->generateDouble() * 0.40);
        if (state.forkliftCenter.isNull())
            state.forkliftCenter = QPointF(0.30 + rng->generateDouble() * 0.40, 0.30 + rng->generateDouble() * 0.40);

        state.personCenter = wanderPosition(state.personCenter);
        state.forkliftCenter = wanderPosition(state.forkliftCenter);

        constexpr double personW = 0.12, personH = 0.30, forkliftW = 0.24, forkliftH = 0.22;
        const BBox personBox = state.personBBoxOverride.value_or(BBox(state.personCenter.x() - personW / 2,
                                                                        state.personCenter.y() - personH / 2,
                                                                        personW, personH));
        const BBox forkliftBox = state.forkliftBBoxOverride.value_or(BBox(state.forkliftCenter.x() - forkliftW / 2,
                                                                            state.forkliftCenter.y() - forkliftH / 2,
                                                                            forkliftW, forkliftH));

        meta.setPersonBBox(personBox);
        meta.setForkliftBBox(forkliftBox);
    }

    return meta;
}
