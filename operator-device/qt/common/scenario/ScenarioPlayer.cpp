#include "ScenarioPlayer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(lcScenario, "safety.scenario")

// - 시나리오 JSON 전용 bbox 파싱. RiskMetadata의 서버 payload 포맷(x,y,width,height)과
//   달리 여기서는 x,y,w,h로 짧게 씀 -- 파일이 사람 손으로 자주 편집되는 스크립트라 간결함이 더 중요함
BBox bboxFromScenarioJson(const QJsonObject &obj)
{
    return BBox(obj.value(QStringLiteral("x")).toDouble(0.0), obj.value(QStringLiteral("y")).toDouble(0.0),
                obj.value(QStringLiteral("w")).toDouble(0.0), obj.value(QStringLiteral("h")).toDouble(0.0));
}

// - risk_level/exception_state/connection state 문자열을 엄격하게 해석함.
//   RiskTypes::exceptionStateFromString() 등 기존 헬퍼는 모르는 값을 조용히 기본값(None)으로
//   눌러버려서 오타를 못 잡아냄 -- 시나리오 파일은 실수를 크게 티내야 하므로 별도로 둠(안 맞으면 nullopt).
std::optional<RiskTypes::RiskLevel> parseRiskLevel(const QString &value)
{
    if (value == QStringLiteral("SAFE")) return RiskTypes::RiskLevel::Safe;
    if (value == QStringLiteral("CAUTION")) return RiskTypes::RiskLevel::Caution;
    if (value == QStringLiteral("DANGER")) return RiskTypes::RiskLevel::Danger;
    if (value == QStringLiteral("EMERGENCY")) return RiskTypes::RiskLevel::Emergency;
    return std::nullopt;
}

std::optional<RiskTypes::ExceptionState> parseExceptionState(const QString &value)
{
    if (value == QStringLiteral("NONE")) return RiskTypes::ExceptionState::None;
    if (value == QStringLiteral("SENSOR_FAULT")) return RiskTypes::ExceptionState::SensorFault;
    if (value == QStringLiteral("DEAD_RECKONING")) return RiskTypes::ExceptionState::DeadReckoning;
    if (value == QStringLiteral("EMERGENCY_IMPACT")) return RiskTypes::ExceptionState::EmergencyImpact;
    if (value == QStringLiteral("NETWORK_DISCONNECTED")) return RiskTypes::ExceptionState::NetworkDisconnected;
    if (value == QStringLiteral("CAMERA_DISCONNECTED")) return RiskTypes::ExceptionState::CameraDisconnected;
    if (value == QStringLiteral("UNCONFIRMED_PROXIMITY")) return RiskTypes::ExceptionState::UnconfirmedProximity;
    return std::nullopt;
}

std::optional<RiskTypes::ConnectionState> parseConnectionState(const QString &value)
{
    if (value == QStringLiteral("DISCONNECTED")) return RiskTypes::ConnectionState::Disconnected;
    if (value == QStringLiteral("CONNECTING")) return RiskTypes::ConnectionState::Connecting;
    if (value == QStringLiteral("CONNECTED")) return RiskTypes::ConnectionState::Connected;
    return std::nullopt;
}

// - a와 b 사이 선형보간 (frac: 0=a, 1=b)
double lerp(double a, double b, double frac)
{
    return a + (b - a) * frac;
}

BBox lerpBBox(const BBox &a, const BBox &b, double frac)
{
    return BBox(lerp(a.x(), b.x(), frac), lerp(a.y(), b.y(), frac), lerp(a.width(), b.width(), frac),
                lerp(a.height(), b.height(), frac));
}

} // namespace

ScenarioPlayer::ScenarioPlayer(QVector<CameraInfo> cameras, int tickIntervalMs, QObject *parent)
    : IMetadataSource(parent)
    , m_tickIntervalMs(tickIntervalMs)
{
    for (const CameraInfo &info : cameras) {
        m_knownCameraIds.insert(info.cameraId);
        m_zoneByCameraId.insert(info.cameraId, info.zone);
    }

    m_timer.setInterval(m_tickIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ScenarioPlayer::tick);
}

void ScenarioPlayer::resetLoadedState()
{
    m_tracks.clear();
    m_cameraOrder.clear();
    m_connectionEvents.clear();
    m_durationMs = 0;
    m_description.clear();
    m_looping = false;
    m_loaded = false;
    m_finished = false;
    m_scenarioClockMs = 0;
    m_baseUtcTime = QDateTime(); // - invalid로 되돌림: 다음 start()가 실제 "지금"으로 다시 채움
}

