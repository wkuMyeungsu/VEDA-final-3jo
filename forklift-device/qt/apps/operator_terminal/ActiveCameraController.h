#pragma once

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QVector>

#include "models/BBox.h"
#include "models/CameraInfo.h"
#include "models/RiskMetadata.h"
#include "models/Types.h"
#include "network/OnvifBBoxParser.h"

class MetadataService;
class VideoSourceManager;
class IWarningDevice;
class IVideoSource;

// Owns "which single camera is currently shown" on the operator terminal.
// In a real deployment the server assigns this camera_id (e.g. based on
// which zone the forklift is in); for now it starts at terminal.json's
// default_camera_id and can be changed via OperatorDemoController for the
// handover demo. Not shared with the control center -- this is
// operator-terminal-specific state, so it lives here rather than in
// common/.
class ActiveCameraController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString activeCameraId READ activeCameraId WRITE setActiveCameraId NOTIFY activeCameraIdChanged)
    Q_PROPERTY(QString zone READ zone NOTIFY activeCameraIdChanged)
    Q_PROPERTY(QString cameraName READ cameraName NOTIFY activeCameraIdChanged)
    Q_PROPERTY(int riskLevel READ riskLevel NOTIFY metadataChanged)
    Q_PROPERTY(int exceptionState READ exceptionState NOTIFY metadataChanged)
    Q_PROPERTY(double distanceM READ distanceM NOTIFY metadataChanged)
    Q_PROPERTY(bool distanceValid READ distanceValid NOTIFY metadataChanged)
    Q_PROPERTY(BBox personBBox READ personBBox NOTIFY metadataChanged)
    Q_PROPERTY(BBox forkliftBBox READ forkliftBBox NOTIFY metadataChanged)
    Q_PROPERTY(
        RiskTypes::ConnectionState videoConnectionState READ videoConnectionState NOTIFY videoConnectionStateChanged)

public:
    ActiveCameraController(QVector<CameraInfo> cameras, MetadataService *metadataService,
                            VideoSourceManager *videoManager, IWarningDevice *warningDevice,
                            QObject *parent = nullptr);

    QString activeCameraId() const { return m_activeCameraId; }
    void setActiveCameraId(const QString &cameraId);

    QString zone() const { return m_zone; }
    QString cameraName() const { return m_cameraName; }

    int riskLevel() const { return static_cast<int>(m_latest.riskLevel()); }
    int exceptionState() const { return static_cast<int>(m_latest.exceptionState()); }
    double distanceM() const { return m_latest.distanceM(); }
    bool distanceValid() const { return m_latest.distanceValid(); }
    // ONVIF 데이터가 들어온 적 있는 카메라(RTSP)만 그쪽 bbox 사용, 그 외(Mock/LocalFile)는 기존 경로 유지
    BBox personBBox() const { return m_onvifActive ? m_onvifPersonBBox : m_latest.personBBox(); }
    BBox forkliftBBox() const { return m_latest.forkliftBBox(); }

    RiskTypes::ConnectionState videoConnectionState() const { return m_videoConnectionState; }

signals:
    void activeCameraIdChanged();
    void metadataChanged();
    void videoConnectionStateChanged();

private slots:
    void handleMetadataUpdated(const RiskMetadata &metadata);
    void handlePersonDetected(const BBox &bbox);

private:
    void attachVideoConnection();

    QHash<QString, CameraInfo> m_cameras;
    QString m_activeCameraId;
    QString m_zone;
    QString m_cameraName;
    RiskMetadata m_latest;

    MetadataService *m_metadataService = nullptr;
    VideoSourceManager *m_videoManager = nullptr;
    IWarningDevice *m_warningDevice = nullptr;

    RiskTypes::ConnectionState m_videoConnectionState = RiskTypes::ConnectionState::Disconnected;
    QMetaObject::Connection m_videoConnection;

    OnvifBBoxParser *m_onvifParser = nullptr;
    QMetaObject::Connection m_onvifConnection;
    BBox m_onvifPersonBBox;
    bool m_onvifActive = false;
};
