#include "CameraInfo.h"

QString videoSourceTypeToString(VideoSourceType type)
{
    switch (type) {
    case VideoSourceType::Mock: return QStringLiteral("mock");                   // - 가상 유형 변환: Mock 타입을 'mock' 문자열로 반환
    case VideoSourceType::LocalFile: return QStringLiteral("local_file");         // - 파일 유형 변환: LocalFile 타입을 'local_file' 문자열로 반환
    case VideoSourceType::Rtsp: return QStringLiteral("rtsp");                   // - 스트림 유형 변환: Rtsp 타입을 'rtsp' 문자열로 반환
    }
    return QStringLiteral("mock");                                              // - 기본값 반환: 알 수 없는 타입인 경우 'mock' 반환
}

VideoSourceType videoSourceTypeFromString(const QString &value)
{
    if (value == QStringLiteral("local_file")) return VideoSourceType::LocalFile; // - 파일 유형 비교: 'local_file' 문자인 경우 LocalFile 타입 반환
    if (value == QStringLiteral("rtsp")) return VideoSourceType::Rtsp;          // - 스트림 유형 비교: 'rtsp' 문자인 경우 Rtsp 타입 반환
    return VideoSourceType::Mock;                                               // - 기본값 반환: 그 외 문자열인 경우 Mock 타입 반환
}