bool ScenarioPlayer::setError(LoadError error, const QString &message)
{
    m_lastError = error;
    m_lastErrorMessage = message;
    m_loaded = false;
    qCWarning(lcScenario) << message;
    return false;
}

bool ScenarioPlayer::loadScenario(const QString &filePath)
{
    resetLoadedState(); // - 이전에 로드된 게 있었어도 싹 지움: 절반만 로드된 상태가 재생되면 안 됨
    m_lastError = LoadError::None;
    m_lastErrorMessage.clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return setError(LoadError::FileNotFound, QStringLiteral("failed to open scenario file: %1 (%2)")
                                                       .arg(filePath, file.errorString()));

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return setError(LoadError::MalformedJson,
                         QStringLiteral("malformed scenario JSON in %1: %2").arg(filePath, parseError.errorString()));

    return parseScenario(doc.object(), filePath);
}

bool ScenarioPlayer::parseScenario(const QJsonObject &root, const QString &context)
{
    m_description = root.value(QStringLiteral("description")).toString();
    m_looping = root.value(QStringLiteral("loop")).toBool(false);

    const QJsonArray keyframesArray = root.value(QStringLiteral("keyframes")).toArray();
    if (keyframesArray.isEmpty())
        return setError(LoadError::EmptyKeyframes, QStringLiteral("scenario has no keyframes: %1").arg(context));

    // 1) 원본 배열을 순서대로 훑으며 파싱 + 카메라별로 묶음.
    //    firstSeenOrder는 QVector라 삽입 순서가 그대로 유지됨 -- byCameraRaw(QHash)의
    //    키 순회 순서는 프로세스마다 달라질 수 있어서(해시 시드) emit 순서 결정에 절대
    //    쓰면 안 됨. 이게 "같은 파일 -> 매번 같은 이벤트 순서"를 프로세스 재시작에도
    //    보장하는 핵심 포인트.
    QHash<QString, QVector<Keyframe>> byCameraRaw;
    QVector<QString> firstSeenOrder;
    QSet<QString> seenCameraIds;

    for (const QJsonValue &value : keyframesArray) {
        const QJsonObject obj = value.toObject();

        const QString cameraId = obj.value(QStringLiteral("camera_id")).toString();
        if (!m_knownCameraIds.contains(cameraId))
            return setError(LoadError::UnknownCameraId,
                             QStringLiteral("unknown camera_id '%1' in %2").arg(cameraId, context));

        Keyframe kf;
        kf.tMs = static_cast<qint64>(obj.value(QStringLiteral("t_ms")).toDouble());

        const QString riskStr = obj.value(QStringLiteral("risk_level")).toString(QStringLiteral("SAFE"));
        const std::optional<RiskTypes::RiskLevel> risk = parseRiskLevel(riskStr);
        if (!risk)
            return setError(LoadError::InvalidEnumValue,
                             QStringLiteral("invalid risk_level '%1' in %2").arg(riskStr, context));
        kf.riskLevel = *risk;

        const QString exceptionStr = obj.value(QStringLiteral("exception_state")).toString(QStringLiteral("NONE"));
        const std::optional<RiskTypes::ExceptionState> exception = parseExceptionState(exceptionStr);
        if (!exception)
            return setError(LoadError::InvalidEnumValue,
                             QStringLiteral("invalid exception_state '%1' in %2").arg(exceptionStr, context));
        kf.exceptionState = *exception;

        kf.distanceM = obj.value(QStringLiteral("distance_m")).toDouble(0.0);
        kf.distanceValid = obj.value(QStringLiteral("distance_valid")).toBool(true);

        if (obj.contains(QStringLiteral("person_bbox"))) {
            kf.personBBox = bboxFromScenarioJson(obj.value(QStringLiteral("person_bbox")).toObject());
            kf.hasPersonBBox = true;
        }
        if (obj.contains(QStringLiteral("forklift_bbox"))) {
            kf.forkliftBBox = bboxFromScenarioJson(obj.value(QStringLiteral("forklift_bbox")).toObject());
            kf.hasForkliftBBox = true;
        }

        if (!seenCameraIds.contains(cameraId)) {
            seenCameraIds.insert(cameraId);
            firstSeenOrder.append(cameraId);
        }
        byCameraRaw[cameraId].append(kf);
    }

    // 2) 카메라별로 t_ms 단조 증가 검증 + 트랙 확정
    for (const QString &cameraId : firstSeenOrder) {
        const QVector<Keyframe> &frames = byCameraRaw.value(cameraId);
        for (int i = 1; i < frames.size(); ++i) {
            if (frames[i].tMs == frames[i - 1].tMs)
                return setError(LoadError::DuplicateTimestamp,
                                 QStringLiteral("duplicate t_ms=%1 for camera_id '%2' in %3")
                                     .arg(frames[i].tMs)
                                     .arg(cameraId, context));
            if (frames[i].tMs < frames[i - 1].tMs)
                return setError(LoadError::OutOfOrderTimestamp,
                                 QStringLiteral("out-of-order t_ms for camera_id '%1' in %2").arg(cameraId, context));
        }

        CameraTrack track;
        track.keyframes = frames;
        track.zone = m_zoneByCameraId.value(cameraId);
        m_tracks.insert(cameraId, track);
        m_cameraOrder.append(cameraId);
        m_durationMs = std::max(m_durationMs, frames.last().tMs);
    }

    // 3) 전역 연결 상태 이벤트(옵션) 파싱 -- 배열이면 이미 JSON 순서 그대로라 별도 정렬 불필요
    const QJsonArray connectionArray = root.value(QStringLiteral("connection_events")).toArray();
    for (const QJsonValue &value : connectionArray) {
        const QJsonObject obj = value.toObject();
        ConnectionEvent event;
        event.tMs = static_cast<qint64>(obj.value(QStringLiteral("t_ms")).toDouble());

        const QString stateStr = obj.value(QStringLiteral("state")).toString();
        const std::optional<RiskTypes::ConnectionState> state = parseConnectionState(stateStr);
        if (!state)
            return setError(LoadError::InvalidEnumValue,
                             QStringLiteral("invalid connection_events[].state '%1' in %2").arg(stateStr, context));
        event.state = *state;

        if (!m_connectionEvents.isEmpty()) {
            const qint64 lastMs = m_connectionEvents.last().tMs;
            if (event.tMs == lastMs)
                return setError(LoadError::DuplicateTimestamp,
                                 QStringLiteral("duplicate t_ms=%1 in connection_events of %2")
                                     .arg(event.tMs)
                                     .arg(context));
            if (event.tMs < lastMs)
                return setError(LoadError::OutOfOrderTimestamp,
                                 QStringLiteral("out-of-order t_ms in connection_events of %1").arg(context));
        }

        m_connectionEvents.append(event);
        m_durationMs = std::max(m_durationMs, event.tMs);
    }

    m_loaded = true;
    m_lastError = LoadError::None;
    m_lastErrorMessage.clear();
    qCInfo(lcScenario) << "loaded scenario" << context << "cameras=" << m_cameraOrder.size()
                        << "durationMs=" << m_durationMs << "loop=" << m_looping;
    return true;
}

