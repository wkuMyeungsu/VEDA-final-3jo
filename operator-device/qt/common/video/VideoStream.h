#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QImage>
#include <QQuickPaintedItem>
#include <qqmlintegration.h>

#include "../models/Types.h"

class IVideoSource;

// QML에서 직접 쓰는 영상 출력 화면 (QML_ELEMENT)
// - cameraId만 지정하면 알맞은 IVideoSource를 자동으로 찾아 연결
// - 카메라 전환/연결 끊김 중엔 마지막 프레임을 계속 보여줌 (화면 안 꺼짐)
// - 그 위에 반투명 "전환 중..." 표시로 최신 상태 아님을 알림
class VideoStream : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString cameraId READ cameraId WRITE setCameraId NOTIFY cameraIdChanged)
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(qreal fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(bool switching READ isSwitching NOTIFY switchingChanged)
    Q_PROPERTY(QColor placeholderColor READ placeholderColor WRITE setPlaceholderColor NOTIFY placeholderColorChanged)

public:
    explicit VideoStream(QQuickItem *parent = nullptr);

    // QQuickPaintedItem이 매 update()마다 호출 -- 실제 그리기는 여기서 다 함
    void paint(QPainter *painter) override;

    QString cameraId() const { return m_cameraId; }
    void setCameraId(const QString &id);

    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }
    // 프레임을 아직 한 번도 못 받았으면 임시로 16:9 가정 (DetectionOverlay 등이
    // videoSize=0일 때 나누기 오류/이상한 비율로 그리는 걸 방지)
    QSize videoSize() const { return m_lastFrame.isNull() ? QSize(16, 9) : m_lastFrame.size(); }
    qreal fps() const { return m_fps; }
    bool isSwitching() const { return m_switching; }

    QColor placeholderColor() const { return m_placeholderColor; }
    void setPlaceholderColor(const QColor &color);

signals:
    void cameraIdChanged();
    void connectionStateChanged();
    void videoSizeChanged();
    void fpsChanged();
    void switchingChanged();
    void placeholderColorChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private slots:
    // IVideoSource가 새 프레임을 낼 때마다 연결됨 (frameReady 시그널)
    void handleFrame(const QImage &frame);
    // IVideoSource의 연결 상태가 바뀔 때마다 연결됨
    void handleConnectionStateChanged(RiskTypes::ConnectionState state);

private:
    // cameraId에 맞는 IVideoSource를 찾아 신호 연결 (frameReady, connectionStateChanged)
    void attachToSource();
    // 카메라 전환 전, 이전 소스와의 연결을 끊음 (안 끊으면 이전 카메라 프레임이 계속 들어옴)
    void detachFromSource();
    // switching 프로퍼티 setter -- 값 바뀔 때만 signal emit
    void setSwitching(bool switching);
    // 최근 1초간 프레임 개수를 세서 fps 계산 (매 프레임마다 계산 안 하고 1초 단위로만 갱신)
    void updateFpsCounter();
    // 프레임을 아직 못 받았을 때(연결 전/끊김) 대신 그리는 화면 (사선 패턴 + "NO SIGNAL")
    void paintPlaceholder(QPainter *painter, const QRectF &bounds) const;

    QString m_cameraId;
    IVideoSource *m_source = nullptr;
    QImage m_lastFrame;       // 카메라 전환/끊김 중에도 계속 화면에 남아있는 마지막 프레임
    QSize m_lastFrameSize;
    bool m_switching = false;
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
    QColor m_placeholderColor{0x10, 0x14, 0x1f};

    QElapsedTimer m_fpsClock;
    int m_frameCountWindow = 0;  // 이번 1초 구간 안에서 지금까지 센 프레임 수
    qreal m_fps = 0.0;
};
