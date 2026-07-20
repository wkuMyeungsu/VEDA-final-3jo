#include "MockMetadataSource.h"

#include <QDateTime>
#include <QRandomGenerator>

namespace {
constexpr int kTickIntervalMs = 1200;
}

MockMetadataSource::MockMetadataSource(QVector<CameraInfo> cameras, QObject *parent)
    : IMetadataSource(parent)
{
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
    tick();
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

void MockMetadataSource::tick()
{
    for (auto it = m_states.begin(); it != m_states.end(); ++it) {
        if (!it->hasAnyOverride() && !m_autoPlay)
            continue;
        emit metadataReceived(generateForCamera(it.key(), it.value()));
    }
}

void MockMetadataSource::emitCameraUpdate(const QString &cameraId)
{
    auto it = m_states.find(cameraId);
    if (it == m_states.end())
        return;
    emit metadataReceived(generateForCamera(cameraId, it.value()));
}

QPointF MockMetadataSource::wanderPosition(QPointF current)
{
    QRandomGenerator *rng = QRandomGenerator::global();
    const double dx = (rng->generateDouble() - 0.5) * 0.04;
    const double dy = (rng->generateDouble() - 0.5) * 0.04;
    const double x = qBound(0.05, current.x() + dx, 0.80);
    const double y = qBound(0.05, current.y() + dy, 0.80);
    return QPointF(x, y);
}

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
        level = state.overrideLevel;
        distance = state.overrideDistanceM;
        exception = state.overrideException;
    } else {
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
