#pragma once

#include <QHash>
#include <QPointF>
#include <QTimer>
#include <QVector>
#include <optional>

#include "../models/CameraInfo.h"
#include "IMetadataSource.h"

// Periodically generates RiskMetadata events for every configured camera,
// mostly SAFE with occasional CAUTION/DANGER/EMERGENCY, plus a slow
// random-walk of the person/forklift bounding boxes so the demo looks
// alive. DemoController drives the override* methods to force a specific
// camera into a specific state for a scripted demo.
class MockMetadataSource : public IMetadataSource
{
    Q_OBJECT

public:
    explicit MockMetadataSource(QVector<CameraInfo> cameras, QObject *parent = nullptr);

    void start() override;
    void stop() override;

    // --- Demo control surface (called by DemoController) ---
    void setAutoPlay(bool enabled);
    bool autoPlay() const { return m_autoPlay; }

    void setRiskOverride(const QString &cameraId, RiskTypes::RiskLevel level, double distanceM,
                          RiskTypes::ExceptionState exception);
    void clearRiskOverride(const QString &cameraId);

    void setPersonBBoxOverride(const QString &cameraId, const BBox &box);
    void setForkliftBBoxOverride(const QString &cameraId, const BBox &box);
    void clearBBoxOverrides(const QString &cameraId);

private slots:
    void tick();

private:
    struct CameraState {
        QString zone;
        bool overrideActive = false;
        RiskTypes::RiskLevel overrideLevel = RiskTypes::RiskLevel::Safe;
        double overrideDistanceM = 0.0;
        RiskTypes::ExceptionState overrideException = RiskTypes::ExceptionState::None;
        std::optional<BBox> personBBoxOverride;
        std::optional<BBox> forkliftBBoxOverride;
        QPointF personCenter;
        QPointF forkliftCenter;

        bool hasAnyOverride() const
        {
            return overrideActive || personBBoxOverride.has_value() || forkliftBBoxOverride.has_value();
        }
    };

    RiskMetadata generateForCamera(const QString &cameraId, CameraState &state) const;
    void emitCameraUpdate(const QString &cameraId);
    static QPointF wanderPosition(QPointF current);

    QHash<QString, CameraState> m_states;
    QTimer m_timer;
    bool m_autoPlay = true;
};
