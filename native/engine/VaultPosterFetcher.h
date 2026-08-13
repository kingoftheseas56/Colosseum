#pragma once
// VaultPosterFetcher — canonical poster fetcher + cache (Vault browse-artwork execution
// plan, Slice 2, 2026-08-13). Sibling to Slice 1's VaultThumbnailer (frame-grab
// producer): same injected-cacheDir pattern, same SHA-1-hashed-key cache filename, same
// idempotent cache-hit short-circuit, same atomic tmp-then-rename promote, same honest
// "no file on failure" contract — but the transport is a QNetworkAccessManager GET
// instead of an ffmpeg QProcess, and the cache key is the recognized item's stable
// IDENTITY id (VaultIndex::FileRow::identityId), never the (path,size,mtime) triple
// VaultThumbnailer/VaultCacheKey use — a poster belongs to the CATALOGUE match, not to
// any one file on disk.
//
// Given a poster URL (the https://live.metahub.space/poster/medium/<tt>/img
// VaultIdentifier.cpp:182 already derives into FileRow::identityCoverUrl) and that
// identity id, downloads the image off the GUI thread (async QNetworkAccessManager,
// never a blocking read) into a "posters/" subdir under the injected cache dir.
// QNetworkAccessManager natively handles both http(s):// (the real metahub URL) and
// file:// (what the deterministic test drives — no live network in the gate), so the
// same code path serves both with no branching on scheme.
//
// Idempotent: a cache hit for an already-fetched id returns the file path immediately
// and starts no download. A failed fetch (network error, 404, unreadable file://, empty
// body) leaves NO file — never a half-written or zero-byte file, never a broken-image
// marker.

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class VaultPosterFetcher : public QObject
{
    Q_OBJECT

public:
    // `cacheDir` is the same VaultStoreIo-managed dir VaultThumbnailer/VaultEnricher are
    // constructed with; this class owns a "posters/" subdirectory under it. Never a
    // QStandardPaths location baked in here — callers (production and tests alike)
    // inject the dir so a QTemporaryDir isolates a test run.
    explicit VaultPosterFetcher(QString cacheDir, QObject* parent = nullptr);
    ~VaultPosterFetcher() override;

    // Cache hit: returns the poster's file path immediately, downloads nothing. Cache
    // miss (and not already in flight for this id): returns "" and starts an
    // off-thread download — listen to posterReady() for the result. An empty id or url
    // is a no-op ("") — never a job for a request that cannot possibly key or fetch.
    QString requestPoster(const QString& identityId, const QString& url);

    // Synchronous, side-effect-free cache lookup: the file path if a poster already
    // sits in the cache for this id, "" otherwise. Never starts a download.
    QString cachedPosterPath(const QString& identityId) const;

    // Test/diagnostic seam: how many downloads are currently in flight.
    int inFlightCount() const { return m_jobs.size(); }

Q_SIGNALS:
    // Fired once, on a FRESH successful download only (never on a cache hit — that
    // returns synchronously from requestPoster instead).
    void posterReady(const QString& identityId, const QString& filePath);

private:
    QString postersDir() const;
    QString outputPathForId(const QString& identityId) const;
    void finishJob(QNetworkReply* reply);

    QString m_cacheDir;
    QNetworkAccessManager* m_nam = nullptr;
    QHash<QNetworkReply*, QString> m_jobs; // reply -> identityId
    QSet<QString> m_activeIds;             // ids with a download running (never double-started)
};
