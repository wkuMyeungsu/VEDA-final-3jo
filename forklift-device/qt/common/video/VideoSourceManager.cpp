#include "VideoSourceManager.h"

#include <QLoggingCategory>

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
    if (g_instance == this)                                                    // - 인스턴스 해제: 소멸 시 전역 포인터 초기화
        g_instance = nullptr;
}

VideoSourceManager *VideoSourceManager::instance()
{
    return g_instance;                                                         // - 인스턴스 조회: 전역 인스턴스 포인터 반환
}

void VideoSourceManager::setCameras(const QVector<CameraInfo> &cameras)
{
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
