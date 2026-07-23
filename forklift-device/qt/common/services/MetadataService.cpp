#include "MetadataService.h"

#include "../network/IMetadataSource.h"

MetadataService::MetadataService(QVector<CameraInfo> cameras, int eventLogMaxEntries, QObject *parent)
    : QObject(parent)
    , m_eventLogModel(eventLogMaxEntries)
{
    m_cameraListModel.setCameras(cameras);
    for (const CameraInfo &info : cameras)
        m_cameraNames.insert(info.cameraId, info.name);
}

void MetadataService::setSource(IMetadataSource *source)
{
    if (m_source == source)
        return;

    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);

    m_source = source;

    if (m_source) {
        connect(m_source, &IMetadataSource::metadataReceived, this, &MetadataService::handleMetadata);
        connect(m_source, &IMetadataSource::connectionStateChanged, this,
                &MetadataService::handleSourceConnectionStateChanged);
        handleSourceConnectionStateChanged(m_source->connectionState());
    }
}

void MetadataService::start()
{
    if (m_source)
        m_source->start();
}

void MetadataService::stop()
{
    if (m_source)
        m_source->stop();
}

RiskMetadata MetadataService::latestFor(const QString &cameraId) const
{
    return m_latest.value(cameraId);
}

void MetadataService::handleMetadata(const RiskMetadata &metadata)
{
    m_latest.insert(metadata.cameraId(), metadata);

    m_cameraListModel.updateRisk(metadata.cameraId(), metadata.riskLevel(), metadata.exceptionState(),
                                  metadata.distanceM());

    m_alertListModel.upsert(metadata.cameraId(), m_cameraNames.value(metadata.cameraId()), metadata.zone(),
                             metadata.riskLevel(), metadata.distanceM(), metadata.exceptionState());

    const bool noteworthy =
        metadata.riskLevel() != RiskTypes::RiskLevel::Safe || metadata.exceptionState() != RiskTypes::ExceptionState::None;
    if (noteworthy)
        m_eventLogModel.addEntry(metadata);

    emit metadataUpdated(metadata);
}

void MetadataService::handleSourceConnectionStateChanged(RiskTypes::ConnectionState state)
{
    if (m_connectionState == state)
        return;
    m_connectionState = state;
    emit connectionStateChanged();
}
