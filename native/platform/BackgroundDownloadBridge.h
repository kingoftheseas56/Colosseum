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
    Q_PROPERTY(int videoRunningCount READ videoRunningCount NOTIFY changed)
    Q_PROPERTY(bool backgroundHostReady READ backgroundHostReady NOTIFY hostStateChanged)
    Q_PROPERTY(QString backgroundHostError READ backgroundHostError NOTIFY hostStateChanged)

public:
    explicit BackgroundDownloadBridge(QObject *parent = nullptr);
    BackgroundDownloadBridge(LocalDownloads *source, QObject *parent = nullptr);
    ~BackgroundDownloadBridge() override;

    QVariantList jobs() const;
    int runningCount() const;
    bool backgroundHostRequired() const;
    int videoRunningCount() const { return m_videoRunningCount; }
    bool backgroundHostReady() const { return m_hostReady && backgroundHostRequired(); }
    QString backgroundHostError() const { return m_hostError; }
    void setForeground(bool foreground);
    void acceptHostState(qint64 epoch, bool ready, const QString &error);

    void setSource(LocalDownloads *source);
    Q_INVOKABLE void refresh();

signals:
    void changed();
    void platformSnapshotReady(const QVariantList &jobs);
    void hostStateChanged();
    void openDownloadsRequested();

private:
    QPointer<LocalDownloads> m_source;
    QVariantList m_jobs;
    int m_runningCount = 0;
    int m_videoRunningCount = 0;
    bool m_foreground = true;
    bool m_hostReady = false;
    QString m_hostError;
    qint64 m_hostEpoch = 0;
    qint64 m_nativeHandle = 0;
    void publishAndroidSnapshot();
};

} // namespace Colosseum::Platform
