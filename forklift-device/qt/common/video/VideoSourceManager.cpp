#include "VideoSourceManager.h"

#include <QLoggingCategory>
#include <QTimer>

#include "LocalFileVideoSource.h"
#include "MockVideoSource.h"
#include "RtspVideoSource.h"

namespace {
Q_LOGGING_CATEGORY(lcVideoManager, "safety.video.manager")                    // - 로깅 카테고리 정의: 영상 소스 관리 로그 분류용 이름 지정
VideoSourceManager *g_instance = nullptr;                                      // - 전역 인스턴스 포인터: 싱글톤 인스턴스 주소 보관
}

VideoSourceManager::VideoSourceManager(QObject *parent)
    : QObject(parent)
{
    Q_ASSERT_X(g_instance == nullptr, "VideoSourceManager", "only one instance is supported per process"); // - 중복 생성 검증: 프로세스당 단일 인스턴스 생성 보장
    g_instance = this;                                                         // - 인스턴스 등록: 현재 객체 주소를 전역 포인터에 저장
}

VideoSourceManager::~VideoSourceManager()
{
    clearPendingStops();                                                       // - 정지 예약 정리: 소멸 중에 타이머가 죽은 소스를 건드리지 않게 먼저 제거
    if (g_instance == this)                                                    // - 인스턴스 해제: 소멸 시 전역 포인터 초기화
        g_instance = nullptr;
}

VideoSourceManager *VideoSourceManager::instance()
{
    return g_instance;                                                         // - 인스턴스 조회: 전역 인스턴스 포인터 반환
}

void VideoSourceManager::setCameras(const QVector<CameraInfo> &cameras)
{
    clearPendingStops();                                                       // - 정지 예약 정리: 소스를 지우기 전에 먼저 없애야 타이머가 사라진 소스를 건드리지 않음
    qDeleteAll(m_sources);                                                     // - 기존 소스 정리: 이전 영상 소스 메모리 전체 해제
    m_sources.clear();                                                         // - 맵 초기화: 영상 소스 저장용 맵 비우기

    for (const CameraInfo &info : cameras) {                                   // - 카메라 목록 순회: 설정별 영상 소스 객체 생성
        if (IVideoSource *source = createSource(info))                         // - 소스 생성 검증: 생성된 영상 소스 존재 시 맵에 추가
            m_sources.insert(info.cameraId, source);                           // - 소스 저장: 카메라 ID를 키로 영상 소스 객체 보관
    }
}

void VideoSourceManager::startAll()
{
    for (IVideoSource *source : std::as_const(m_sources))                      // - 전체 소스 순회: 등록된 모든 영상 소스 구동
        source->start();                                                       // - 재생 시작: 각 영상 소스 start() 호출
}

void VideoSourceManager::stopAll()
{
    for (IVideoSource *source : std::as_const(m_sources))                      // - 전체 소스 순회: 등록된 모든 영상 소스 정지
        source->stop();                                                        // - 재생 정지: 각 영상 소스 stop() 호출
}

void VideoSourceManager::requestStart(const QString &cameraId)
{
    cancelPendingStop(cameraId);                                               // - 정지 예약 취소: 끄려던 카메라를 다시 쓰게 됐으므로 예약 해제

    IVideoSource *source = m_sources.value(cameraId, nullptr);                 // - 소스 조회: 대상 카메라의 영상 소스 획득
    if (!source)                                                               // - 소스 유효성 검증: 없는 카메라면 아무것도 하지 않음
        return;

    source->start();                                                           // - 재생 시작: 이미 돌고 있으면 소스 쪽에서 알아서 무시함
}

void VideoSourceManager::requestStopAfter(const QString &cameraId, int delayMs)
{
    if (!m_sources.contains(cameraId))                                         // - 소스 유효성 검증: 없는 카메라면 예약하지 않음
        return;
    if (m_stopTimers.contains(cameraId))                                       // - 중복 예약 방지: 이미 예약돼 있으면 그대로 둠
        return;

    QTimer *timer = new QTimer(this);                                          // - 타이머 생성: 부모를 지정해 메인 스레드 소속 및 자동 정리 보장
    timer->setSingleShot(true);                                                // - 1회 발화 설정: 예약은 한 번만 실행
    timer->setInterval(delayMs);                                               // - 대기 시간 설정: 요청받은 유예 시간 적용
    connect(timer, &QTimer::timeout, this, [this, cameraId]() {                // - 발화 처리: 유예 시간이 지나면 실제로 정지
        if (QTimer *fired = m_stopTimers.take(cameraId)) {                     // - 예약 제거: 맵에서 빼고 타이머 객체 정리 예약
            fired->deleteLater();
        }
        if (IVideoSource *source = m_sources.value(cameraId, nullptr))         // - 소스 조회: 그 사이 소스가 바뀌었을 수 있으므로 다시 확인
            source->stop();                                                    // - 재생 정지: 실제 스트림 종료
    });
    m_stopTimers.insert(cameraId, timer);                                      // - 예약 보관: 카메라 ID를 키로 타이머 저장
    timer->start();                                                            // - 타이머 구동: 유예 시간 카운트 시작
}

void VideoSourceManager::cancelPendingStop(const QString &cameraId)
{
    if (QTimer *timer = m_stopTimers.take(cameraId)) {                         // - 예약 조회 및 제거: 해당 카메라의 타이머를 맵에서 빼기
        timer->stop();                                                         // - 타이머 정지: 남은 시간 취소
        timer->deleteLater();                                                  // - 객체 정리: 이벤트 루프에 삭제 위임
    }
}

void VideoSourceManager::clearPendingStops()
{
    for (QTimer *timer : std::as_const(m_stopTimers)) {                        // - 예약 순회: 보유 중인 모든 지연 정지 타이머 처리
        timer->stop();                                                         // - 타이머 정지: 남은 예약 취소 (이 시점 이후로는 발화하지 않음)
        timer->deleteLater();                                                  // - 객체 정리: 이벤트 루프에 삭제 위임 (소멸 중이면 부모가 대신 정리)
    }
    m_stopTimers.clear();                                                      // - 맵 초기화: 예약 저장소 비우기
}

IVideoSource *VideoSourceManager::sourceFor(const QString &cameraId) const
{
    return m_sources.value(cameraId, nullptr);                                 // - 소스 조회: 카메라 ID에 해당하는 영상 소스 포인터 반환
}

IVideoSource *VideoSourceManager::createSource(const CameraInfo &info)
{
    switch (info.sourceType) {                                                 // - 소스 유형 분류: 설정 타입에 따른 객체 동적 생성
    case VideoSourceType::Mock:                                                // - 가상 소스 처리: MockVideoSource 객체 생성
        return new MockVideoSource(info.cameraId, info.zone, this);
    case VideoSourceType::LocalFile:                                           // - 파일 소스 처리: LocalFileVideoSource 객체 생성
        return new LocalFileVideoSource(info.cameraId, QUrl::fromLocalFile(info.localFilePath), this);
    case VideoSourceType::Rtsp:                                                // - RTSP 소스 처리: RtspVideoSource 객체 생성
        return new RtspVideoSource(info.cameraId, QUrl(info.rtspUrl), this);
    }

    qCWarning(lcVideoManager) << "unknown source type for camera" << info.cameraId << "-- falling back to mock"; // - 경고 로그: 정의되지 않은 유형 예외 처리
    return new MockVideoSource(info.cameraId, info.zone, this);                // - 예외 대체: 기본값으로 MockVideoSource 생성 및 반환
}
