#pragma once

#include <QHash>
#include <QObject>
#include <QVector>

#include "../models/CameraInfo.h"
#include "../models/RiskMetadata.h"
#include "AlertListModel.h"
#include "CameraListModel.h"
#include "EventLogModel.h"

class IMetadataSource;

// Consumes IMetadataSource events, keeps the latest known RiskMetadata per
// camera, and fans updates out to the three QML-facing models:
// CameraListModel (grid/status), AlertListModel (active warnings), and
// EventLogModel (audit trail). Entirely UI-independent -- see
// tests/test_mock_metadata_source.cpp for how it's exercised without QML.
class MetadataService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RiskTypes::ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)

public:
    MetadataService(QVector<CameraInfo> cameras, int eventLogMaxEntries, QObject *parent = nullptr);

    // Ownership stays with the caller; MetadataService only connects to it.
    void setSource(IMetadataSource *source);
    void start();
    void stop();

    CameraListModel *cameraListModel() { return &m_cameraListModel; }
    EventLogModel *eventLogModel() { return &m_eventLogModel; }
    AlertListModel *alertListModel() { return &m_alertListModel; }

    Q_INVOKABLE RiskMetadata latestFor(const QString &cameraId) const;
    RiskTypes::ConnectionState connectionState() const { return m_connectionState; }

signals:
    void metadataUpdated(const RiskMetadata &metadata);
    void connectionStateChanged();

private slots:
    void handleMetadata(const RiskMetadata &metadata);
    void handleSourceConnectionStateChanged(RiskTypes::ConnectionState state);

private:
    QHash<QString, QString> m_cameraNames; // cameraId -> display name (for AlertListModel rows)
    QHash<QString, RiskMetadata> m_latest;
    CameraListModel m_cameraListModel;
    EventLogModel m_eventLogModel;
    AlertListModel m_alertListModel;
    IMetadataSource *m_source = nullptr;
    RiskTypes::ConnectionState m_connectionState = RiskTypes::ConnectionState::Disconnected;
};
