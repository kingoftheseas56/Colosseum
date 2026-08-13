#pragma once
// VaultThumbnailer — persistent frame-grab producer (Vault browse-artwork execution
// plan, Slice 1, 2026-08-13). Turns the player's throwaway hover-frame extraction
// (SeekThumbnailer, native/player/seekthumbnailer.cpp — a SIBLING, not touched by this
// class) into a durable, cached still a browse tile can later wear as its picture.
//
// One ffmpeg still per distinct (path,size,mtimeMs) key, reusing SeekThumbnailer's
// exact ffmpeg invocation shape (-ss <t> -i <src> -frames:v 1 -vf scale=320:-2
// -f mjpeg) but writing the frame to a PERSISTENT FILE under the injected cacheDir's
// "thumbs/" subdirectory instead of piping it to stdout as a data: URL (that's the
// hover path's job, not this one's). Idempotent: a cache hit for an already-grabbed
// key returns the existing file path immediately and never re-spawns ffmpeg. ffmpeg
// runs off the GUI thread via async QProcess (same transport idiom as
// SeekThumbnailer), with a small bounded in-flight count so a batch of newly-adopted
// videos can't fork an unbounded pile of ffmpeg processes at once.
//
// The cache key is the SAME (normalizedPath, size, mtimeMs) triple VaultEnricher's
// video duration cache uses, via the shared VaultCacheKey::make() — never a second,
// silently-diverging derivation.

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QTimer>

class VaultThumbnailer : public QObject
{
    Q_OBJECT

public:
    // `cacheDir` is the same VaultStoreIo-managed dir VaultEnricher is constructed
    // with (holds durations.json); this class owns a "thumbs/" subdirectory under it.
    // Never a QStandardPaths location baked in here — callers (production and tests
    // alike) inject the dir so a QTemporaryDir isolates a test run.
    explicit VaultThumbnailer(QString cacheDir, QObject* parent = nullptr);
    ~VaultThumbnailer() override;

    // Cache hit: returns the still's file path immediately, spawns nothing. Cache
    // miss: returns "" and starts (or queues, under the in-flight bound) an
    // off-thread ffmpeg grab — listen to thumbReady() for the result.
    // `knownDurationSec` lets a caller pass VaultEnricher's already-cached probe
    // result so the grab lands past a black/logo lead-in; -1 (default) means unknown
    // and falls back to a fixed 60s offset, per the execution plan.
    QString requestThumb(const QString& path, qint64 size, qint64 mtimeMs,
                         double knownDurationSec = -1.0);

    // Synchronous, side-effect-free cache lookup: the file path if a still already
    // sits in the cache for this key, "" otherwise. Never spawns ffmpeg.
    QString cachedThumbPath(const QString& path, qint64 size, qint64 mtimeMs) const;

    // Test/diagnostic seam: how many ffmpeg grabs are currently running.
    int inFlightCount() const { return m_jobs.size(); }

Q_SIGNALS:
    // Fired once, on a FRESH grab only (never on a cache hit — that returns
    // synchronously from requestThumb instead).
    void thumbReady(const QString& key, const QString& filePath);

private:
    struct PendingRequest {
        QString key;
        QString sourcePath;
        double offsetSec = 0.0;
    };
    struct Job {
        QString key;
        QString tmpPath;
        QString outPath;
        QTimer* stallTimer = nullptr;
    };

    QString thumbsDir() const;
    QString outputPathForKey(const QString& key) const;
    static double offsetSecondsForDuration(double durationSec);
    void startJob(const QString& key, const QString& sourcePath, double offsetSec);
    void finishJob(QProcess* proc, int exitCode, QProcess::ExitStatus status);
    void killJob(QProcess* proc);
    void pumpQueue();

    QString m_cacheDir;
    QHash<QProcess*, Job> m_jobs;
    QSet<QString> m_activeKeys;     // keys with a job running OR queued (never double-spawned)
    QQueue<PendingRequest> m_pending;
};
