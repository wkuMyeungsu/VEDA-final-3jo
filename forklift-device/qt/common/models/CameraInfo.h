#pragma once

#include <QString>

// - 영상 소스 유형 열거형 (카메라 연결 방식 구분)
enum class VideoSourceType {
    Mock,                                                                       // - 가상 영상: 테스트용 시뮬레이션 소스
    LocalFile,                                                                  // - 로컬 파일: 저장된 비디오 파일 소스
    Rtsp                                                                        // - 실시간 스트리밍: RTSP 네트워크 영상 소스
};

QString videoSourceTypeToString(VideoSourceType type);                          // - 문자열 변환: 열거형 타입을 문자열로 변환
VideoSourceType videoSourceTypeFromString(const QString &value);                // - 열거형 변환: 문자열을 열거형 타입으로 변환

// - 카메라 정보 구조체 (cameras.json 데이터 보관)
struct CameraInfo {
    QString streamId;                                                           // - 스트림 식별자: 단일 카메라 다채널 구분용 (예: CAM_01_CH_01)
    QString cameraId;                                                           // - 카메라 식별자: 물리 카메라 구분용 ID (예: CAM_01)
    int channel = 0;                                                            // - 채널 번호: 0, 1, 2, 3
    QString name;                                                               // - 카메라 이름: 화면 표시용 명칭
    QString zone;                                                               // - 설치 구역: 카메라 설치 위치 정보
    VideoSourceType sourceType = VideoSourceType::Mock;                         // - 영상 소스 유형: 연결 방식 (기본값: Mock)
    QString rtspUrl;                                                            // - RTSP 주소: 네트워크 영상 스트리밍 URL
    QString localFilePath;                                                      // - 비디오 파일 경로: 로컬 영상 파일 저장 경로
    int rtspLatencyMs = 100;                                                    // - RTSP 지터버퍼 (ms, 기본값: 100)
    QString rtspProtocols = QStringLiteral("tcp");                             // - RTSP 전송 프로토콜 (tcp / udp, 기본값: tcp)
    QString rtspDecoder = QStringLiteral("avdec_h264");                         // - RTSP 디코더 엘리먼트 (avdec_h264 / v4l2h264dec 등, 기본값: avdec_h264)

    QString effectiveId() const { return streamId.isEmpty() ? cameraId : streamId; }
};

