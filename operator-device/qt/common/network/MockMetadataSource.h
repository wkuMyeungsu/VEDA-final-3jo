#pragma once

#include <QHash>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <optional>

#include "../models/CameraInfo.h"
#include "IMetadataSource.h"

// 실서버 없이도 앱이 돌아가게 해주는 가짜 IMetadataSource 구현체
// - tick()마다 카메라별 랜덤 이벤트 생성 (대부분 SAFE, 가끔 위험/예외)
// - person/forklift bbox는 랜덤으로 천천히 이동 (데모가 "살아있게" 보이도록)
// - DemoController의 override* 호출 시 그 카메라는 랜덤 대신 지정값 고정
// - fromJson()을 안 거침 -- 서버 통신 경로와 완전히 분리, distanceValid는 항상 true
class MockMetadataSource : public IMetadataSource
{
    Q_OBJECT

public:
    explicit MockMetadataSource(QVector<CameraInfo> cameras, QObject *parent = nullptr);

    // 연결 상태를 Connected로 바꾸고 타이머 시작 (첫 tick도 바로 1회 실행)
    void start() override;
    // 타이머 정지, 연결 상태를 Disconnected로 바꿈
    void stop() override;

    // --- 데모 조작면 (DemoController가 호출) ---
    void setAutoPlay(bool enabled);
    bool autoPlay() const { return m_autoPlay; }

    // 이후 이 카메라는 tick()의 랜덤 로직을 안 타고 지정값 고정 (override 계열 공통)
    void setRiskOverride(const QString &cameraId, RiskTypes::RiskLevel level, double distanceM,
                          RiskTypes::ExceptionState exception);
    // override 해제 -- autoPlay면 곧바로 랜덤 로직으로 복귀
    void clearRiskOverride(const QString &cameraId);

    // 사람 bbox를 지정값으로 고정 (랜덤 워크 대신 이 값 사용)
    void setPersonBBoxOverride(const QString &cameraId, const BBox &box);
    // 지게차 bbox를 지정값으로 고정
    void setForkliftBBoxOverride(const QString &cameraId, const BBox &box);
    // 사람/지게차 bbox override를 둘 다 해제
    void clearBBoxOverrides(const QString &cameraId);

private slots:
    // QTimer 주기마다 호출 -- 카메라 전체를 순회하며 이벤트 생성
    void tick();

private:
    // 카메라 1대의 "지금 상태" -- 다음 tick 때도 이어서 쓰이므로 여기 보관
    struct CameraState {
        QString zone;
        bool overrideActive = false;                                       // true면 랜덤 대신 아래 override* 값 사용
        RiskTypes::RiskLevel overrideLevel = RiskTypes::RiskLevel::Safe;
        double overrideDistanceM = 0.0;
        RiskTypes::ExceptionState overrideException = RiskTypes::ExceptionState::None;
        std::optional<BBox> personBBoxOverride;   // 값 있으면 랜덤 위치 대신 이 bbox 고정 사용
        std::optional<BBox> forkliftBBoxOverride;
        QPointF personCenter;    // 랜덤워크(wanderPosition) 현재 중심점 -- 다음 tick의 시작점
        QPointF forkliftCenter;

        // 위험 상태든 bbox든 뭔가 고정된 게 하나라도 있으면 true
        bool hasAnyOverride() const
        {
            return overrideActive || personBBoxOverride.has_value() || forkliftBBoxOverride.has_value();
        }
    };

    // CameraState 하나로 실제 RiskMetadata 이벤트 1건을 조립 (랜덤 or override 값 사용)
    RiskMetadata generateForCamera(const QString &cameraId, CameraState &state) const;
    // override 계열 setter들이 값 바꾼 직후 "즉시 1건 내보내기" 위해 호출
    // (다음 정기 tick까지 안 기다리고 바로 화면에 반영되게 함)
    void emitCameraUpdate(const QString &cameraId);
    // 이전 위치에서 살짝만 랜덤 이동한 새 좌표 반환 (0.05~0.80 범위로 제한)
    static QPointF wanderPosition(QPointF current);

    QHash<QString, CameraState> m_states;
    QTimer m_timer;
    bool m_autoPlay = true;
};