void ScenarioPlayer::start()
{
    if (!m_loaded) {
        qCWarning(lcScenario) << "start() called without a successfully loaded scenario, ignoring";
        return;
    }

    m_scenarioClockMs = 0;
    m_finished = false;
    if (!m_baseUtcTime.isValid()) // - setBaseUtcTimeForTesting()로 이미 고정돼 있으면 실제 시각으로 덮어쓰지 않음
        m_baseUtcTime = QDateTime::currentDateTimeUtc();

    setConnectionState(RiskTypes::ConnectionState::Connected);
    m_timer.start();
    advanceOnce(); // - MockMetadataSource와 동일한 관례: 첫 tick(t=0)을 다음 타이머까지 안 기다리고 바로 내보냄
}

void ScenarioPlayer::stop()
{
    m_timer.stop();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void ScenarioPlayer::tick()
{
    advanceOnce();
}

void ScenarioPlayer::advanceForTesting(int steps)
{
    for (int i = 0; i < steps; ++i)
        advanceOnce();
}

qint64 ScenarioPlayer::effectiveTimeMs() const
{
    if (m_durationMs <= 0)
        return 0;
    if (m_looping)
        return m_scenarioClockMs % m_durationMs; // - 루프: 매 바퀴 0으로 되감김
    return std::min(m_scenarioClockMs, m_durationMs); // - 비루프: 마지막 키프레임 값에서 고정(hold)
}

void ScenarioPlayer::advanceOnce()
{
    if (!m_loaded || m_finished)
        return;

    const qint64 tMs = effectiveTimeMs();

    for (const QString &cameraId : m_cameraOrder) {
        const CameraTrack &track = m_tracks.value(cameraId);
        const Keyframe sample = sampleTrackAt(track, tMs);
        emit metadataReceived(toRiskMetadata(cameraId, track, sample, tMs));
    }

    applyConnectionStateAt(tMs);

    // - "끝났는지"는 방금 emit에 실제로 쓴 시각(tMs) 기준으로 판단해야 함. 전진시킨 뒤의
    //   시계(m_scenarioClockMs)로 판단하면, 마지막 키프레임(t_ms==durationMs) 값 자체는
    //   한 번도 emit 못 해보고 그 직전 값에서 끝나버리는 결함이 생김(하루살이 버그로 실제 발견됨).
    const bool reachedEnd = !m_looping && tMs >= m_durationMs;

    m_scenarioClockMs += m_tickIntervalMs;

    if (reachedEnd && !m_finished) {
        m_finished = true;
        m_timer.stop();
        emit finished();
    }
}

ScenarioPlayer::Keyframe ScenarioPlayer::sampleTrackAt(const CameraTrack &track, qint64 tMs) const
{
    const QVector<Keyframe> &kf = track.keyframes;

    if (tMs <= kf.first().tMs)
        return kf.first();
    if (tMs >= kf.last().tMs)
        return kf.last();

    // - tMs보다 뒤인 첫 키프레임을 찾음(정렬돼 있음이 로드 시 이미 검증됨) -> 그 앞뒤 둘을 보간
    const auto it = std::upper_bound(kf.begin(), kf.end(), tMs,
                                      [](qint64 t, const Keyframe &k) { return t < k.tMs; });
    const Keyframe &b = *it;
    const Keyframe &a = *(it - 1);

    Keyframe result = a; // - 이산 필드(risk_level 등)는 앞 키프레임(a) 값을 그대로 유지
    result.tMs = tMs;

    const double span = double(b.tMs - a.tMs); // - 로드 시 중복/역행을 걸렀으므로 항상 > 0
    const double frac = (tMs - a.tMs) / span;

    result.distanceM = lerp(a.distanceM, b.distanceM, frac);
    if (a.hasPersonBBox && b.hasPersonBBox) // - 둘 다 검출 상태일 때만 bbox 자체를 보간 (한쪽만 있으면 등장/소실 경계라 이산 처리)
        result.personBBox = lerpBBox(a.personBBox, b.personBBox, frac);
    if (a.hasForkliftBBox && b.hasForkliftBBox)
        result.forkliftBBox = lerpBBox(a.forkliftBBox, b.forkliftBBox, frac);

    return result;
}

void ScenarioPlayer::applyConnectionStateAt(qint64 tMs)
{
    if (m_connectionEvents.isEmpty())
        return;

    // - tMs 이하인 이벤트 중 가장 나중 것을 찾음 (정렬돼 있으므로 앞에서부터 훑다가 넘으면 중단)
    //   해당하는 이벤트가 아직 하나도 없으면(early in scenario) 아무 것도 안 바꿈 -- start()가
    //   이미 Connected로 세팅해뒀으므로 그 값이 자연스러운 기본 상태임.
    bool any = false;
    RiskTypes::ConnectionState resolved = RiskTypes::ConnectionState::Connected;
    for (const ConnectionEvent &event : m_connectionEvents) {
        if (event.tMs > tMs)
            break;
        resolved = event.state;
        any = true;
    }
    if (any)
        setConnectionState(resolved); // - IMetadataSource::setConnectionState: 값이 같으면 emit 안 함(중복 방지)
}

RiskMetadata ScenarioPlayer::toRiskMetadata(const QString &cameraId, const CameraTrack &track, const Keyframe &sample,
                                             qint64 tMs) const
{
    RiskMetadata meta;
    meta.setCameraId(cameraId);
    meta.setZone(track.zone);
    meta.setRiskLevel(sample.riskLevel);
    meta.setExceptionState(sample.exceptionState);
    meta.setDistanceM(sample.distanceM);
    meta.setDistanceValid(sample.distanceValid);
    if (sample.hasPersonBBox)
        meta.setPersonBBox(sample.personBBox); // - 없으면 기본 BBox()(invalid)로 남음 = "검출 없음"
    if (sample.hasForkliftBBox)
        meta.setForkliftBBox(sample.forkliftBBox);
    // utc_time은 시나리오 시계를 따라 흐름: TtcEstimator/워치독 등 신선도(staleness) 판정이
    // 실제 시각과 동떨어진 값을 보면 다 무효 처리해버리므로, base(실제 UTC) + 경과(tMs)로 맞춤
    meta.setUtcTime(m_baseUtcTime.isValid() ? m_baseUtcTime.addMSecs(tMs) : QDateTime::currentDateTimeUtc());
    return meta;
}

RiskMetadata ScenarioPlayer::sampleForTesting(const QString &cameraId, qint64 tMs) const
{
    if (!m_loaded || !m_tracks.contains(cameraId))
        return RiskMetadata();

    const CameraTrack &track = m_tracks.value(cameraId);
    const Keyframe sample = sampleTrackAt(track, tMs);
    return toRiskMetadata(cameraId, track, sample, tMs);
}
