#pragma once

#include <QUrl>
#include <atomic>
#include <thread>
#include "IVideoSource.h"

typedef struct _GstElement GstElement;
typedef struct _GstBus GstBus;


class RtspVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    RtspVideoSource(QString cameraId, QUrl rtspUrl, QObject *parent = nullptr);
    ~RtspVideoSource() override;    

    void start() override;
    void stop() override;

private:

    void busLoop(); //워커 스레드에서 실행되는 함수
    void scheduleReconnect();

    QString m_cameraId;
    QUrl m_rtspUrl;

    GstElement *m_pipeline = nullptr;
    GstBus *m_bus = nullptr;

    std::thread m_busThread;
    std::atomic_bool m_stopRequested{false};

};