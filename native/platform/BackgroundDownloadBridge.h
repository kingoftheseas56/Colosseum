#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>

class LocalDownloads;

namespace Colosseum::Platform {

class BackgroundDownloadBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY changed)
    Q_PROPERTY(bool backgroundHostRequired READ backgroundHostRequired NOTIFY changed)

public:
    explicit BackgroundDownloadBridge(QObject *parent = nullptr);
    BackgroundDownloadBridge(LocalDownloads *source, QObject *parent = nullptr);

    QVariantList jobs() const;
    int runningCount() const;
    bool backgroundHostRequired() const;

    void setSource(LocalDownloads *source);
    Q_INVOKABLE void refresh();

signals:
    void changed();
    void platformSnapshotReady(const QVariantList &jobs);

private:
    QPointer<LocalDownloads> m_source;
    QVariantList m_jobs;
    int m_runningCount = 0;
};

} // namespace Colosseum::Platform
