#pragma once

#include <QElapsedTimer>
#include <QTimer>

#include "IVideoSource.h"

// Synthetic "test pattern" feed: gradient background, a moving marker, and
// camera_id/zone/timestamp burned into the frame. Every camera uses this
// until config/cameras.json points it at a real source_type.
class MockVideoSource : public IVideoSource
{
    Q_OBJECT

public:
    MockVideoSource(QString cameraId, QString zone, QObject *parent = nullptr);

    void start() override;
    void stop() override;
    void setSimulatedConnected(bool connected) override;

private slots:
    void emitFrame();

private:
    QImage renderFrame() const;

    QString m_cameraId;
    QString m_zone;
    QTimer m_frameTimer;
    QElapsedTimer m_clock;
    QSize m_frameSize{960, 540};
    bool m_simulatedConnected = true;
};
