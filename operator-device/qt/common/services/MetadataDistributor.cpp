#include "MetadataDistributor.h"

#include <QDateTime>

#include "../network/IMetadataSource.h"

MetadataDistributor::MetadataDistributor(QVector<CameraInfo> cameras, int eventLogMaxEntries, QObject *parent)
    : QObject(parent)
    , m_cameras(cameras)
    , m_eventLogModel(eventLogMaxEntries)
{
    m_cameraListModel.setCameras(cameras);
    for (const CameraInfo &info : cameras) {
        m_cameraNames.insert(info.effectiveId(), info.name);
        m_cameraNames.insert(info.cameraId, info.name);
    }
}

// 데이터 출처 교체 지점
// - 지금: MockMetadataSource(가짜 데이터)
// - 실서버 연동 시: RiskEventSource 등으로 교체
// - IMetadataSource 인터페이스만 맞으면 아래 로직 변경 불필요
// - 기존 source 있으면 먼저 disconnect (안 그러면 신호 중복 수신)
void MetadataDistributor::setSource(IMetadataSource *source)
{
    if (m_source == source)
        return;

    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);

    m_source = source;

    if (m_source) {
        // metadataReceived 1건 -> handleMetadata() 호출 -> 3개 모델 분배 (유일한 진입점)
        connect(m_source, &IMetadataSource::metadataReceived, this, &MetadataDistributor::handleMetadata);
        // 연결 상태 변화(끊김/연결중/연결됨) 감지 -> 상태바 등에 반영
        connect(m_source, &IMetadataSource::connectionStateChanged, this,
                &MetadataDistributor::handleSourceConnectionStateChanged);
        // 워치독 등에 의한 전역 통신 두절 -> 전 채널 NetworkDisconnected 브로드캐스트
        connect(m_source, &IMetadataSource::linkLost, this, &MetadataDistributor::handleLinkLost);
        // connect는 "이후 변화"만 감지 -> 현재 상태 수동 1회 반영 (안 하면 기본값으로 보임)
        handleSourceConnectionStateChanged(m_source->connectionState());
    }
}

// 데모 패널 전용 보조 입력
// - 실서버 소스(m_source)는 그대로 두고 데모 값만 추가로 흘려보냄
// - connectionStateChanged는 일부러 연결 안 함 -- 서버 연결 배지는 실제 링크 상태가 진실이어야 함
// - start()/stop() 대상도 아님 -- 수명은 호출한 쪽(main.cpp)이 관리
void MetadataDistributor::setDemoSource(IMetadataSource *source)
{
    if (m_demoSource == source)
        return;
    // 주 소스와 같은 객체면 아래 disconnect가 주 경로까지 끊어버림 -- 애초에 붙일 필요도 없음
    if (source && source == m_source)
        return;

    if (m_demoSource)
        disconnect(m_demoSource, nullptr, this, nullptr);

    m_demoSource = source;

    if (m_demoSource)
        connect(m_demoSource, &IMetadataSource::metadataReceived, this, &MetadataDistributor::handleMetadata);
}

void MetadataDistributor::start()
{
    if (m_source)
        m_source->start();
}

void MetadataDistributor::stop()
{
    if (m_source)
        m_source->stop();
}

// 카메라별 "마지막으로 받은 값" 즉시 조회용
// - ExpandedCameraView.qml이 카메라 클릭 시 다음 이벤트 안 기다리고 바로 표시
RiskMetadata MetadataDistributor::latestFor(const QString &cameraId) const
{
    return m_latest.value(cameraId);
}

