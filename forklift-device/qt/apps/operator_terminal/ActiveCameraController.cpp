#include "ActiveCameraController.h"

#include "network/IWarningDevice.h"
#include "services/MetadataDistributor.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"

#include <QLoggingCategory>
namespace {
Q_LOGGING_CATEGORY(lcActiveCamera, "safety.activecamera")                  // - 로깅 카테고리 정의: 활성 카메라 제어 로그 분류용 이름 지정
}

ActiveCameraController::ActiveCameraController(QVector<CameraInfo> cameras, MetadataDistributor *metadataDistributor,
                                                 VideoSourceManager *videoManager, IWarningDevice *warningDevice,
                                                 QObject *parent)
    : QObject(parent)
    , m_metadataDistributor(metadataDistributor)                           // - 데이터 분배기 객체 보관
    , m_videoManager(videoManager)                                         // - 영상 소스 관리자 객체 보관
    , m_warningDevice(warningDevice)                                       // - 경고 장치 제어 객체 보관
{
    for (const CameraInfo &info : cameras)                                 // - 카메라 목록 등록: 제공된 카메라 정보를 맵에 저장
        m_cameras.insert(info.cameraId, info);

    if (m_metadataDistributor)                                             // - 데이터 수신 이벤트 연결: 분배기의 데이터 갱신 신호 연결
        connect(m_metadataDistributor, &MetadataDistributor::metadataUpdated, this,
                &ActiveCameraController::handleMetadataUpdated);

    if (m_warningDevice) {                                                 // - 경고 장치 상태 신호 연결: FPGA 상태 변화를 캐시에 반영
        connect(m_warningDevice, &IWarningDevice::estopActiveChanged, this,
                &ActiveCameraController::handleWarningDeviceStateChanged);
        connect(m_warningDevice, &IWarningDevice::movementCutoffActiveChanged, this,
                &ActiveCameraController::handleWarningDeviceStateChanged);
        connect(m_warningDevice, &IWarningDevice::errorLatchChanged, this,
                &ActiveCameraController::handleWarningDeviceStateChanged);
        connect(m_warningDevice, &IWarningDevice::fpgaConnectionStateChanged, this,
                &ActiveCameraController::handleWarningDeviceStateChanged);
    }

    m_onvifParser = new OnvifBBoxParser(this);                             // - ONVIF 파서 생성: BBox 좌표 추출용 객체 생성
    connect(m_onvifParser, &OnvifBBoxParser::personDetected, this, &ActiveCameraController::handlePersonDetected); // - 사람 검출 신호 연결
}

void ActiveCameraController::setActiveCameraId(const QString &cameraId)
{
    if (m_activeCameraId == cameraId)                                      // - 중복 전환 방지: 동일한 카메라 ID인 경우 실행 생략
        return;

    m_activeCameraId = cameraId;                                           // - 활성 카메라 ID 갱신: 현재 화면 표시 카메라 ID 변경

    const CameraInfo info = m_cameras.value(cameraId);                     // - 카메라 정보 조회: 선택된 카메라의 세부 정보 검색
    m_zone = info.zone;                                                    // - 구역 정보 갱신: 카메라 설치 구역 저장
    m_cameraName = info.name;                                              // - 카메라 이름 갱신: 화면 표시용 이름 저장

    m_latest = m_metadataDistributor ? m_metadataDistributor->latestFor(cameraId) : RiskMetadata(); // - 최신 위험 데이터 조회: 초기 위험 정보 가져오기
    m_onvifActive = false;                                                 // - ONVIF 감지 상태 초기화: 감지 상태 해제
    m_onvifPersonBBox = BBox();                                            // - 사람 영역 상자 초기화: 빈 BBox로 설정

    attachVideoConnection();                                               // - 영상 연결 수립: 새로 선택된 카메라의 영상 신호 연결

    if (m_warningDevice)                                                   // - 경고 장치 갱신: 현재 카메라의 위험 수치 반영
        m_warningDevice->setRiskLevel(m_latest.riskLevel());

    emit activeCameraIdChanged();                                          // - 카메라 변경 신호 발생: 활성 카메라 ID 변경 알림
    emit metadataChanged();                                                // - 메타데이터 변경 신호 발생: 위험 정보 변경 알림
}

