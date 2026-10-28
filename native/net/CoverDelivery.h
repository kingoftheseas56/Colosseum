#pragma once
//
// CoverDelivery — the shared, app-wide admission + Retry-After owner for cover
// GET traffic on the existing CachingNam/CachingNamFactory stack.
//
// One CachingNamFactory supplies one CoverDeliveryState to every cache-enabled
// NAM it creates (the QML engine creates NAMs on more than one thread), so the
// per-host cap, the bounded FIFO queues and the cooldown ledger are genuinely
// app-wide. Nothing here is a second network stack: admitted work re-enters the
// SAME CachingNam's dispatch path (prepare -> pin/UA -> watch -> diag -> evict).
//
// Contract highlights (see INPUT-OWNER-PLAN.md):
//  * Positive classification only: exact cover-host/path patterns plus an
//    explicit test-host set. Catalogue XHR, search, downloads, Jikan and every
//    useCache=false lane never enter this gate.
//  * Two active managed cover transactions per logical hostname by default,
//    FIFO within a host, bounded queues; overflow is an asynchronous local
//    failure, never a silent drop and never a provider retry.
//  * All counter mutation happens under one small mutex; callbacks/wakes are
//    posted outside it and run on the owning NAM's thread.
//  * Durable, versioned, bounded JSON ledger (hashed URL digests + hosts) under
//    the app's local data root; QSaveFile atomic write; malformed/oversized
//    state fails safe to empty.
//

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSslError>
#include <QString>
#include <QUrl>

#include <atomic>
#include <functional>

class QIODevice;
class QNetworkReply;
class QTimer;

// ── Retry-After parsing (pure, injectable clock) ─────────────────────────────

struct RetryAfterDecision
{
    bool valid = false;     // usable server value or bounded fallback
    bool fallback = false;  // header absent/malformed -> local policy fallback
    bool expired = false;   // valid value already in the past / explicit zero
    qint64 deadlineUtcMs = 0;
    qint64 deltaSeconds = 0;   // numeric server delta when parseable
    bool hadHeader = false;    // header was present (even if malformed)
};

// ── The dispatcher surface a DeferredCoverReply needs from its NAM ───────────

class CoverDispatcher
{
public:
    virtual ~CoverDispatcher() = default;

    // Start one real transaction through the owning NAM's existing dispatch
    // path (pins, UA, Jikan branch, cache, watch/diag/eviction). Never called
    // under the delivery-state mutex.
    virtual QNetworkReply *dispatchCoverRequest(QNetworkAccessManager::Operation op,
                                                const QNetworkRequest &prepared,
                                                QIODevice *outgoing,
                                                const QUrl &logicalUrl,
                                                const QString &logicalHost) = 0;

    // Fresh prepared request for a cross-host managed hop: pin + UA + redirect
    // policy applied for the DESTINATION so no stale Host/TLS identity crosses.
    virtual QNetworkRequest prepareCoverHop(const QUrl &target) const = 0;

    // Peek the existing QNetworkDiskCache (same NAM, same effective key). No
    // socket and no permit: cooldown cache-only reuse is free.
    virtual bool coverCacheContains(const QUrl &effectiveUrl) = 0;
};

// ── Shared app-wide state ────────────────────────────────────────────────────

class CoverDeliveryState : public QObject
{
    Q_OBJECT
public:
    enum class Admission { Admitted, Queued, Overflow };

    explicit CoverDeliveryState(QObject *parent = nullptr);
    ~CoverDeliveryState() override;

    // Classification. Static form = production patterns. Instance form adds the
    // explicit test-host set (harness loopback hosts only; production never
    // calls addManagedHostForTesting).
    static bool isManagedCoverUrl(const QUrl &url);
    bool isManagedCover(const QUrl &url) const;
    void addManagedHostForTesting(const QString &host);
    static QString normalizeHost(const QString &host);
    static QByteArray urlDigest(const QUrl &url);   // sha256 of fragment-stripped URL

    // Test seams (limits/clock/fallback). Production defaults: cap 2,
    // 64 queued per host, 256 total, 300 s fallback.
    void setActiveCapForTesting(int cap);
    void setQueueLimitsForTesting(int perHost, int total);
    void setFallbackSecondsForTesting(int seconds);
    void setClockForTesting(std::function<qint64()> nowUtcMs);
    // Pause after promotion but before sending a wake, for deterministic teardown tests.
    void setBeforeWakeSignalForTesting(std::function<void()> hook);
    static RetryAfterDecision parseRetryAfter(const QByteArray &raw, qint64 nowUtcMs,
                                              int fallbackSeconds);

    // Cooldown surface.
    qint64 cooldownRemainingMs(const QString &host, const QByteArray &digest) const;
    void record429(const QString &host, const QByteArray &digest,
                   const QByteArray &retryAfterRaw, bool *usedFallback = nullptr);
    void noteSuccess(const QString &host, const QByteArray &digest);

    // Admission. acquire() never blocks: Admitted (caller starts now), Queued
    // (wake via DeferredCoverReply::onGranted) or Overflow (local failure).
    Admission acquire(const QString &host, class DeferredCoverReply *reply);
    // Idempotent removal: active completion, queued abort, reply destruction.
    void forget(class DeferredCoverReply *reply);
    // Called on the reply's own thread once its context-bound wake is delivered.
    void acknowledgeGrant(class DeferredCoverReply *reply);

