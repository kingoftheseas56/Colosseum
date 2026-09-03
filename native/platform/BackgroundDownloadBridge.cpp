#include "BackgroundDownloadBridge.h"

#include "BackgroundDownloadPolicy.h"
#include "engine/LocalDownloads.h"

namespace Colosseum::Platform {

BackgroundDownloadBridge::BackgroundDownloadBridge(QObject *parent)
    : QObject(parent) {
}

BackgroundDownloadBridge::BackgroundDownloadBridge(
    LocalDownloads *source, QObject *parent)
    : QObject(parent) {
    setSource(source);
}

QVariantList BackgroundDownloadBridge::jobs() const {
    return m_jobs;
}

int BackgroundDownloadBridge::runningCount() const {
    return m_runningCount;
}

bool BackgroundDownloadBridge::backgroundHostRequired() const {
    return m_runningCount > 0;
}

void BackgroundDownloadBridge::setSource(LocalDownloads *source) {
    if (m_source == source)
        return;

    if (m_source)
        disconnect(m_source, nullptr, this, nullptr);

    m_source = source;
    if (m_source) {
        connect(m_source, &LocalDownloads::changed,
                this, &BackgroundDownloadBridge::refresh);
    }
    refresh();
}

void BackgroundDownloadBridge::refresh() {
    const QVariantList nextJobs = m_source ? m_source->activeJobs() : QVariantList();
    int nextRunningCount = 0;
    for (const QVariant &value : nextJobs) {
        if (jobRequiresBackgroundHost(value.toMap()))
            ++nextRunningCount;
    }

    if (nextJobs == m_jobs && nextRunningCount == m_runningCount)
        return;

    m_jobs = nextJobs;
    m_runningCount = nextRunningCount;
    emit changed();
    emit platformSnapshotReady(m_jobs);
}

} // namespace Colosseum::Platform
