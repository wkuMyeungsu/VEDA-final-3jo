#include "MetadataDistributor.h"

#include "../network/IMetadataSource.h"

MetadataDistributor::MetadataDistributor(QVector<CameraInfo> cameras, int eventLogMaxEntries, QObject *parent)
    : QObject(parent)
    , m_eventLogModel(eventLogMaxEntries)                                      // - 생성자: 이벤트 로그 모델의 최대 보관 개수 설정
{
    m_cameraListModel.setCameras(cameras);                                     // - 카메라 설정 등록: 전체 카메라 목록 모델 초기화
    for (const CameraInfo &info : cameras)                                     // - 카메라 목록 순회: ID 기반 카메라 이름 맵 구성
        m_cameraNames.insert(info.cameraId, info.name);                        // - 이름 맵 저장: 카메라 ID별 이름 저장
}

void MetadataDistributor::setSource(IMetadataSource *source)
{
    if (m_source == source)                                                    // - 중복 설정 방지: 동일 소스인 경우 실행 생략
        return;

    if (m_source)                                                              // - 기존 소스 해제: 이전에 연결된 데이터 소스의 신호 연결 해제
        disconnect(m_source, nullptr, this, nullptr);

    m_source = source;                                                         // - 소스 교체: 새 데이터 소스 객체 저장

    if (m_source) {
        connect(m_source, &IMetadataSource::metadataReceived, this, &MetadataDistributor::handleMetadata); // - 데이터 수신 신호 연결: 데이터 수신 시 분배 처리 함수 호출
        connect(m_source, &IMetadataSource::connectionStateChanged, this,
                &MetadataDistributor::handleSourceConnectionStateChanged);     // - 상태 변경 신호 연결: 소스 연결 상태 변경 감지
        handleSourceConnectionStateChanged(m_source->connectionState());        // - 초기 상태 반영: 현재 소스의 연결 상태 수동 1회 반영
    }
}

void MetadataDistributor::start()
{
    if (m_source)                                                              // - 수신 시작: 데이터 소스가 존재하는 경우 시작 함수 호출
        m_source->start();
}

void MetadataDistributor::stop()
{
    if (m_source)                                                              // - 수신 정지: 데이터 소스가 존재하는 경우 정지 함수 호출
        m_source->stop();
}

RiskMetadata MetadataDistributor::latestFor(const QString &cameraId) const
{
    return m_latest.value(cameraId);                                           // - 최신 상태 반환: 지정 카메라의 가장 최근 위험 메타데이터 조회 및 반환
}

void MetadataDistributor::handleMetadata(const RiskMetadata &metadata)
{
    m_latest.insert(metadata.cameraId(), metadata);                             // - 상태 캐시 갱신: 카메라 ID별 최신 메타데이터 맵에 저장

    m_cameraListModel.updateRisk(metadata.cameraId(), metadata.riskLevel(), metadata.exceptionState(),
                                  metadata.distanceM(), metadata.distanceValid()); // - 카메라 목록 모델 갱신: 위험 수준, 예외 상태 및 거리 정보 업데이트

    m_alertListModel.upsert(metadata.cameraId(), m_cameraNames.value(metadata.cameraId()), metadata.zone(),
                             metadata.riskLevel(), metadata.distanceM(), metadata.distanceValid(),
                             metadata.exceptionState());                       // - 경보 목록 모델 갱신: 위험 수준에 따라 경보 목록 항목 추가/갱신/삭제

    const bool noteworthy =
        metadata.riskLevel() != RiskTypes::RiskLevel::Safe || metadata.exceptionState() != RiskTypes::ExceptionState::None; // - 주요 이벤트 검증: 안전 단계가 아니거나 예외 발생 여부 확인
    if (noteworthy)
        m_eventLogModel.addEntry(metadata);                                    // - 이벤트 로그 추가: 주요 위험/예외 발생 시 로그 모델에 기록

    emit metadataUpdated(metadata);                                            // - 신호 발생: 실시간 메타데이터 갱신 이벤트 전달
}

void MetadataDistributor::handleSourceConnectionStateChanged(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)                                            // - 중복 변경 방지: 동일한 상태값인 경우 처리 생략
        return;
    m_connectionState = state;                                                 // - 상태값 갱신: 내부 상태 변수 업데이트
    emit connectionStateChanged();                                             // - 신호 발생: 데이터 소스 연결 상태 변경 알림
}
