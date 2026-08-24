#pragma once

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QVector>

#include "models/BBox.h"
#include "models/CameraInfo.h"
#include "models/RiskMetadata.h"
#include "models/Types.h"
#include "network/OnvifBBoxParser.h"

class MetadataDistributor;
class VideoSourceManager;
class IWarningDevice;
class IVideoSource;
class QTimer;                                                                                                     // - 전방 선언: 전환 마감 타이머 타입

// - 활성 카메라 제어 클래스 (현재 화면에 표시할 카메라 선택 및 해당 카메라의 메타데이터/영상 상태 관리)
class ActiveCameraController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString activeCameraId READ activeCameraId WRITE setActiveCameraId NOTIFY activeCameraIdChanged) // - 활성 카메라 ID 속성: 현재 카메라 변경 및 알림
    Q_PROPERTY(QString zone READ zone NOTIFY activeCameraIdChanged)                                              // - 설치 구역 속성: 카메라 설치 위치 정보 전달
    Q_PROPERTY(QString cameraName READ cameraName NOTIFY activeCameraIdChanged)                                        // - 카메라 이름 속성: 화면 표시용 이름 전달
    Q_PROPERTY(int riskLevel READ riskLevel NOTIFY metadataChanged)                                              // - 위험 단계 속성: 현재 위험 수치 전달
    Q_PROPERTY(int exceptionState READ exceptionState NOTIFY metadataChanged)                                        // - 예외 상태 속성: 시스템 상태 정보 전달
    Q_PROPERTY(double distanceM READ distanceM NOTIFY metadataChanged)                                           // - 측정 거리 속성: 감지 대상과의 거리(m) 전달
    Q_PROPERTY(bool distanceValid READ distanceValid NOTIFY metadataChanged)                                     // - 거리 유효성 속성: 거리 측정 가능 여부 전달
    Q_PROPERTY(BBox personBBox READ personBBox NOTIFY metadataChanged)                                           // - 사람 영역 속성: 감지된 사람 위치 좌표 전달
    Q_PROPERTY(BBox forkliftBBox READ forkliftBBox NOTIFY metadataChanged)                                       // - 지게차 영역 속성: 감지된 지게차 위치 좌표 전달
    Q_PROPERTY(
        RiskTypes::ConnectionState videoConnectionState READ videoConnectionState NOTIFY videoConnectionStateChanged) // - 영상 연결 상태 속성: 영상 소스 접속 상태 전달
    Q_PROPERTY(bool estopActive READ estopActive NOTIFY estopActiveChanged)                                          // - 비상정지 속성: FPGA 물리 ESTOP 버튼 눌림 여부
    Q_PROPERTY(bool movementCutoffActive READ movementCutoffActive NOTIFY movementCutoffActiveChanged)               // - 전진 차단 속성: FPGA 전진 릴레이 차단 작동 여부
    Q_PROPERTY(
        RiskTypes::ConnectionState fpgaConnectionState READ fpgaConnectionState NOTIFY fpgaConnectionStateChanged)   // - FPGA 연결 상태 속성: heartbeat 수신 여부 기반
    Q_PROPERTY(bool fpgaErrorLatched READ fpgaErrorLatched NOTIFY fpgaErrorLatchedChanged)                           // - FPGA 오류 누적 속성: checksum/protocol/timeout 중 하나라도 있으면 true
    // - 아래 3개는 fpgaErrorLatched를 구성하는 개별 항목. IWarningDevice가 셋을 항상 한 번에
    //   갱신하므로(setErrorLatches) NOTIFY도 fpgaErrorLatchedChanged를 그대로 공유해 씀.
    Q_PROPERTY(bool checksumErrorLatched READ checksumErrorLatched NOTIFY fpgaErrorLatchedChanged)                   // - 체크섬 오류 누적 속성: CLEAR_ERROR 전까지 유지
    Q_PROPERTY(bool protocolErrorLatched READ protocolErrorLatched NOTIFY fpgaErrorLatchedChanged)                   // - 프로토콜 오류 누적 속성: CLEAR_ERROR 전까지 유지
    Q_PROPERTY(bool timeoutErrorLatched READ timeoutErrorLatched NOTIFY fpgaErrorLatchedChanged)                     // - 타임아웃 오류 누적 속성: CLEAR_ERROR 전까지 유지

