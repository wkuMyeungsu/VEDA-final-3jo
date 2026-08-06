#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickPaintedItem>
#include <qqmlintegration.h>

#include "../models/Types.h"

class IVideoSource;

// - 비디오 스트림 출력 아이템 클래스 (QML 연동 카메라 화면 출력, 프레임 유지 및 전환 오버레이 표출)
class VideoStream : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT                                                                 // - QML 모듈 내 요소 등록 매크로
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId NOTIFY cameraIdChanged) // - 카메라 ID 속성: 화면에 표출할 카메라 식별자
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged) // - 연결 상태 속성: 영상 소스 접속 상태
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)          // - 영상 크기 속성: 원본 영상 해상도 (기본값 16:9)
    Q_PROPERTY(qreal fps READ fps NOTIFY fpsChanged)                            // - FPS 속성: 초당 프레임 재생 수치
    Q_PROPERTY(bool switching READ isSwitching NOTIFY switchingChanged)         // - 전환 상태 속성: 카메라 전환 진행 여부
    Q_PROPERTY(QColor placeholderColor READ placeholderColor WRITE setPlaceholderColor NOTIFY placeholderColorChanged) // - 배경 색상 속성: 대체 화면 배경색

public:
    explicit VideoStream(QQuickItem *parent = nullptr);                       // - 생성자: 부모 객체 지정 및 화면 아이템 초기화

    void paint(QPainter *painter) override;                                    // - 그리기 함수: 영상 프레임, 대체 화면 및 전환 오버레이 렌더링

    QString cameraId() const { return m_cameraId; }                            // - 카메라 ID 조회: 현재 표시 중인 카메라 ID 반환
    void setCameraId(const QString &id);                                       // - 카메라 ID 설정: 표시 카메라 변경 및 영상 소스 재연결

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; } // - 연결 상태 조회: 현재 영상 소스 접속 상태 반환
    QSize videoSize() const { return m_lastFrame.isNull() ? QSize(16, 9) : m_lastFrame.size(); } // - 영상 크기 조회: 프레임 해상도 반환 (미수신 시 16:9)
    qreal fps() const { return m_fps; }                                        // - FPS 조회: 현재 초당 프레임 수 반환
    bool isSwitching() const { return m_switching; }                           // - 전환 상태 조회: 카메라 전환 진행 상태 반환

    QColor placeholderColor() const { return m_placeholderColor; }             // - 배경 색상 조회: 대체 화면 배경 색상 반환
    void setPlaceholderColor(const QColor &color);                             // - 배경 색상 설정: 대체 화면 배경 색상 변경 및 화면 갱신

signals:
    void cameraIdChanged();                                                    // - 카메라 ID 변경 알림: 표시 카메라 ID 변경 시 발생
    void connectionStateChanged();                                            // - 연결 상태 변경 알림: 영상 접속 상태 변경 시 발생
    void videoSizeChanged();                                                   // - 영상 크기 변경 알림: 영상 해상도 변경 시 발생
    void fpsChanged();                                                         // - FPS 수치 변경 알림: 초당 프레임 수 변경 시 발생
    void switchingChanged();                                                   // - 전환 상태 변경 알림: 카메라 전환 상태 변경 시 발생
    void placeholderColorChanged();                                            // - 배경 색상 변경 알림: 대체 화면 배경색 변경 시 발생

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override; // - 영역 변경 처리: 화면 아이템 크기 변경 시 재그리기 요청

private slots:
    void handleFrame(const QImage &frame);                                     // - 프레임 수신 처리: 새 프레임 수신 시 화면 갱신 및 FPS 산출
    void handleConnectionStateChanged(RiskTypes::ConnectionState state);       // - 연결 상태 변경 처리: 소스 상태 변경에 따른 내역 반영

private:
    void attachToSource();                                                     // - 소스 연결 수립: 카메라 ID에 해당하는 IVideoSource 신호 연결
    void detachFromSource();                                                   // - 소스 연결 해제: 이전 IVideoSource 신호 연결 끊기
    void setSwitching(bool switching);                                        // - 전환 상태 설정: 카메라 전환 상태 플래그 갱신 및 알림
    void updateFpsCounter();                                                   // - FPS 측정: 1초 단위 누적 프레임 카운트 산출
    void paintPlaceholder(QPainter *painter, const QRectF &bounds) const;       // - 대체 화면 그리기: 영상 미수신 시 사선 패턴 및 문구 렌더링

    QString m_cameraId;                                                        // - 카메라 ID: 현재 표출 대상 카메라 식별자 보관
    IVideoSource *m_source = nullptr;                                          // - 영상 소스 포인터: 연결된 IVideoSource 객체 참조
    QImage m_lastFrame;                                                        // - 최근 프레임: 화면 유지를 위한 마지막 수신 이미지 보관
    QSize m_lastFrameSize;                                                     // - 최근 해상도: 이전 프레임 이미지 해상도 보관
    bool m_switching = false;                                                  // - 전환 상태 플래그: 카메라 전환 진행 여부 보관
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected; // - 연결 상태: 영상 소스 접속 상태 보관
    QColor m_placeholderColor{0x10, 0x14, 0x1f};                               // - 배경 색상: 대체 화면 배경 색상 보관

    QElapsedTimer m_fpsClock;                                                  // - FPS 타이머: 초당 프레임 측정용 경과 시간 시계
    int m_frameCountWindow = 0;                                                // - 프레임 카운터: 1초 구간 누적 프레임 수
    qreal m_fps = 0.0;                                                         // - 측정 FPS: 산출된 초당 프레임 수치 보관
};
