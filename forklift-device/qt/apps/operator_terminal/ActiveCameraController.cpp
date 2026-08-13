#include "ActiveCameraController.h"

#include "network/IWarningDevice.h"
#include "services/MetadataDistributor.h"
#include "video/IVideoSource.h"
#include "video/VideoSourceManager.h"

#include <QLoggingCategory>
#include <QTimer>
namespace {
Q_LOGGING_CATEGORY(lcActiveCamera, "safety.activecamera")                  // - 로깅 카테고리 정의: 활성 카메라 제어 로그 분류용 이름 지정

constexpr int kCameraLingerMs = 15000;                                     // - 이전 카메라 유예 시간: 바로 끄지 않고 이만큼 남겨둠 (파이 실측 오픈 1.1~1.5초라 되돌아올 때 재접속 비용이 커서 길게 잡음)

// 마감 시간이 없으면 죽은 카메라로 전환할 때 이전 카메라의 멈춘 화면에 영원히 머물고,
// 운전자는 그걸 지금 영상이라고 믿게 됨. 차라리 신호 없음을 보여주는 편이 안전.
constexpr int kSwapDeadlineMs = 3000;                                      // - 첫 프레임 대기 한도: 이 시간이 지나면 프레임이 없어도 전환 확정 (파이 실측 최대 1514ms의 두 배로 여유를 둠)
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

    m_swapDeadlineTimer = new QTimer(this);                                // - 마감 타이머 생성: 부모를 지정해 메인 스레드 소속 및 자동 정리 보장
    m_swapDeadlineTimer->setSingleShot(true);                              // - 1회 발화 설정: 전환 1건당 한 번만 실행
    m_swapDeadlineTimer->setInterval(kSwapDeadlineMs);                     // - 대기 한도 설정: 첫 프레임 최대 대기 시간 적용
    connect(m_swapDeadlineTimer, &QTimer::timeout, this, [this]() {        // - 발화 처리: 첫 프레임이 안 와도 전환을 확정
        if (m_pendingCameraId.isEmpty())                                   // - 대기 여부 확인: 이미 확정됐으면 할 일 없음
            return;
        const QString cameraId = m_pendingCameraId;                        // - 대기 ID 복사: 아래에서 대기 상태가 지워지므로 미리 보관
        qCWarning(lcActiveCamera) << "camera" << cameraId << "sent no frame within" << kSwapDeadlineMs
                                   << "ms -- switching anyway";            // - 경고 로그: 프레임 없이 전환됐음을 기록
        commitCameraSwap(cameraId);                                        // - 전환 확정: 화면을 새 카메라로 넘김
    });
}

// 화면을 바로 바꾸지 않고, 새 카메라의 첫 프레임이 온 뒤에 바꾼다.
// 먼저 바꾸면 이름/구역/위험 정보와 검출 박스만 새 카메라 값이 되고 영상은 아직 이전 것이라
// "이전 카메라 영상 위에 다른 카메라의 사람 박스"가 그려짐.
void ActiveCameraController::setActiveCameraId(const QString &cameraId)
{
    if (m_pendingCameraId.isEmpty() && m_activeCameraId == cameraId)       // - 중복 전환 방지: 이미 보고 있고 기다리는 것도 없으면 생략
        return;
    if (m_pendingCameraId == cameraId)                                     // - 중복 요청 방지: 같은 카메라를 이미 기다리는 중이면 생략
        return;

    const QString abandonedCameraId = m_pendingCameraId;                   // - 포기 대상 기억: 기다리다 취소하는 카메라 ID 보관
    cancelPendingSwap();                                                   // - 대기 상태 정리: 이전 요청의 연결과 타이머 해제

    if (!abandonedCameraId.isEmpty() && abandonedCameraId != m_activeCameraId && m_videoManager) // - 포기 카메라 반납: 켜둔 채 방치되지 않게 정지 예약
        m_videoManager->requestStopAfter(abandonedCameraId, kCameraLingerMs);

    if (cameraId == m_activeCameraId) {                                    // - 원위치 처리: 기다리던 전환만 취소되고 화면은 그대로인 경우
        if (m_videoManager)                                                // - 정지 예약 해제: 혹시 예약돼 있었다면 되살림
            m_videoManager->requestStart(cameraId);
        return;
    }

    m_pendingCameraId = cameraId;                                          // - 대기 카메라 지정: 첫 프레임을 기다릴 대상 저장

    IVideoSource *source = m_videoManager ? m_videoManager->sourceFor(cameraId) : nullptr; // - 소스 조회: 새 카메라의 영상 소스 획득

    if (m_activeCameraId.isEmpty() || !source) {                           // - 즉시 확정 조건: 지킬 이전 화면이 없거나 기다릴 소스가 없는 경우
        if (source && m_videoManager)                                      // - 재생 시작: 소스가 있으면 먼저 구동
            m_videoManager->requestStart(cameraId);
        commitCameraSwap(cameraId);                                        // - 전환 확정: 기다릴 이유가 없으므로 바로 반영
        return;
    }

    m_pendingFrameConnection = connect(source, &IVideoSource::frameReady, this, [this, cameraId]() { // - 첫 프레임 대기 연결: 새 카메라 화면이 준비되면 전환
        if (m_pendingCameraId != cameraId)                                 // - 대상 확인: 그 사이 다른 카메라로 바뀌었으면 무시
            return;
        commitCameraSwap(cameraId);                                        // - 전환 확정: 첫 프레임 도착 시 화면 넘김
    });                                                                    // - 연결이 먼저: Mock 소스는 start() 안에서 첫 프레임을 바로 내보내므로 순서가 반대면 그 프레임을 놓침

    m_swapDeadlineTimer->start();                                          // - 마감 타이머 구동: 프레임이 안 와도 일정 시간 뒤 전환
    m_videoManager->requestStart(cameraId);                                // - 재생 시작: 새 카메라 스트림 구동 (필요할 때만 켜는 방식)
}

void ActiveCameraController::commitCameraSwap(QString cameraId)            // - ID를 값으로 받음: 아래 정리 과정에서 원본이 사라져도 안전하게 쓰기 위함
{
    cancelPendingSwap();                                                   // - 대기 상태 정리: 첫 프레임 연결과 마감 타이머 해제

    const QString previousCameraId = m_activeCameraId;                     // - 이전 카메라 기억: 아래에서 지연 정지에 사용

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

    if (m_videoManager && !previousCameraId.isEmpty() && previousCameraId != cameraId) // - 이전 카메라 정리: 바로 끄지 않고 잠시 뒤 정지 예약
        m_videoManager->requestStopAfter(previousCameraId, kCameraLingerMs); // - 지연 정지: 곧 되돌아올 경우 재접속 비용을 아끼고, 정지 지연이 전환 경로를 막지 않게 함
}

void ActiveCameraController::cancelPendingSwap()
{
    QObject::disconnect(m_pendingFrameConnection);                         // - 첫 프레임 연결 해제: 대기용 1회성 연결 제거
    m_pendingFrameConnection = QMetaObject::Connection();                  // - 연결 핸들 초기화: 재사용 대비 빈 값으로 설정
    m_pendingCameraId.clear();                                             // - 대기 카메라 초기화: 기다리는 대상 없음으로 표시
    if (m_swapDeadlineTimer)                                               // - 마감 타이머 정지: 남은 대기 시간 취소
        m_swapDeadlineTimer->stop();
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