public:
    ActiveCameraController(QVector<CameraInfo> cameras, MetadataDistributor *metadataDistributor,
                            VideoSourceManager *videoManager, IWarningDevice *warningDevice,
                            QObject *parent = nullptr);                                                           // - 생성자: 카메라 목록 및 의존 객체 전달받아 초기화

    QString activeCameraId() const { return m_activeCameraId; }                                                   // - 활성 카메라 ID 조회: 현재 표시 중인 카메라 ID 반환
    void setActiveCameraId(const QString &cameraId);                                                              // - 활성 카메라 변경: 화면 표시 카메라 ID 변경 및 관련 데이터 수립

    QString zone() const { return m_zone; }                                                                       // - 설치 구역 조회: 현재 카메라 설치 위치 반환
    QString cameraName() const { return m_cameraName; }                                                           // - 카메라 이름 조회: 현재 카메라 명칭 반환

    int riskLevel() const { return static_cast<int>(m_latest.riskLevel()); }                                      // - 위험 단계 조회: 현재 위험 수치 정수형 반환
    int exceptionState() const { return static_cast<int>(m_latest.exceptionState()); }                            // - 예외 상태 조회: 시스템 예외 상태 정수형 반환
    double distanceM() const { return m_latest.distanceM(); }                                                     // - 측정 거리 조회: 감지 대상과의 거리(m) 반환
    bool distanceValid() const { return m_latest.distanceValid(); }                                               // - 거리 유효성 조회: 거리 측정 유효 여부 반환
    BBox personBBox() const { return m_onvifActive ? m_onvifPersonBBox : m_latest.personBBox(); }                  // - 사람 영역 조회: ONVIF 활성 여부에 따른 사람 좌표 반환
    BBox forkliftBBox() const { return m_latest.forkliftBBox(); }                                                 // - 지게차 영역 조회: 지게차 위치 좌표 반환

    RiskTypes::ConnectionState videoConnectionState() const { return m_videoConnectionState; }                    // - 영상 상태 조회: 현재 영상 소스 연결 상태 반환

    bool estopActive() const { return m_estopActive; }                                                            // - 비상정지 상태 조회: FPGA 물리 ESTOP 버튼 눌림 여부 반환
    bool movementCutoffActive() const { return m_movementCutoffActive; }                                          // - 전진 차단 상태 조회: FPGA 전진 릴레이 차단 작동 여부 반환
    RiskTypes::ConnectionState fpgaConnectionState() const { return m_fpgaConnectionState; }                      // - FPGA 연결 상태 조회: heartbeat 수신 여부 기반 상태 반환
    bool fpgaErrorLatched() const { return m_fpgaErrorLatched; }                                                  // - FPGA 오류 누적 상태 조회: checksum/protocol/timeout 중 하나라도 있으면 true
    bool checksumErrorLatched() const { return m_checksumErrorLatched; }                                          // - 체크섬 오류 누적 상태 조회
    bool protocolErrorLatched() const { return m_protocolErrorLatched; }                                          // - 프로토콜 오류 누적 상태 조회
    bool timeoutErrorLatched() const { return m_timeoutErrorLatched; }                                            // - 타임아웃 오류 누적 상태 조회

    Q_INVOKABLE void clearFpgaError();                                                                            // - FPGA 통신 오류(Sticky 플래그) 해제 명령
    Q_INVOKABLE void runFpgaSelfTest(int mode);                                                                   // - FPGA 자가진단(0=STOP/1=LED/2=BUZZER/3=ALL) 실행 명령


signals:
    void activeCameraIdChanged();                                                                                 // - 카메라 변경 알림: 활성 카메라 ID 변경 시 발생
    void metadataChanged();                                                                                       // - 메타데이터 변경 알림: 위험 수치 또는 영역 좌표 변경 시 발생
    void videoConnectionStateChanged();                                                                           // - 영상 상태 변경 알림: 영상 소스 연결 상태 변경 시 발생
    void estopActiveChanged();                                                                                    // - 비상정지 상태 변경 알림
    void movementCutoffActiveChanged();                                                                           // - 전진 차단 상태 변경 알림
    void fpgaConnectionStateChanged();                                                                            // - FPGA 연결 상태 변경 알림
    void fpgaErrorLatchedChanged();                                                                               // - FPGA 오류 누적 상태 변경 알림