// 카메라 1대의 새 이벤트 1건 처리
// - 서버/Mock에서 오는 모든 RiskMetadata가 예외 없이 여기 거침
// - 로직/로그 추가할 땐 여기가 기준점
void MetadataDistributor::handleMetadata(const RiskMetadata &metadata)
{
    // 전역 통신 두절(linkLost) 상태였다가 정상 패킷 수신으로 복구된 경우
    // -> 다른 비활성 채널들의 NetworkDisconnected 상태를 Safe/None(대기)으로 복구
    if (m_linkLostState && metadata.exceptionState() != RiskTypes::ExceptionState::NetworkDisconnected) {
        m_linkLostState = false;
        const QString activeTarget = !metadata.streamId().isEmpty() ? metadata.streamId() : metadata.cameraId();
        const QDateTime now = QDateTime::currentDateTimeUtc();
        for (const CameraInfo &cam : m_cameras) {
            const QString camEffId = cam.effectiveId();
            if (camEffId != activeTarget) {
                RiskMetadata recoveryMeta;
                recoveryMeta.setStreamId(camEffId);
                recoveryMeta.setCameraId(cam.cameraId);
                recoveryMeta.setZone(cam.zone);
                recoveryMeta.setExceptionState(RiskTypes::ExceptionState::None);
                recoveryMeta.setRiskLevel(RiskTypes::RiskLevel::Safe);
                recoveryMeta.setDistanceM(0.0);
                recoveryMeta.setDistanceValid(false);
                recoveryMeta.setUtcTime(now);
                handleMetadata(recoveryMeta);
            }
        }
    }

    // 1) 카메라별 마지막 상태 캐시 갱신 (latestFor()가 읽는 곳)
    const QString targetId = !metadata.streamId().isEmpty() ? metadata.streamId() : metadata.cameraId();
    m_latest.insert(targetId, metadata);
    if (metadata.streamId().isEmpty())
        m_latest.insert(metadata.cameraId(), metadata);

    // 2) 카메라 그리드/카드 모델 갱신 -> CameraGrid.qml, CameraCard.qml
    m_cameraListModel.updateRisk(targetId, metadata.riskLevel(), metadata.exceptionState(),
                                  metadata.distanceM(), metadata.distanceValid());

    // 3) 경보 목록 모델 갱신 -> AlertListView.qml
    //    SAFE + 예외없음이면 upsert 내부에서 목록에서 빠짐
    m_alertListModel.upsert(targetId, m_cameraNames.value(targetId, m_cameraNames.value(metadata.cameraId())), metadata.zone(),
                             metadata.riskLevel(), metadata.distanceM(), metadata.distanceValid(),
                             metadata.exceptionState());

    // 4) 이벤트 로그는 "특이사항 발생 또는 상태 전이"일 때만 기록 (스팸 방지)
    //    동일한 예외/위험 상태가 계속 유지될 때는 중복 로깅하지 않고 상태 전이 시에만 1회 기록
    const bool noteworthy =
        metadata.riskLevel() != RiskTypes::RiskLevel::Safe || metadata.exceptionState() != RiskTypes::ExceptionState::None;

    const bool riskChanged = (m_lastLoggedRisk.value(targetId, RiskTypes::RiskLevel::Safe) != metadata.riskLevel());
    const bool exceptionChanged = (m_lastLoggedException.value(targetId, RiskTypes::ExceptionState::None) != metadata.exceptionState());

    if (noteworthy) {
        if (riskChanged || exceptionChanged) {
            m_lastLoggedRisk.insert(targetId, metadata.riskLevel());
            m_lastLoggedException.insert(targetId, metadata.exceptionState());
            m_eventLogModel.addEntry(metadata);
        }
    } else {
        m_lastLoggedRisk.insert(targetId, RiskTypes::RiskLevel::Safe);
        m_lastLoggedException.insert(targetId, RiskTypes::ExceptionState::None);
    }

    // 5) 실시간 구독 중인 QML(ExpandedCameraView.qml 등)에 원본 그대로 전달
    emit metadataUpdated(metadata);
}

// source(서버/Mock) 연결 상태가 바뀔 때만 호출됨
// - 값이 같으면 조기 리턴 (불필요한 QML 갱신/애니메이션 재생 방지)
void MetadataDistributor::handleSourceConnectionStateChanged(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged();
}

void MetadataDistributor::handleLinkLost()
{
    if (m_linkLostState)
        return;
    m_linkLostState = true;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const CameraInfo &cam : m_cameras) {
        RiskMetadata meta;
        meta.setStreamId(cam.effectiveId());
        meta.setCameraId(cam.cameraId);
        meta.setZone(cam.zone);
        meta.setExceptionState(RiskTypes::ExceptionState::NetworkDisconnected);
        meta.setRiskLevel(RiskTypes::RiskLevel::Safe);
        meta.setDistanceM(0.0);
        meta.setDistanceValid(false);
        meta.setUtcTime(now);
        handleMetadata(meta);
    }
}
