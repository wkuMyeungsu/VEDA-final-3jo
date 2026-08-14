#include "MetadataDistributor.h"

#include "../network/IMetadataSource.h"

MetadataDistributor::MetadataDistributor(QVector<CameraInfo> cameras, int eventLogMaxEntries, QObject *parent)
    : QObject(parent)
    , m_eventLogModel(eventLogMaxEntries)
{
    m_cameraListModel.setCameras(cameras);
    for (const CameraInfo &info : cameras)
        m_cameraNames.insert(info.cameraId, info.name);
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
    // 1) 카메라별 마지막 상태 캐시 갱신 (latestFor()가 읽는 곳)
    const QString targetId = !metadata.streamId().isEmpty() ? metadata.streamId() : metadata.cameraId();
    m_latest.insert(targetId, metadata);
    m_latest.insert(metadata.cameraId(), metadata);

    // 2) 카메라 그리드/카드 모델 갱신 -> CameraGrid.qml, CameraCard.qml
    m_cameraListModel.updateRisk(targetId, metadata.riskLevel(), metadata.exceptionState(),
                                  metadata.distanceM(), metadata.distanceValid());

    // 3) 경보 목록 모델 갱신 -> AlertListView.qml
    //    SAFE + 예외없음이면 upsert 내부에서 목록에서 빠짐
    m_alertListModel.upsert(targetId, m_cameraNames.value(targetId, m_cameraNames.value(metadata.cameraId())), metadata.zone(),
                             metadata.riskLevel(), metadata.distanceM(), metadata.distanceValid(),
                             metadata.exceptionState());

    // 4) 이벤트 로그는 "특이사항"일 때만 기록 -> EventLogPanel.qml
    //    SAFE + 예외없음(평상시)은 로그 안 남김 -> 로그가 Event로만 채워짐
    const bool noteworthy =
        metadata.riskLevel() != RiskTypes::RiskLevel::Safe || metadata.exceptionState() != RiskTypes::ExceptionState::None;
    if (noteworthy)
        m_eventLogModel.addEntry(metadata);

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