    // Ledger. Bound: 128 hosts / 2048 URL digests / 256 KiB file.
    bool loadLedger(const QString &path);
    bool saveLedgerNow();
    void scheduleLedgerSave();
    QString ledgerPath() const;
    int ledgerHostCount() const;
    int ledgerUrlCount() const;

signals:
    // Connections are bound to the receiving reply's QObject lifetime. Only
    // integer tickets cross threads; no raw receiver is dereferenced to post.
    void permitGranted(quint64 ticket);

private:
    struct PendingWake { quint64 ticket = 0; };
    struct WakeConnection {
        quint64 ticket = 0;
        QMetaObject::Connection connection;
    };

    void pumpNextLocked(const QString &host, QList<PendingWake> &wakes);
    qint64 nowUtcMs() const;
    qint64 nowMonoMs() const;
    void pruneExpiredLocked(qint64 nowMono, qint64 nowUtc);
    void postWakes(const QList<PendingWake> &wakes);
    void startSaveTimer();

    mutable QMutex m_mutex;
    // Serializes atomic ledger file replacements without holding the queue/state mutex.
    QMutex m_ledgerIoMutex;
    quint64 m_ledgerRevision = 0;

    // Queues + active accounting (all under m_mutex).
    int m_activeCap = 2;
    int m_perHostQueueCap = 64;
    int m_totalQueueCap = 256;
    QHash<QString, int> m_activeCount;
    QHash<QString, QList<class DeferredCoverReply *>> m_queues;
    QHash<class DeferredCoverReply *, QString> m_activeReplies;
    QHash<class DeferredCoverReply *, WakeConnection> m_wakeConnections;
    quint64 m_lastWakeTicket = 0;
    std::function<void()> m_beforeWakeSignalForTesting;
    int m_totalQueued = 0;

    // Cooldowns, monotonic deadline form for in-run math.
    int m_fallbackSeconds = 300;
    QHash<QString, qint64> m_hostDeadlineMono;
    QHash<QByteArray, qint64> m_urlDeadlineMono;
    qint64 m_laneDeadlineMono = 0;
    QHash<QString, qint64> m_testManagedHosts;

    // Ledger.
    QString m_ledgerPath;
    bool m_dirty = false;
    QTimer *m_saveTimer = nullptr;
    std::atomic_bool m_saveArmed{false};

    std::function<qint64()> m_clockUtcMs;   // injected; empty = wall clock
    QElapsedTimer m_monoOrigin;
    qint64 m_utcAtMonoOrigin = 0;
};

// ── The outer reply facade ───────────────────────────────────────────────────

// Returned synchronously by CachingNam::createRequest for admitted cover GETs.
// Streams the inner reply byte-for-byte (sequential), preserves metadata/
// errors/redirects, releases its permit exactly once, and finishes synthetic
// outcomes on a later event-loop turn so callers can attach spies.
class DeferredCoverReply : public QNetworkReply
{
public:
    DeferredCoverReply(CoverDeliveryState *state, CoverDispatcher *dispatcher,
                       QNetworkAccessManager::Operation op, const QNetworkRequest &original,
                       const QNetworkRequest &prepared, QIODevice *outgoing,
                       QObject *parent = nullptr);
    ~DeferredCoverReply() override;

    void begin();        // admission decision; called once by createRequest
    void onGranted();    // posted by the state when a queue slot frees

    void abort() override;
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }
    void ignoreSslErrors();
    void ignoreSslErrors(const QList<QSslError> &errors);

protected:
    qint64 readData(char *data, qint64 maxlen) override;

private:
    enum class Phase { Queued, HopQueued, Active, Done };

    void startInner(QNetworkRequest::CacheLoadControl load, bool needsPermit);
    void discardInner();
    void onInnerFinished();
    void onInnerRedirected(const QUrl &target);
    void hopTo(const QUrl &target, const QString &destHost);
    void copyMetaFromInner(bool forceEmit);
    void finalizeSynthetic(QNetworkReply::NetworkError error, const QString &message,
                           int httpStatus, qint64 retryAfterSeconds);
    void scheduleSynthetic(QNetworkReply::NetworkError error, const QString &message,
                           int httpStatus, qint64 retryAfterSeconds);
    void releasePermit();

    static QString cooldownMessage(const QString &host, qint64 remainingMs);

    CoverDeliveryState *m_state = nullptr;
    CoverDispatcher *m_dispatcher = nullptr;
    QNetworkAccessManager::Operation m_op;
    QNetworkRequest m_original;
    QNetworkRequest m_prepared;
    QIODevice *m_outgoing = nullptr;
    QUrl m_logicalUrl;
    QString m_logicalHost;
    QByteArray m_digest;
    QNetworkReply *m_inner = nullptr;
    bool m_admitted = false;
    bool m_aborted = false;
    bool m_done = false;
    bool m_metaSeen = false;
    bool m_syntheticScheduled = false;
    Phase m_phase = Phase::Queued;
};