void ActiveCameraController::attachVideoConnection()
{
    QObject::disconnect(m_videoConnection);                                // - 이전 영상 신호 연결 해제: 기존 소켓 신호 수신 차단
    QObject::disconnect(m_onvifConnection);                                // - 이전 ONVIF 신호 연결 해제: 기존 메타데이터 수신 차단

    IVideoSource *source = m_videoManager ? m_videoManager->sourceFor(m_activeCameraId) : nullptr; // - 영상 소스 조회: 현재 활성 카메라의 영상 객체 획득
    if (!source) {                                                         // - 영상 소스 유효성 검증: 소스가 없는 경우 끊김 처리
        m_videoConnectionState = RiskTypes::ConnectionState::Disconnected; // - 상태 갱신: 영상 연결 상태를 '연결 끊김'으로 변경
        emit videoConnectionStateChanged();                                // - 상태 변경 신호 발생: 연결 상태 알림
        return;
    }

    m_videoConnectionState = source->connectionState();                    // - 상태 갱신: 현재 영상 소스의 연결 상태 반영
    m_videoConnection = connect(source, &IVideoSource::connectionStateChanged, this,
                                 [this](RiskTypes::ConnectionState state) { // - 영상 상태 신호 연결: 연결 상태 변경 시 이벤트 처리
                                     m_videoConnectionState = state;
                                     emit videoConnectionStateChanged();
                                 });
    m_onvifConnection = connect(source, &IVideoSource::onvifMetadataReceived, m_onvifParser,
                                 &OnvifBBoxParser::processMetadata);       // - ONVIF 데이터 신호 연결: 메타데이터 파서로 전달
    qCDebug(lcActiveCamera) << "attachVideoConnection camera:" << m_activeCameraId
                            << "onvif connection valid:" << bool(m_onvifConnection); // - 디버그 로그: 영상 연결 상태 기록
    emit videoConnectionStateChanged();                                    // - 상태 변경 신호 발생: 영상 연결 상태 변경 알림
}

void ActiveCameraController::handlePersonDetected(const BBox &bbox)
{
    qCDebug(lcActiveCamera) << "handlePersonDetected valid:" << bbox.isValid() << "x:" << bbox.x()
                            << "y:" << bbox.y() << "w:" << bbox.width() << "h:" << bbox.height(); // - 디버그 로그: 사람 검출 정보 기록
    m_onvifActive = true;                                                  // - ONVIF 활성 상태 설정: 사람 감지 수신 표시
    if (m_onvifPersonBBox == bbox)                                         // - 중복 변경 방지: 좌표 변경 없는 경우 생략
        return;
    m_onvifPersonBBox = bbox;                                              // - 영역 상자 갱신: 사람 위치 좌표 저장
    emit metadataChanged();                                                // - 메타데이터 변경 신호 발생: 화면 영역 알림
}

void ActiveCameraController::handleWarningDeviceStateChanged()
{
    if (!m_warningDevice)
        return;

    if (m_estopActive != m_warningDevice->estopActive()) {                 // - 비상정지 상태 갱신: 값이 바뀐 경우에만 신호 발생
        m_estopActive = m_warningDevice->estopActive();
        emit estopActiveChanged();
    }
    if (m_movementCutoffActive != m_warningDevice->movementCutoffActive()) { // - 전진 차단 상태 갱신: 값이 바뀐 경우에만 신호 발생
        m_movementCutoffActive = m_warningDevice->movementCutoffActive();
        emit movementCutoffActiveChanged();
    }
    if (m_fpgaConnectionState != m_warningDevice->fpgaConnectionState()) { // - FPGA 연결 상태 갱신: 값이 바뀐 경우에만 신호 발생
        m_fpgaConnectionState = m_warningDevice->fpgaConnectionState();
        emit fpgaConnectionStateChanged();
    }

    const bool errorLatched = m_warningDevice->checksumErrorLatched() || m_warningDevice->protocolErrorLatched()
        || m_warningDevice->timeoutErrorLatched();                        // - 3개 누적 플래그를 하나로 합산 (CLEAR_ERROR 전까지 유지)
    if (m_fpgaErrorLatched != errorLatched) {
        m_fpgaErrorLatched = errorLatched;
        emit fpgaErrorLatchedChanged();
    }
}

void ActiveCameraController::handleMetadataUpdated(const RiskMetadata &metadata)
{
    if (metadata.cameraId() != m_activeCameraId)                          // - 카메라 ID 검증: 현재 활성 카메라 데이터가 아닌 경우 무시
        return;

    m_latest = metadata;                                                   // - 최신 위험 데이터 갱신: 수신된 메타데이터 저장
    if (m_warningDevice)                                                   // - 경고 장치 갱신: 수신된 위험 수치 전달
        m_warningDevice->setRiskLevel(metadata.riskLevel());
    emit metadataChanged();                                                // - 메타데이터 변경 신호 발생: UI 표시 정보 알림
}
