#pragma once

// DownloadStore — the Theatre lane's video download engine.
// v2 (2026-07-04): bounded job QUEUE with lazy per-job stream resolution — TB2's
// proven gap-episode flow at Harbor's simplicity. A job's durable payload is the
// episode's stream id (tt…:s:e), which never expires: resolution to a concrete
// URL happens only when the job is promoted (needResolve → feedUrl handshake with
// the QML resolver), so retry is always honest. Cap = MAX_ACTIVE_VIDEO.
// Finished files land in the persisted library index (videos/index.json);
// the in-flight queue survives restarts via videos/queue.json.

#include <QHash>
#include <QList>
#include <QObject>
#include <QNetworkAccessManager>
#include <QVariantList>
#include <QVariantMap>

class QFile;
class QNetworkReply;

class DownloadStore : public QObject {
    Q_OBJECT
    // `status` mirrors the ACTIVE job — the player's download panel contract.
    Q_PROPERTY(QVariantMap status READ status NOTIFY changed)
    Q_PROPERTY(QString defaultDownloadDir READ defaultDownloadDir NOTIFY changed)
    Q_PROPERTY(int queueRevision READ queueRevision NOTIFY changed)

public:
    explicit DownloadStore(QObject *parent = nullptr);
    ~DownloadStore() override;

    QVariantMap status() const;
    QString defaultDownloadDir() const { return m_defaultDownloadDir; }
    int queueRevision() const { return m_queueRevision; }

    // Player path (pre-resolved url in the request) — kept name for compat.
    Q_INVOKABLE void startDownload(const QVariantMap &request);
    Q_INVOKABLE void cancelDownload();               // cancels the ACTIVE job (player panel)
    Q_INVOKABLE void revealDownload();
    Q_INVOKABLE void resetDownload();

    // ── queue (season checkout et al.) ──
    Q_INVOKABLE void enqueue(const QVariantMap &request);       // url optional
    Q_INVOKABLE void enqueueBatch(const QVariantList &requests);
    Q_INVOKABLE void cancelJob(const QString &id);   // drops the job entirely
    Q_INVOKABLE void retryJob(const QString &id);    // failed → queued (re-resolve)
    Q_INVOKABLE void pauseJob(const QString &id);    // keeps the .part; frees the cap slot
    Q_INVOKABLE void resumeJob(const QString &id);   // paused → queued (Range-append if same-session url)
    Q_INVOKABLE QVariantList jobs() const;
    Q_INVOKABLE bool hasVideo(const QString &id) const;   // already in the library
    // resolver handshake (Main.qml answers needResolve with one of these)
    Q_INVOKABLE void feedUrl(const QString &id, const QString &url);
    Q_INVOKABLE void failJob(const QString &id, const QString &reason);

    // ── downloaded-videos library ──
    Q_INVOKABLE QVariantList downloadedVideos() const;
    Q_INVOKABLE QVariantMap removeVideo(const QString &id);

    // dev harness: COLOSSEUM_VIDEOQ_SELFTEST="exactrow" (later: "group"|"speed"|"pause")
    // logs [videoq-selftest] PASS/FAIL lines at startup; test jobs self-clean.
    Q_INVOKABLE void selfTest(const QString &mode);

signals:
    void changed();          // active-job status and/or queue shape changed
    void libraryChanged();   // the persisted downloaded-videos set changed
    void removed(const QString &id);
    void needResolve(const QString &id, const QString &streamId, const QString &mediaType);

private:
    struct Job {
        QVariantMap request;   // full identity payload (id/kind/title/…/art)
        QString id;
        QString url;           // "" until resolved
        QString state;         // "queued" | "resolving" | "downloading" | "failed"
        QString error;
        double ratio = 0.0;
        qint64 received = 0;
        qint64 total = 0;
        double speed = 0.0;          // bytes/sec, EMA-smoothed
        int etaSec = -1;             // -1 = unknown
        qint64 lastSampleMs = 0;
        qint64 lastSampleBytes = 0;
        qint64 baseOffset = 0;       // resume: bytes already in .part before this reply
        bool resumeFromPart = false;   // set by resumeJob when the paused url is still valid
        QString outputPath;
        QString partPath;
        QNetworkReply *reply = nullptr;
        QFile *file = nullptr;
    };

    struct Entry {
        QString id, kind, title, subtitle, seriesTitle, path, art;
        int season = 0, episode = 0;
        qint64 bytes = 0, addedAt = 0;
    };

    QString buildDefaultDownloadDir() const;
    QString buildOutputPath(const QVariantMap &request) const;
    QString sanitizeFilePart(const QString &value) const;
    QString extensionFromUrl(const QString &url) const;
    static QString groupKeyFor(const QVariantMap &request);

    int activeIndex() const;                 // resolving/downloading job, or -1
    int jobIndex(const QString &id) const;
    void pump();                             // promote oldest queued while under cap
    void startHttp(Job &job);
    void finishHttp(Job &job);
    void sampleProgress(Job &job, qint64 received, qint64 total, qint64 nowMs);
    void pruneGroupIfSettled(const QString &groupKey);
    void cleanupJob(Job &job);
    void touch();                            // ++revision + changed()

    QString baseDir() const;
    void loadIndex();
    void saveIndex() const;
    void loadQueue();
    void saveQueue() const;
    void recordFinished(const Job &job, qint64 bytes);

    static constexpr int MAX_ACTIVE_VIDEO = 1;   // videos are GBs; raise deliberately

    QNetworkAccessManager m_network;
    QString m_defaultDownloadDir;
    QString m_lastDonePath;
    QList<Job> m_jobs;
    QHash<QString, Entry> m_index;
    int m_queueRevision = 0;
};
