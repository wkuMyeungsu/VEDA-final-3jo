#pragma once

#include <QElapsedTimer>
#include <QTimer>

#include "IVideoSource.h"

// 실제 카메라 없이 화면을 채우는 가짜 영상 소스
// - 그라디언트 배경 + 움직이는 마커 + camera_id/zone/시각을 프레임에 그려 넣음
// - source_type을 "rtsp" 등으로 바꾸기 전까지 모든 카메라가 기본으로 씀
class MockVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    MockVideoSource(QString cameraId, QString zone, QObject *parent = nullptr);

    // 연결 상태를 Connected로 바꾸고 프레임 타이머 시작
    void start() override;
    // 프레임 타이머 정지, 연결 상태를 Disconnected로 바꿈
    void stop() override;
    // 데모 전용: 실제 네트워크 연결과 무관하게 "연결 끊김"을 흉내냄 (IVideoSource 참고)
    void setSimulatedConnected(bool connected) override;

private slots:
    // 타이머 주기마다 호출 -- 새 프레임 1장을 그려서 내보냄
    void emitFrame();

private:
    // 매번 새로 그려서 반환 (녹화된 영상이 아니라 매 호출마다 실시간 렌더링)
    QImage renderFrame() const;

    QString m_cameraId;
    QString m_zone;
    QTimer m_frameTimer;
    QElapsedTimer m_clock;   // renderFrame()의 마커 애니메이션 진행 시간 기준
    QSize m_frameSize{960, 540};
    bool m_simulatedConnected = true;
};