private slots:
    void handleMetadataUpdated(const RiskMetadata &metadata);                                                      // - 데이터 갱신 처리: 위험 메타데이터 수신 이벤트 핸들러
    void handlePersonDetected(const BBox &bbox);                                                                 // - 사람 검출 처리: ONVIF 파서의 사람 감지 이벤트 핸들러
    void handleWarningDeviceStateChanged();                                                                       // - 경고 장치 상태 반영: IWarningDevice의 각종 signal 수신 시 캐시 갱신

private:
    void attachVideoConnection();                                                                                 // - 영상 연결 수립: 새로 선택된 카메라의 영상 및 메타데이터 신호 연결
    void commitCameraSwap(QString cameraId);                                                                      // - 전환 확정: 대기 중이던 카메라를 활성 카메라로 승격 (값으로 받음: 실행 중에 원본 대기 ID가 지워짐)
    void cancelPendingSwap();                                                                                     // - 전환 대기 취소: 첫 프레임 대기 연결과 마감 타이머 해제
    void applyRiskToWarningDevice(const RiskMetadata &metadata);                                                  // - 위험도를 FPGA로 보낼지, 두절이라 멈출지 한 곳에서 판단

    QHash<QString, CameraInfo> m_cameras;                                                                         // - 카메라 정보 맵: 카메라 ID 기준 정보 저장소
    QString m_activeCameraId;                                                                                     // - 활성 카메라 ID: 현재 화면 표시 카메라 ID 보관
    QString m_pendingCameraId;                                                                                    // - 대기 카메라 ID: 첫 프레임을 기다리는 중인 다음 카메라 ID 보관
    QMetaObject::Connection m_pendingFrameConnection;                                                             // - 첫 프레임 신호 연결: 대기 카메라의 frameReady 1회성 연결
    QTimer *m_swapDeadlineTimer = nullptr;                                                                        // - 전환 마감 타이머: 첫 프레임이 안 와도 일정 시간 뒤 강제 전환
    QString m_zone;                                                                                               // - 설치 구역: 현재 카메라 설치 위치 보관
    QString m_cameraName;                                                                                         // - 카메라 이름: 현재 카메라 명칭 보관
    RiskMetadata m_latest;                                                                                        // - 최신 위험 데이터: 수신된 위험 메타데이터 보관

    MetadataDistributor *m_metadataDistributor = nullptr;                                                         // - 데이터 분배기: 전체 카메라 위험 데이터 분배 객체
    VideoSourceManager *m_videoManager = nullptr;                                                                 // - 영상 관리자: 카메라 영상 소스 관리 객체
    IWarningDevice *m_warningDevice = nullptr;                                                                    // - 경고 장치: 경고 수치 전달 객체

    RiskTypes::ConnectionState m_videoConnectionState = RiskTypes::ConnectionState::Disconnected;                // - 영상 연결 상태: 현재 영상 소스 접속 상태 보관
    QMetaObject::Connection m_videoConnection;                                                                    // - 영상 신호 연결: 영상 상태 신호 연결 객체

    bool m_estopActive = false;                                                                                   // - 비상정지 상태 캐시: IWarningDevice 신호로 갱신
    bool m_movementCutoffActive = false;                                                                          // - 전진 차단 상태 캐시: IWarningDevice 신호로 갱신
    RiskTypes::ConnectionState m_fpgaConnectionState = RiskTypes::ConnectionState::Disconnected;                  // - FPGA 연결 상태 캐시: IWarningDevice 신호로 갱신
    bool m_fpgaErrorLatched = false;                                                                              // - FPGA 오류 누적 상태 캐시: IWarningDevice 신호로 갱신
    bool m_checksumErrorLatched = false;                                                                          // - 체크섬 오류 누적 상태 캐시: IWarningDevice 신호로 갱신
    bool m_protocolErrorLatched = false;                                                                          // - 프로토콜 오류 누적 상태 캐시: IWarningDevice 신호로 갱신
    bool m_timeoutErrorLatched = false;                                                                           // - 타임아웃 오류 누적 상태 캐시: IWarningDevice 신호로 갱신

    OnvifBBoxParser *m_onvifParser = nullptr;                                                                     // - ONVIF 파서: 사람 위치 좌표 파싱 객체
    QMetaObject::Connection m_onvifConnection;                                                                    // - ONVIF 신호 연결: 메타데이터 신호 연결 객체
    BBox m_onvifPersonBBox;                                                                                       // - ONVIF 사람 영역: ONVIF 데이터 기준 사람 위치 좌표
    bool m_onvifActive = false;                                                                                   // - ONVIF 활성 상태: ONVIF 데이터 수신 여부 보관
};
