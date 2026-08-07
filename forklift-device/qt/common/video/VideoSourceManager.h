#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "../models/CameraInfo.h"
#include "IVideoSource.h"

// - 영상 소스 관리자 클래스 (카메라 ID별 IVideoSource 객체 보관 및 인스턴스 생성/구동 관리)
class VideoSourceManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoSourceManager(QObject *parent = nullptr);                     // - 생성자: 부모 객체 지정 및 싱글톤 인스턴스 등록
    ~VideoSourceManager() override;                                           // - 소멸자: 보유 중인 영상 소스 객체 전체 정리

    static VideoSourceManager *instance();                                     // - 인스턴스 조회: 프로세스 단일 인스턴스 포인터 반환

    void setCameras(const QVector<CameraInfo> &cameras);                       // - 카메라 설정 등록: 기존 소스 삭제 및 새 카메라 정보 기반 소스 객체 생성
    void startAll();                                                           // - 전체 재생 시작: 등록된 모든 영상 소스의 start() 일괄 호출
    void stopAll();                                                            // - 전체 재생 정지: 등록된 모든 영상 소스의 stop() 일괄 호출

    IVideoSource *sourceFor(const QString &cameraId) const;                     // - 소스 조회: 카메라 ID에 해당하는 영상 소스 객체 포인터 반환

private:
    IVideoSource *createSource(const CameraInfo &info);                         // - 소스 객체 생성: 카메라 소스 유형별(Mock/LocalFile/Rtsp) 객체 동적 생성

    QHash<QString, IVideoSource *> m_sources;                                  // - 영상 소스 맵: 카메라 ID 기준 영상 소스 포인터 배열 보관
};
