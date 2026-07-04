#pragma once

#include <QHash>
#include <QObject>
#include <QNetworkAccessManager>
#include <QVariantList>
#include <QVariantMap>

class QFile;
class QNetworkReply;

class DownloadStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QString defaultDownloadDir READ defaultDownloadDir NOTIFY changed)

public:
    explicit DownloadStore(QObject *parent = nullptr);
    ~DownloadStore() override;

    QVariantMap status() const { return m_status; }
    QString defaultDownloadDir() const { return m_defaultDownloadDir; }

    Q_INVOKABLE void startDownload(const QVariantMap &request);
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void revealDownload();
    Q_INVOKABLE void resetDownload();

    // ── downloaded-videos library (persisted index of every finished download) ──
    Q_INVOKABLE QVariantList downloadedVideos() const;   // stale-checked: missing files reported, not hidden
    Q_INVOKABLE void removeVideo(const QString &id);     // deletes the local file AND the entry

signals:
    void changed();          // live-job status (player panel) — unchanged contract
    void libraryChanged();   // the persisted downloaded-videos set changed

private:
    struct Entry {
        QString id;          // stream id when known, else generated
        QString kind;        // "movie" | "episode"
        QString title;
        QString subtitle;
        QString seriesTitle;
        int season = 0;
        int episode = 0;
        QString path;
        qint64 bytes = 0;
        qint64 addedAt = 0;
    };

    QString buildDefaultDownloadDir() const;
    QString buildOutputPath(const QVariantMap &request) const;
    QString sanitizeFilePart(const QString &value) const;
    QString extensionFromUrl(const QString &url) const;
    void setStatus(const QVariantMap &status);
    void failDownload(const QString &message);
    void cleanupActiveReply();

    QString indexPath() const;
    void loadIndex();
    void saveIndex() const;
    void recordFinished(const QString &path, qint64 bytes);

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
    QString m_defaultDownloadDir;
    QString m_outputPath;
    QString m_partPath;
    QVariantMap m_status;
    QVariantMap m_activeRequest;      // metadata of the in-flight job, kept for recordFinished
    QHash<QString, Entry> m_index;    // id -> finished video
};
