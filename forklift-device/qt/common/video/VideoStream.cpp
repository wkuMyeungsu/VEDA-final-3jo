#include "VideoStream.h"

#include <QPainter>

#include "AspectFit.h"
#include "IVideoSource.h"
#include "VideoSourceManager.h"

VideoStream::VideoStream(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setFillColor(Qt::transparent);                                             // - 투명 채우기 설정: 기본 배경 투명 처리
    setAntialiasing(true);                                                      // - 안티앨리어싱 설정: 테두리 부드럽게 표현
}

void VideoStream::setCameraId(const QString &id)
{
    if (m_cameraId == id)                                                      // - 중복 변경 방지: 기존 카메라 ID와 동일한 경우 생략
        return;

    detachFromSource();                                                        // - 소스 해제: 이전 영상 소스와의 연결 해제
    m_cameraId = id;                                                           // - 카메라 ID 갱신: 신규 카메라 식별자 저장
    if (!m_lastFrame.isNull())                                                 // - 프레임 유효성 확인: 이전 프레임 존재 시 전환 표시 설정
        setSwitching(true);
    attachToSource();                                                          // - 소스 연결: 신규 카메라 영상 소스 연결 수립

    emit cameraIdChanged();                                                    // - 신호 발생: 카메라 ID 변경 알림
    update();                                                                  // - 화면 갱신: 비디오 스트림 다시 그리기 요청
}

void VideoStream::setPlaceholderColor(const QColor &color)
{
    if (m_placeholderColor == color)                                           // - 중복 변경 방지: 기존 색상과 동일한 경우 생략
        return;
    m_placeholderColor = color;                                                // - 배경색 갱신: 대체 화면 배경 색상 저장
    emit placeholderColorChanged();                                           // - 신호 발생: 대체 화면 배경색 변경 알림
    update();                                                                  // - 화면 갱신: 비디오 스트림 다시 그리기 요청
}

void VideoStream::attachToSource()
{
    if (m_cameraId.isEmpty())                                                  // - ID 유효성 검증: 카메라 ID가 없는 경우 연결 생략
        return;

    VideoSourceManager *manager = VideoSourceManager::instance();              // - 관리자 참조 획득: 영상 소스 관리자 싱글톤 인스턴스 가져오기
    m_source = manager ? manager->sourceFor(m_cameraId) : nullptr;              // - 소스 조회: 관리자에서 해당 카메라 영상 소스 획득

    if (!m_source) {                                                           // - 소스 유효성 검증: 소스 미존재 시 연결 끊김 처리
        handleConnectionStateChanged(RiskTypes::ConnectionState::Disconnected);
        return;
    }

    if (manager)                                                               // - 재생 보장: 화면이 붙는 시점에 소스를 구동 (첫 표시 카메라가 꺼진 채 남는 것 방지)
        manager->requestStart(m_cameraId);                                     // - 정지 예약도 함께 취소됨 (여러 번 불러도 안전)

    connect(m_source, &IVideoSource::frameReady, this, &VideoStream::handleFrame, Qt::UniqueConnection); // - 프레임 신호 연결: 새 영상 프레임 수신 신호 연결
    connect(m_source, &IVideoSource::connectionStateChanged, this,
            &VideoStream::handleConnectionStateChanged, Qt::UniqueConnection); // - 상태 신호 연결: 연결 상태 변경 신호 연결
    handleConnectionStateChanged(m_source->connectionState());                // - 초기 상태 반영: 현재 영상 소스 접속 상태 즉시 반영
}

void VideoStream::detachFromSource()
{
    if (m_source)                                                              // - 소스 존재 확인: 연결된 소스가 있는 경우 신호 연결 해제
        disconnect(m_source, nullptr, this, nullptr);
    m_source = nullptr;                                                        // - 소스 포인터 초기화: 영상 소스 참조 해제
}

void VideoStream::setSwitching(bool switching)
{
    if (m_switching == switching)                                              // - 중복 변경 방지: 기존 전환 상태와 동일한 경우 생략
        return;
    m_switching = switching;                                                   // - 전환 상태 갱신: 카메라 전환 플래그 저장
    emit switchingChanged();                                                   // - 신호 발생: 카메라 전환 상태 변경 알림
}

void VideoStream::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);              // - 영역 변경 처리: 부모 클래스 위치/크기 변경 함수 호출
    update();                                                                  // - 화면 갱신: 변경된 크기에 맞춰 다시 그리기 요청
}

void VideoStream::handleFrame(const QImage &frame)
{
    if (frame.isNull())                                                        // - 프레임 검증: 유효하지 않은 영상 프레임인 경우 처리 생략
        return;

    m_lastFrame = frame;                                                       // - 프레임 저장: 최근 수신된 프레임 이미지 저장
    setSwitching(false);                                                       // - 전환 상태 해제: 프레임 수신 완료에 따른 전환 표시 종료
    updateFpsCounter();                                                        // - FPS 측정: 초당 프레임 수 카운터 갱신

    if (m_lastFrameSize != frame.size()) {                                     // - 해상도 변경 확인: 영상 크기가 변경된 경우 신호 알림
        m_lastFrameSize = frame.size();                                        // - 해상도 저장: 변경된 영상 크기 보관
        emit videoSizeChanged();                                               // - 신호 발생: 영상 크기 변경 알림
    }

    update();                                                                  // - 화면 갱신: 비디오 스트림 다시 그리기 요청
}

