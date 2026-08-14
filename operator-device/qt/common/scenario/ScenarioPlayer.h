#pragma once

// ============================================================================
// ScenarioPlayer -- JSON으로 미리 짜둔 타임라인을 재생하는 결정적(deterministic)
// IMetadataSource 구현체.
//
// 왜 필요한가
//   MockMetadataSource는 랜덤 워크라서 데모할 때마다 다른 값이 나오고, 사람이
//   버튼을 눌러야 상황이 바뀜 -- 재현 가능한 데모/회귀 테스트가 불가능함.
//   게다가 MockMetadataSource.cpp의 personH는 상수(0.30)라서 bbox 높이가 절대
//   안 바뀜 -- TtcEstimator는 ds/dt>0(높이가 커짐)이 있어야 TTC를 낼 수 있는데,
//   그 조건을 Mock 경로로는 구조적으로 만들 수 없음.
//   ScenarioPlayer는 파일에 적힌 키프레임을 정해진 시계로 재생만 하므로,
//   같은 파일이면 항상 같은 이벤트 시퀀스가 나오고, bbox 높이도 자유롭게 스크립트 가능.
//
// 결정성(determinism)을 만드는 방법
//   - 재생 시계(m_scenarioClockMs)는 "실제 경과 시간"이 아니라 "틱이 몇 번
//     울렸는가 * 고정 간격"으로 전진함 -- 실제 QTimer가 정확히 200.0ms마다 안
//     울려도(OS 스케줄링 지터), 논리적 시나리오 시각은 항상 0,200,400,... 그대로임.
//   - 랜덤 함수를 어디서도 안 씀.
//   - advanceForTesting()으로 실제 QTimer/이벤트 루프 대기 없이 같은 코드 경로를
//     즉시 N번 실행할 수 있어서, 테스트가 "재생을 두 번 해서 비교"를 실시간
//     대기 없이 빠르게 할 수 있음.
//
// 키프레임 사이 보간(interpolation)
//   distance_m과 bbox(x,y,w,h)는 두 키프레임 사이를 선형보간함 -- 계단식으로
//   두면 bbox 높이가 순간적으로 튀어서 TtcEstimator의 기울기 추정이 엉망이 됨.
//   risk_level/exception_state/distance_valid/bbox 존재 여부는 이산값이라
//   보간 안 하고 "앞 키프레임 값을 다음 키프레임까지 유지"로 처리함.
// ============================================================================

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>
#include <optional>

#include "../models/BBox.h"
#include "../models/CameraInfo.h"
#include "../models/RiskMetadata.h"
#include "../network/IMetadataSource.h"

class ScenarioPlayer : public IMetadataSource
{
    Q_OBJECT

public:
    // - 시나리오 로드가 실패할 수 있는 사유 (성공하면 None)
    enum class LoadError {
        None,
        FileNotFound,         // - 파일이 없거나 못 엶
        MalformedJson,        // - JSON 문법 오류이거나 최상위가 object가 아님
        EmptyKeyframes,       // - keyframes 배열이 없거나 비어있음
        UnknownCameraId,      // - 생성자에 안 준 camera_id를 키프레임이 참조함
        OutOfOrderTimestamp,  // - 같은 camera_id(또는 connection_events) 안에서 t_ms가 역행함
        DuplicateTimestamp,   // - 같은 camera_id(또는 connection_events) 안에서 t_ms가 중복됨
        InvalidEnumValue,     // - risk_level/exception_state/connection_events[].state 문자열이 알려진 값이 아님
    };

    static constexpr int kDefaultTickIntervalMs = 200; // - RiskEventSource.cpp의 kServerHeartbeatMs와 동일 주기: 서버가 실제로 이 간격으로 위험 판정을 발행하므로, TtcEstimator의 창(kDefaultWindowSize=8)이 기대하는 샘플 간격과 맞춰야 함

    explicit ScenarioPlayer(QVector<CameraInfo> cameras, int tickIntervalMs = kDefaultTickIntervalMs, QObject *parent = nullptr);

    // 시나리오 JSON 파일을 로드. 성공 시 true, 실패 시 false를 돌려주고
    // lastError()/lastErrorMessage()에 사유를 남김 (테스트가 그대로 assert 가능).
    // 실패해도 예외를 던지거나 크래시하지 않음 -- 절반만 로드된 시나리오는 절대
    // 재생 상태로 안 남고, 이전에 로드돼 있던 것도 함께 지워짐(안전한 쪽으로).
    bool loadScenario(const QString &filePath);

    bool isLoaded() const { return m_loaded; }
    LoadError lastError() const { return m_lastError; }
    QString lastErrorMessage() const { return m_lastErrorMessage; }
    QString description() const { return m_description; }

    // 시나리오 전체 길이(ms). 로드 전이면 0.
    qint64 durationMs() const { return m_durationMs; }

    // JSON의 "loop" 필드로 로드 시 채워짐. 필요하면 로드 후 수동으로 덮어쓸 수 있음.
    void setLooping(bool looping) { m_looping = looping; }
    bool looping() const { return m_looping; }

