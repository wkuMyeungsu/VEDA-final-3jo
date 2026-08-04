#include "MockVideoSource.h"

#include <QDateTime>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

namespace {
constexpr int kFrameIntervalMs = 66; // 약 15fps -- 가짜 화면이니 이 정도면 충분
}

MockVideoSource::MockVideoSource(QString cameraId, QString zone, QObject *parent)
    : IVideoSource(parent)
    , m_cameraId(std::move(cameraId))
    , m_zone(std::move(zone))
{
    // 타이머는 여기서 연결만 해두고, 실제 시작은 start()에서
    m_frameTimer.setInterval(kFrameIntervalMs);
    connect(&m_frameTimer, &QTimer::timeout, this, &MockVideoSource::emitFrame);
}

void MockVideoSource::start()
{
    // 마커 애니메이션 시계는 한 번만 시작 (start/stop 반복해도 시간이 안 리셋되게)
    if (!m_clock.isValid())
        m_clock.start();

    // "뽑아둔" 상태(m_simulatedConnected=false)면 타이머 안 돌림
    setConnectionState(m_simulatedConnected ? RiskTypes::ConnectionState::Connected
                                             : RiskTypes::ConnectionState::Disconnected);
    if (m_simulatedConnected && !m_frameTimer.isActive()) {
        emitFrame(); // 타이머 첫 tick까지 안 기다리고 첫 프레임을 바로 내보냄
        m_frameTimer.start();
    }
}

void MockVideoSource::stop()
{
    m_frameTimer.stop();
    setConnectionState(RiskTypes::ConnectionState::Disconnected);
}

void MockVideoSource::setSimulatedConnected(bool connected)
{
    if (m_simulatedConnected == connected)
        return;
    m_simulatedConnected = connected;

    if (connected) {
        setConnectionState(RiskTypes::ConnectionState::Connected);
        if (!m_frameTimer.isActive()) {
            emitFrame();
            m_frameTimer.start();
        }
    } else {
        m_frameTimer.stop();
        setConnectionState(RiskTypes::ConnectionState::Disconnected);
    }
}

// 타이머 tick마다 호출 -- 프레임 1장을 새로 그려서 내보냄
void MockVideoSource::emitFrame()
{
    emit frameReady(renderFrame());
}

// 그라디언트 배경 + 줄무늬 + 움직이는 마커 + 텍스트(camera_id/zone/시각)를 매번 새로 그림
QImage MockVideoSource::renderFrame() const
{
    QImage image(m_frameSize, QImage::Format_RGB32);

    const double t = m_clock.isValid() ? m_clock.elapsed() / 1000.0 : 0.0;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF bounds(0, 0, m_frameSize.width(), m_frameSize.height());

    QLinearGradient gradient(bounds.topLeft(), bounds.bottomRight());
    gradient.setColorAt(0.0, QColor(0x14, 0x1b, 0x2c));
    gradient.setColorAt(1.0, QColor(0x0a, 0x0f, 0x1a));
    painter.fillRect(bounds, gradient);

    // 은은한 대각선 줄무늬 -- 화면이 그냥 멈춘/깨진 게 아니라 "의도된 테스트 패턴"임을 표시
    painter.setPen(QPen(QColor(255, 255, 255, 14), 1));
    for (int x = -m_frameSize.height(); x < m_frameSize.width(); x += 36)
        painter.drawLine(x, m_frameSize.height(), x + m_frameSize.height(), 0);

    // 움직이는 점(마커) -- "살아있는" 피드임을 보여주는 용도
    const double markerX = bounds.width() * 0.5 + qCos(t * 0.6) * bounds.width() * 0.32;
    const double markerY = bounds.height() * 0.55 + qSin(t * 0.9) * bounds.height() * 0.22;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0x4f, 0xc3, 0xf7, 200));
    painter.drawEllipse(QPointF(markerX, markerY), 14, 14);

    painter.setPen(QColor(255, 255, 255, 40));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(bounds.adjusted(1, 1, -1, -1));

    QFont idFont = painter.font();
    idFont.setPointSize(22);
    idFont.setBold(true);
    painter.setFont(idFont);
    painter.setPen(QColor(255, 255, 255, 235));
    painter.drawText(QRectF(20, 14, bounds.width() - 40, 36), Qt::AlignLeft | Qt::AlignVCenter, m_cameraId);

    QFont zoneFont = painter.font();
    zoneFont.setPointSize(12);
    zoneFont.setBold(false);
    painter.setFont(zoneFont);
    painter.setPen(QColor(255, 255, 255, 160));
    painter.drawText(QRectF(20, 48, bounds.width() - 40, 22), Qt::AlignLeft | Qt::AlignVCenter, m_zone);

    painter.setPen(QColor(255, 255, 255, 90));
    painter.drawText(bounds.adjusted(0, 0, -16, -12), Qt::AlignRight | Qt::AlignBottom,
                      QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")));

    painter.setPen(QColor(255, 255, 255, 60));
    painter.drawText(bounds.adjusted(16, 0, 0, -12), Qt::AlignLeft | Qt::AlignBottom,
                      QStringLiteral("MOCK FEED"));

    painter.end();
    return image;
}