void VideoStream::handleConnectionStateChanged(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)                                            // - 중복 변경 방지: 기존 상태와 동일한 경우 생략
        return;
    m_connectionState = state;                                                 // - 상태 갱신: 내부 연결 상태 변수 저장
    emit connectionStateChanged();                                             // - 신호 발생: 영상 연결 상태 변경 알림
    update();                                                                  // - 화면 갱신: 상태 변경에 따른 다시 그리기 요청
}

void VideoStream::updateFpsCounter()
{
    if (!m_fpsClock.isValid()) {                                               // - 타이머 검증: 타이머 미작동 시 측정 시작
        m_fpsClock.start();                                                    // - 타이머 구동: FPS 측정 시각 시계 시작
        m_frameCountWindow = 0;                                                // - 프레임 수 초기화: 누적 카운트 0으로 설정
        return;
    }

    ++m_frameCountWindow;                                                     // - 프레임 수 증가: 누적 프레임 카운트 증가
    const qint64 elapsed = m_fpsClock.elapsed();                               // - 경과 시간 측정: 측정 시계 경과 시간(ms) 확인
    if (elapsed >= 1000) {                                                     // - 1초 주기 확인: 1초(1000ms) 경과 시 FPS 값 계산
        m_fps = m_frameCountWindow * 1000.0 / elapsed;                         // - FPS 계산: 경과 시간 대비 프레임 수 산출
        m_frameCountWindow = 0;                                                // - 프레임 수 초기화: 다음 측정을 위한 카운트 리셋
        m_fpsClock.restart();                                                  // - 타이머 재시작: 측정 시계 0ms 재시작
        emit fpsChanged();                                                     // - 신호 발생: FPS 수치 변경 알림
    }
}

void VideoStream::paint(QPainter *painter)
{
    const QRectF bounds(0, 0, width(), height());                              // - 그려질 영역 생성: 스트림 아이템 전체 영역
    if (bounds.isEmpty())                                                      // - 영역 검증: 크기가 없으면 그리기 생략
        return;

    painter->fillRect(bounds, m_placeholderColor);                             // - 배경 채우기: 대체 배경 색상으로 지정 영역 채우기

    if (!m_lastFrame.isNull()) {                                               // - 프레임 존재 확인: 수신된 영상 프레임이 있는 경우 렌더링
        const QRectF videoRect = AspectFit::fitRect(bounds.size(), m_lastFrame.size()); // - 비율 유지 영역 계산: 원본 비율 맞춤 영역 산출
        painter->drawImage(videoRect, m_lastFrame);                            // - 영상 그리기: 비율 맞춘 영역에 이미지 렌더링
    } else {
        paintPlaceholder(painter, bounds);                                     // - 대체 화면 그리기: 영상 미수신 시 대체 그래픽 렌더링
    }

    if (m_switching) {                                                         // - 전환 상태 확인: 카메라 전환 중인 경우 덮어씌우기
        painter->fillRect(bounds, QColor(0, 0, 0, 110));                       // - 반투명 덮개: 반투명 검은색 오버레이 채우기
        QFont font = painter->font();                                          // - 폰트 가져오기: 텍스트 표시용 폰트 설정
        font.setPointSize(12);                                                 // - 글자 크기 설정: 12pt 지정
        font.setBold(true);                                                    // - 굵게 설정: 굵은 글씨체 지정
        painter->setFont(font);                                                // - 폰트 적용: 그려질 폰트 등록
        painter->setPen(QColor(255, 255, 255, 230));                           // - 텍스트 펜 설정: 흰색 글씨용 펜 지정
        painter->drawText(bounds, Qt::AlignCenter, QStringLiteral("전환 중..."));// - 텍스트 그리기: 중앙 정렬 전환 문구 그리기
    }
}

void VideoStream::paintPlaceholder(QPainter *painter, const QRectF &bounds) const
{
    painter->setPen(QPen(QColor(255, 255, 255, 18), 1));                        // - 배경 선 펜 설정: 사선 무늬용 반투명 펜 지정
    for (int x = -static_cast<int>(bounds.height()); x < static_cast<int>(bounds.width()); x += 28) // - 사선 무늬 반복: 배경 사선 패턴 그리기
        painter->drawLine(QPointF(x, bounds.height()), QPointF(x + bounds.height(), 0));

    QFont font = painter->font();                                              // - 폰트 가져오기: 카메라 ID 표시용 폰트 설정
    font.setPointSize(14);                                                     // - 글자 크기 설정: 14pt 지정
    font.setBold(true);                                                        // - 굵게 설정: 굵은 글씨체 지정
    painter->setFont(font);                                                    // - 폰트 적용: 그려질 폰트 등록
    painter->setPen(QColor(255, 255, 255, 150));                               // - 텍스트 펜 설정: 반투명 흰색 펜 지정
    painter->drawText(bounds.adjusted(0, -10, 0, -10), Qt::AlignCenter,
                       m_cameraId.isEmpty() ? QStringLiteral("NO SIGNAL") : m_cameraId); // - ID 텍스트 그리기: 카메라 ID 또는 신호 없음 텍스트 그리기

    QFont subFont = painter->font();                                           // - 하단 폰트 가져오기: 보조 문구용 폰트 설정
    subFont.setPointSize(10);                                                  // - 글자 크기 설정: 10pt 지정
    subFont.setBold(false);                                                    // - 일반체 설정: 일반 글씨체 지정
    painter->setFont(subFont);                                                 // - 폰트 적용: 그려질 폰트 등록
    painter->setPen(QColor(255, 255, 255, 90));                                // - 보조 펜 설정: 연한 흰색 펜 지정
    painter->drawText(bounds.adjusted(0, 16, 0, 16), Qt::AlignCenter, QStringLiteral("NO SIGNAL")); // - 보조 텍스트 그리기: 신호 없음 보조 문구 그리기
}
