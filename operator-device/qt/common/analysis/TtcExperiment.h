#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "../models/RiskMetadata.h"
#include "TtcEstimator.h"
#include "TtcResult.h"

// ============================================================================
// TtcExperiment -- 예측 실험(experiment) 전용 창구. 카메라별 TtcEstimator를 하나씩
// 들고 있다가, personBBox 높이가 들어올 때마다 먹여서 QML이 읽어갈 수 있게 함.
//
// MetadataDistributor::metadataUpdated의 "추가" 구독자일 뿐임 -- 절대로
// handleMetadata()(MetadataDistributor.cpp) 내부에 넣거나, riskLevel/경보/로그
// 판정에 값을 되돌려보내지 않음. main.cpp에서 순수 구경꾼(observer)으로만 연결함.
// ============================================================================
class TtcExperiment : public QObject
{
    Q_OBJECT

public:
    explicit TtcExperiment(QObject *parent = nullptr);

    // cameraId의 최근 추정 결과 -- 아직 한 번도 데이터가 안 들어온 카메라면
    // 기본값(TtcResult{}, valid=false, reason="")을 그대로 돌려줌
    Q_INVOKABLE TtcResult resultFor(const QString &cameraId) const;

public slots:
    // MetadataDistributor::metadataUpdated에 연결하는 슬롯 (main.cpp에서 별도 connect,
    // handleMetadata의 판정 로직과는 완전히 분리된 경로)
    void handleMetadata(const RiskMetadata &metadata);

signals:
    // cameraId의 결과가 갱신됐음을 알림 -- QML은 이 신호를 받고 resultFor()로 다시 읽어감
    void resultUpdated(const QString &cameraId);

private:
    QHash<QString, TtcEstimator> m_estimators; // - 카메라별 추정기: bbox 높이 궤적을 서로 안 섞이게 각자 따로 추적
    QHash<QString, TtcResult> m_results;        // - 카메라별 마지막 추정 결과 캐시 (resultFor()가 읽는 곳)
};
