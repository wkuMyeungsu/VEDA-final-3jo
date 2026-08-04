#include "VideoStream.h"

#include <QPainter>

#include "AspectFit.h"
#include "IVideoSource.h"
#include "VideoSourceManager.h"

VideoStream::VideoStream(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    // 배경은 paint()에서 직접 채우므로 기본 fillColor는 투명 처리
    setFillColor(Qt::transparent);
    setAntialiasing(true);
}

void VideoStream::setCameraId(const QString &id)
{
    if (m_cameraId == id)
        return;

    detachFromSource();
    m_cameraId = id;
    // m_lastFrame은 일부러 안 지움 -- 전환 중에도 이전 화면 유지
    // 대신 "전환 중" 표시로 최신 아님을 알림
    if (!m_lastFrame.isNull())
        setSwitching(true);
    attachToSource();

    emit cameraIdChanged();
    update();
}

// QML에서 카메라별 배경색을 다르게 주고 싶을 때 사용 (기본값은 헤더의 짙은 남색)
void VideoStream::setPlaceholderColor(const QColor &color)
{
    if (m_placeholderColor == color)
        return;
    m_placeholderColor = color;
    emit placeholderColorChanged();
    update();
}

void VideoStream::attachToSource()
{
    if (m_cameraId.isEmpty())
        return;

    // VideoSourceManager가 이미 만들어둔 소스를 "빌려서" 씀 (여기서 새로 안 만듦)
    VideoSourceManager *manager = VideoSourceManager::instance();
    m_source = manager ? manager->sourceFor(m_cameraId) : nullptr;

    if (!m_source) {
        handleConnectionStateChanged(RiskTypes::ConnectionState::Disconnected);
        return;
    }

    // Qt::UniqueConnection: 혹시 같은 소스에 실수로 두 번 연결해도 중복 등록 안 되게 방어
    connect(m_source, &IVideoSource::frameReady, this, &VideoStream::handleFrame, Qt::UniqueConnection);
    connect(m_source, &IVideoSource::connectionStateChanged, this,
            &VideoStream::handleConnectionStateChanged, Qt::UniqueConnection);
    // connect는 "이후 변화"만 알려주므로, 지금 이미 어떤 상태인지 한 번 수동 반영
    handleConnectionStateChanged(m_source->connectionState());
}

void VideoStream::detachFromSource()
{
    // 이전 소스와의 연결을 끊음 -- 안 그러면 카메라 전환 후에도 이전 카메라의
    // frameReady가 계속 이 VideoStream으로 들어와서 엉뚱한 영상이 섞여 보임
    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);
    m_source = nullptr;
}

// true/false 바뀔 때만 emit -- paint()가 이 값 보고 "전환 중..." 오버레이를 그림
void VideoStream::setSwitching(bool switching)
{
    if (m_switching == switching)
        return;
    m_switching = switching;
    emit switchingChanged();
}

void VideoStream::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    update();
}

// 새 프레임 1장이 도착할 때마다 호출 (Mock은 타이머로, RTSP는 GStreamer 콜백으로 emit)
void VideoStream::handleFrame(const QImage &frame)
{
    if (frame.isNull())
        return;

    m_lastFrame = frame;
    setSwitching(false); // 새 프레임이 왔으니 "전환 중" 표시를 내림
    updateFpsCounter();

    // 해상도가 바뀐 경우에만 알림 (매 프레임마다 emit하면 낭비 -- 보통 해상도는 안 바뀜)
    if (m_lastFrameSize != frame.size()) {
        m_lastFrameSize = frame.size();
        emit videoSizeChanged();
    }

    update(); // QQuickPaintedItem에게 "다시 그려라" 요청 -> paint() 호출됨
}

// IVideoSource의 상태 변화를 받아 그대로 반영 (연결/재연결/끊김 등)
void VideoStream::handleConnectionStateChanged(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged();
    update();
}

// 1초 단위로만 fps 갱신 (매 프레임 계산하면 숫자가 들쭉날쭉함)
void VideoStream::updateFpsCounter()
{
    if (!m_fpsClock.isValid()) {
        m_fpsClock.start();
        m_frameCountWindow = 0;
        return;
    }

    ++m_frameCountWindow;
    const qint64 elapsed = m_fpsClock.elapsed();
    if (elapsed >= 1000) {
        m_fps = m_frameCountWindow * 1000.0 / elapsed;
        m_frameCountWindow = 0;
        m_fpsClock.restart();
        emit fpsChanged();
    }
}

// update()가 호출될 때마다(새 프레임/크기 변경/전환 상태 변경 등) Qt가 이 함수를 실행
void VideoStream::paint(QPainter *painter)
{
    const QRectF bounds(0, 0, width(), height());
    if (bounds.isEmpty())
        return;

    painter->fillRect(bounds, m_placeholderColor);

    if (!m_lastFrame.isNull()) {
        // AspectFit으로 비율 유지 -- DetectionOverlay도 똑같은 계산을 다시 해서
        // bbox 위치를 이 영상 위치와 맞춤 (AspectFit.h 주석 참고)
        const QRectF videoRect = AspectFit::fitRect(bounds.size(), m_lastFrame.size());
        painter->drawImage(videoRect, m_lastFrame);
    } else {
        // 프레임이 한 번도 안 왔음(연결 전/끊김) -- placeholder 화면
        paintPlaceholder(painter, bounds);
    }

    // 카메라 전환 중이면 마지막 프레임 위에 반투명 검은 막 + "전환 중..." 텍스트를 덧그림
    if (m_switching) {
        painter->fillRect(bounds, QColor(0, 0, 0, 110));
        QFont font = painter->font();
        font.setPointSize(12);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(QColor(255, 255, 255, 230));
        painter->drawText(bounds, Qt::AlignCenter, QStringLiteral("전환 중..."));
    }
}

// 사선 줄무늬 배경 + 가운데 카메라 ID(또는 "NO SIGNAL") + 그 아래 작은 "NO SIGNAL"
void VideoStream::paintPlaceholder(QPainter *painter, const QRectF &bounds) const
{
    painter->setPen(QPen(QColor(255, 255, 255, 18), 1));
    for (int x = -static_cast<int>(bounds.height()); x < static_cast<int>(bounds.width()); x += 28)
        painter->drawLine(QPointF(x, bounds.height()), QPointF(x + bounds.height(), 0));

    QFont font = painter->font();
    font.setPointSize(14);
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(QColor(255, 255, 255, 150));
    painter->drawText(bounds.adjusted(0, -10, 0, -10), Qt::AlignCenter,
                       m_cameraId.isEmpty() ? QStringLiteral("NO SIGNAL") : m_cameraId);

    QFont subFont = painter->font();
    subFont.setPointSize(10);
    subFont.setBold(false);
    painter->setFont(subFont);
    painter->setPen(QColor(255, 255, 255, 90));
    painter->drawText(bounds.adjusted(0, 16, 0, 16), Qt::AlignCenter, QStringLiteral("NO SIGNAL"));
}