    // loop=false인 시나리오가 끝까지 재생됐는지 (finished() 신호와 별개로 폴링 가능)
    bool isFinished() const { return m_finished; }

    void start() override;
    void stop() override;

    // --- 테스트 전용 훅 (아래 둘 다 프로덕션 경로에서는 안 씀) ---

    // t=0에 대응하는 UTC 시각을 고정. 안 부르면 start()가 호출된 실제 시각을 씀.
    // 재현성 테스트가 두 번 재생한 결과를 utc_time까지 완전히 동일하게 비교할 수 있게 함.
    void setBaseUtcTimeForTesting(const QDateTime &baseUtcTime) { m_baseUtcTime = baseUtcTime; }

    // 실제 QTimer/이벤트 루프 대기 없이 tick()과 완전히 같은 코드 경로를 steps번 실행.
    // start()가 이미 실행 중이어도 안전(실제 타이머가 테스트 도중 우연히 안 울리는 한
    // 단위 테스트는 이벤트 루프를 안 돌리므로 문제 없음).
    void advanceForTesting(int steps = 1);

    // 재생 상태(시계/타이머)와 무관하게, 특정 시각(tMs)의 보간 결과를 그 자리에서
    // 계산해서 돌려줌. interpolation 자체만 tick 그리드와 독립적으로 검증하기 위함.
    // camera_id가 모르는 값이거나 로드 전이면 기본값(빈 RiskMetadata) 반환.
    RiskMetadata sampleForTesting(const QString &cameraId, qint64 tMs) const;

signals:
    // loop=false인 시나리오가 마지막 키프레임까지 재생을 마쳤을 때 1회 emit
    void finished();

private slots:
    void tick();

private:
    // - 시나리오 파일 하나에 등장하는 키프레임 1개 (한 카메라, 한 시각)
    struct Keyframe {
        qint64 tMs = 0;
        RiskTypes::RiskLevel riskLevel = RiskTypes::RiskLevel::Safe;
        RiskTypes::ExceptionState exceptionState = RiskTypes::ExceptionState::None;
        double distanceM = 0.0;
        bool distanceValid = true;
        BBox personBBox;
        bool hasPersonBBox = false;
        BBox forkliftBBox;
        bool hasForkliftBBox = false;
    };

    // - 카메라 1대의 키프레임 목록(t_ms 오름차순 보장됨) + zone
    struct CameraTrack {
        QVector<Keyframe> keyframes;
        QString zone;
    };

    // - 전역 연결 상태 변경 이벤트 (카메라와 무관, IMetadataSource::connectionState 전체에 적용)
    struct ConnectionEvent {
        qint64 tMs = 0;
        RiskTypes::ConnectionState state = RiskTypes::ConnectionState::Connected;
    };

    bool setError(LoadError error, const QString &message);
    bool parseScenario(const class QJsonObject &root, const QString &context);
    void resetLoadedState();

    // - 현재 재생 시계 기준으로 카메라별 이벤트를 emit하고, 연결 상태를 반영하고, 시계를 전진시킴
    void advanceOnce();
    // - 루프 여부에 따라 "지금 계산에 쓸 유효 시각"으로 변환 (looping이면 duration으로 wrap)
    qint64 effectiveTimeMs() const;
    // - 카메라 1대의 트랙에서 tMs 시점 값을 찾음 (범위 밖이면 양끝 값 고정, 범위 안이면 보간)
    Keyframe sampleTrackAt(const CameraTrack &track, qint64 tMs) const;
    // - tMs 시점에 적용돼야 할 연결 상태를 찾아 setConnectionState() 호출 (해당하는 이벤트가 없으면 안 건드림)
    void applyConnectionStateAt(qint64 tMs);
    RiskMetadata toRiskMetadata(const QString &cameraId, const CameraTrack &track, const Keyframe &sample, qint64 tMs) const;

    QHash<QString, CameraTrack> m_tracks;      // - camera_id -> 트랙 (조회용, 순서 무의미)
    QVector<QString> m_cameraOrder;            // - JSON에 처음 등장한 순서 그대로 (emit 순서를 결정적으로 만듦 -- QHash 순회 순서에 의존하면 안 됨)
    QVector<ConnectionEvent> m_connectionEvents; // - t_ms 오름차순
    QSet<QString> m_knownCameraIds;            // - 생성자로 받은 카메라 목록 (unknown camera_id 검증용)
    QHash<QString, QString> m_zoneByCameraId;

    QTimer m_timer;
    int m_tickIntervalMs;
    qint64 m_scenarioClockMs = 0; // - 논리적 시나리오 시각(ms) -- 실제 경과 시간이 아니라 틱 횟수 * 간격
    qint64 m_durationMs = 0;
    bool m_looping = false;
    bool m_loaded = false;
    bool m_finished = false;
    QDateTime m_baseUtcTime; // - t=0에 대응하는 UTC 시각 (start()가 채움, setBaseUtcTimeForTesting으로 고정 가능)

    QString m_description;
    LoadError m_lastError = LoadError::None;
    QString m_lastErrorMessage;
};
