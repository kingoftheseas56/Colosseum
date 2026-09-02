#pragma once

// BiblioCatalog — the QML-facing daily keyless refresh coordinator for the
// Biblio Discover / Explore catalogue (spec 2026-08-01, plan 2026-08-03 Task
// 4). It owns the writable BiblioCatalogStore snapshot, fetches Apple Books
// RSS + Apple Search + Open Library records through an injectable transport
// seam, canonicalizes and ranks them (BiblioCanonicalizer / BiblioRanking),
// and publishes a validated snapshot at most once per local day. QML never
// computes rankings or talks to the network directly — it only reads the
// properties/invokables below.
//
// Threading: networking and UI-visible state stay on the GUI thread. Provider
// parsing, canonicalization, ranking, and the atomic SQLite snapshot publish run
// on one dedicated worker lane. The GUI-owned store remains the read/lazy-write
// connection; snapshot publishing uses a separate worker-owned SQLite connection.
// Requests are bounded to kMaxConcurrent in flight at a time;
// transient failures (network error, 429, 5xx) retry with a bounded linear
// backoff (kMaxRetries); a forced or day-due refresh that starts while a
// prior one is still running cancels that prior generation's outstanding
// requests first so results never straddle two refreshes.
//
// Failure posture (spec §8): publish() only ever runs against a COMPLETE
// normalized snapshot, and only once at least one provider produced usable
// records. A refresh that retrieves nothing (or whose snapshot fails
// BiblioCatalogStore::publish's own validation) leaves the previously
// published snapshot fully intact — `ready` only ever goes true→false when
// there was never a successful publish in the first place.
//
// Lazy grid enrichment (plan 2026-08-15 Slice 4): requestEnrichment(catalogId)
// is the See-All grids' first-view hook. It pages the CURRENT published
// ranking for that catalogue, picks the works whose artwork/rating evidence
// is absent, and issues its OWN bounded apple-search burst
// (kLazyEnrichmentBudget) through the transport — a separate reply handler
// that only writes BiblioCatalogStore's biblio_pending_enrichment rows and
// never touches the refresh generation machinery (m_records/m_queue/
// m_generation, refreshing, ready, revision, or the daily jobs). Parked
// outcomes: 'enriched' bodies wait raw to fold into the NEXT daily publish
// (they merge onto the right canonical works by identity, and are cleared
// once that publish succeeds), 'pending' is the crash-safe in-flight marker,
// 'unavailable' re-attempts at most once per local day. The daily refresh
// consults those rows too — covered works skip the apple-search half of the
// enrichment fan-out so the daily pass stays preview-priority honest. The
// published snapshot, its revision, and `ready` are never touched by the
// lazy path; a burst is idempotent per (session, catalogue).

#include "BiblioCanonicalizer.h"
#include "BiblioCatalogStore.h"
#include "BiblioCatalogTypes.h"
#include "BiblioProviders.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QQueue>
#include <QSet>
#include <QThreadPool>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

// ---------------------------------------------------------------------------
// Injectable transport seam (Task 4 fixed contract). This is the ONLY new
// abstraction Task 4 introduces: production wires BiblioNetworkTransport
// (QNetworkAccessManager-backed, below); the service harness wires a fake
// that scripts responses, holds/delays replies, and records request order —
// there is no generic "HTTP client factory" and no separate retry-policy
// class hierarchy; retry/backoff/concurrency-cap logic are plain loops
// inside BiblioCatalog itself.
// ---------------------------------------------------------------------------

// One in-flight (or completed) request the transport hands back. `finished`
// fires exactly once per reply that is not cancelled before completion:
// httpStatus is 0 with a non-empty `error` for a transport-level failure
// (timeout, DNS, connection refused) — distinct from a valid-but-unsuccessful
// HTTP status (404, 429, 500...), which arrives as a non-zero httpStatus with
// an empty error. BiblioCatalog owns every reply it receives from get() and
// deletes it (deleteLater) once its outcome has been handled.
class BiblioTransportReply : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~BiblioTransportReply() override = default;

    // Abort an in-flight request. No-op if already finished. A caller that
    // cancels and no longer cares about the outcome (BiblioCatalog dropping
    // an obsolete generation) must not depend on `finished` arriving after
    // cancel() — it may or may not, and either is a legal implementation.
    virtual void cancel() = 0;

signals:
    void finished(int httpStatus, const QByteArray &body, const QString &error);
};

// The transport seam itself. Constructor-injected into BiblioCatalog.
class IBiblioTransport {
public:
    virtual ~IBiblioTransport() = default;

    // Issue a keyless GET. Ownership: the caller (BiblioCatalog) owns the
    // returned reply and is responsible for deleting it once finished() has
    // been observed (or the request has been cancelled and abandoned).
    virtual BiblioTransportReply *get(const QUrl &url, const QVariantMap &headers) = 0;
};

// Production transport: a single QNetworkAccessManager, keyless GET only. Any
// non-2xx HTTP response still completes `finished` with that status (never
// treated as a transport-level `error`); only a QNetworkReply::NetworkError
// != NoError (DNS, timeout, refused, aborted...) populates `error`.
class BiblioNetworkTransport : public QObject, public IBiblioTransport {
    Q_OBJECT
public:
    explicit BiblioNetworkTransport(QObject *parent = nullptr);

    BiblioTransportReply *get(const QUrl &url, const QVariantMap &headers) override;

private:
    QNetworkAccessManager *m_nam = nullptr;
};

// ---------------------------------------------------------------------------
// BiblioCatalog
// ---------------------------------------------------------------------------
class BiblioCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY refreshingChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY staleChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY offlineChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QDateTime lastSuccessfulRefresh READ lastSuccessfulRefresh NOTIFY lastSuccessfulRefreshChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    // dbPath: writable SQLite path; the parent directory is created if
    // absent. transport: inject a fake for tests. When null (the production
    // default used by main.cpp), BiblioCatalog constructs and OWNS its own
    // QNetworkAccessManager-backed transport. When a non-null transport is
    // passed in, BiblioCatalog does NOT take ownership — the caller (harness)
    // is responsible for its lifetime, which must outlive this object.
    explicit BiblioCatalog(const QString &dbPath, IBiblioTransport *transport = nullptr,
                           QObject *parent = nullptr);
    ~BiblioCatalog() override;

    bool ready() const { return m_ready; }
    bool refreshing() const { return m_refreshing; }
    bool stale() const { return m_stale; }
    bool offline() const { return m_offline; }
    int revision() const { return m_revision; }
    QDateTime lastSuccessfulRefresh() const { return m_lastSuccessfulRefresh; }
    QString lastError() const { return m_lastError; }

    // Start a refresh when one is due: no successful (or completed) refresh
    // attempt has happened yet today (local date) and no refresh is already
    // in flight. Calling this while a refresh is already running is a no-op
    // (coalesced) UNLESS force is true, in which case the running
    // generation's outstanding requests are cancelled and a fresh one starts
    // immediately, day-gate or not. Never blocks — safe to call from
    // main.cpp right after construction.
    Q_INVOKABLE void refreshIfDue(bool force = false);
    Q_INVOKABLE void setBackgroundWorkSuspended(bool suspended);
    void setForegroundPriorityActive(bool active);

    // Proxies BiblioCatalogStore::page() in the identical {items,nextOffset,
    // exhausted,freshness,warning} shape (the DiscoverBrowser contract).
    Q_INVOKABLE QVariantMap discoverPage(const QString &catalogId, const QString &facetAxis,
                                         const QString &facetKey, bool includeExplicit,
                                         int offset, int limit) const;

    // The controlled filter axes/values (BiblioCatalogStore::filterGroups).
    Q_INVOKABLE QVariantList filterGroups(bool includeExplicit) const;

    // One row per house catalogue, in the fixed order [popular, top-rated,
    // new-releases, trending, most-read, classics]: {catalogId, items}.
    // `limitPerShelf` caps each shelf (clamped to [1,100] by the store).
    Q_INVOKABLE QVariantList exploreRows(int limitPerShelf, bool includeExplicit) const;

    // Items for one mosaic, addressed by a controlled FACET KEY (e.g.
    // "nonfiction", "young-adult", "science-fiction" — any key
    // BiblioTaxonomy::filterGroups() advertises under genre/audience/theme/
    // setting/period/language; NOT an axis name). Resolves the owning axis
    // internally and returns BiblioCatalogStore::page("popular", axis, key,
    // includeExplicit, 0, limit).items. An unknown key returns an empty list.
    Q_INVOKABLE QVariantList mosaic(const QString &facetKey, int limit, bool includeExplicit) const;

    // Lazy grid enrichment (plan 2026-08-15 Slice 4): QML calls this once per
    // catalogue grid on first view. Pages the CURRENT published ranking for
    // `catalogId` and issues a bounded apple-search burst (at most
    // kLazyEnrichmentBudget requests) for the works whose artwork/rating
    // evidence is absent — skipping works already parked 'enriched'/'pending'
    // and 'unavailable' ones already attempted today. Candidates beyond the
    // burst cap are simply not requested (no 'pending' row is written for
    // them — a marker without an in-flight request would block the next day
    // for no reason). Idempotent per (session, catalogId): a second call for
    // the same catalogue is a no-op. Results land in the store's
    // biblio_pending_enrichment table and fold into the NEXT daily publish —
    // the published snapshot, its revision, and `ready` are untouched. Safe
    // to call while a daily refresh is running (fully separate machinery).
    Q_INVOKABLE void requestEnrichment(const QString &catalogId);

signals:
    void readyChanged();
    void refreshingChanged();
    void staleChanged();
    void offlineChanged();
    void revisionChanged();
    void lastSuccessfulRefreshChanged();
    void lastErrorChanged();
    // Emitted exactly once per refresh attempt that actually ran (a no-op
    // coalesced/day-gated call never emits this). success is whether a new
    // snapshot was published.
    void refreshFinished(bool success);

private:
    struct FetchJob {
        QUrl url;
        QString kind;       // "apple-rss" | "apple-search" | "openlibrary"
                            // | "openlibrary-trending" | "openlibrary-classics"
                            // | "openlibrary-subject"
        QString facetAxis;  // optional facet hint carried by genre-tagged
                            // apple-rss feeds and openlibrary-subject jobs
        QString facetKey;
        int attempt = 0;
    };

    void setReady(bool v);
    void setRefreshing(bool v);
    void setStale(bool v);
    void setOffline(bool v);
    void setLastError(const QString &v);
    void recomputeStale();

    void startRefresh();
    void enqueue(int generation, const QUrl &url, const QString &kind,
                const QString &facetAxis = QString(), const QString &facetKey = QString());
    // Bounded per-refresh enrichment fan-out for one freshly parsed record:
    // dedupes by folded title+author (m_enrichedCandidates), decrements
    // m_enrichmentRemaining, and enqueues an apple-search job. When
    // alsoOpenLibrary is true (the apple-rss path — records that are NOT
    // already Open Library data) it additionally enqueues the Open Library
    // title/author search; the openlibrary-* branches pass false because the
    // work already IS Open Library data — re-searching it would only burn one
    // of the four concurrency slots.
    void enqueueEnrichment(int generation, const BiblioSourceRecord &record,
                          bool alsoOpenLibrary);
    void pumpQueue(int generation);
    void issueRequest(int generation, const FetchJob &job);
    void onReplyFinished(BiblioTransportReply *reply, int status, const QByteArray &body,
                        const QString &error);
    // ── Lazy grid enrichment (Slice 4) ── fully separate from the refresh
    // generation machinery above: no queue, no concurrency accounting, no
    // retry — the burst is bounded by kLazyEnrichmentBudget and the replies
    // only write biblio_pending_enrichment rows.
    void issueLazyEnrichment(const QString &workKey, const QString &title);
    void onLazyReplyFinished(BiblioTransportReply *reply, int status,
                            const QByteArray &body, const QString &error);
    void parseReplyAsync(int generation, const FetchJob &job, const QByteArray &body);
    void applyParsedSuccess(int generation, const FetchJob &job,
                            const QList<BiblioSourceRecord> &records);
    void scheduleRetry(int generation, FetchJob job);
    void cancelGeneration(int generation);
    void finalizeGeneration(int generation);
    void finishGeneration(int generation, bool success, const QString &failureReason,
                          const QDateTime &publishedAt = QDateTime());
    void maybeFinalize(int generation);
    QString axisForFacetKey(const QString &key) const;
    // Reconciles the generation's accumulated records into a complete
    // candidate BiblioCatalogSnapshot: canonical works/editions/sources,
    // derived + hinted facets, the four chart catalogues' rankings (reading
    // prior ranking-history from m_store plus today's fresh demand reading),
    // and — from Slice 3's seeding flip — the "most-read" / "classics" ranking
    // rows. Those two come from the generation's payload-order sourceId lists
    // (m_mostReadOrder / m_classicsOrder in the caller, passed by value so this
    // stays const): each sourceId is mapped to its canonical work via the
    // canonicalizer's fieldSources provenance, and rank/score follow payload
    // position — NOT BiblioRanking over the merged pool, because
    // openLibraryPopularity mixes incommensurable scales (trending payload
    // index vs all-time readinglog counts) and a chart-pool work with a huge
    // readinglog must never contaminate the daily list.
    static BiblioCatalogSnapshot buildSnapshot(
        const QList<BiblioCanonicalWork> &canonical,
        const QStringList &mostReadOrder, const QStringList &classicsOrder,
        const QHash<QString, QList<QPair<QString, QString>>> &facetHints,
        const QList<BiblioRankSnapshot> &priorHistory, const QDateTime &capturedAt);

    BiblioCatalogStore m_store;
    QString m_dbPath; // immutable DB identity used by worker-owned SQLite publisher connections
    IBiblioTransport *m_transport = nullptr;
    BiblioNetworkTransport *m_ownedTransport = nullptr; // non-null only when we own it
    QThreadPool m_cpuPool; // one serialized lane: parse/canonicalize/rank/publish, never GUI

    bool m_ready = false;
    bool m_refreshing = false;
    bool m_stale = false;
    bool m_offline = false;
    int m_revision = 0;
    QDateTime m_lastSuccessfulRefresh;
    QString m_lastError;

    int m_generation = 0;
    QDate m_lastAttemptDate; // local gate, seeded from persisted last successful publish on restart
    bool m_backgroundWorkSuspended = false;
    bool m_resumeRefreshWhenUnsuspended = false;
    bool m_foregroundPriorityActive = false;
    bool m_resumeRefreshWhenForegroundIdle = false;

    QQueue<FetchJob> m_queue;
    int m_activeCount = 0;
    int m_pendingRetries = 0;
    int m_pendingParses = 0;
    bool m_finalizing = false;
    QSet<QString> m_requestedUrls;        // dedupe within one generation
    QSet<QString> m_enrichedCandidates;   // dedupe apple-search/openlibrary fan-out
    int m_enrichmentRemaining = 0;
    QHash<BiblioTransportReply *, QPair<FetchJob, int>> m_inFlight; // reply -> (job, generation)
    QList<BiblioSourceRecord> m_records;  // accumulated for the in-flight generation
    // Payload-order sourceIds of the generation's openlibrary-trending /
    // openlibrary-classics records (the order the provider ranked them in —
    // the only trustworthy ordering source for the two payload-ordered
    // catalogues). Reset in startRefresh; consumed by buildSnapshot via
    // finalizeGeneration.
    QStringList m_mostReadOrder;
    QStringList m_classicsOrder;
    // sourceId -> facet hints (axis,key) gathered from genre/audience-tagged
    // RSS feeds and openlibrary-subject jobs; applied to whichever canonical
    // work field-sources from it.
    QHash<QString, QList<QPair<QString, QString>>> m_facetHints;
    bool m_anySuccess = false;

    // ── Lazy grid enrichment (plan 2026-08-15 Slice 4) ──
    // One burst per (session, catalogue): requestEnrichment marks the
    // catalogue here on entry, so repeat calls are a no-op.
    QSet<QString> m_lazyEnrichedCatalogs;
    QSet<QString> m_deferredEnrichmentCatalogs;
    // The lazy path's own in-flight replies (never mixed into m_inFlight —
    // cancelGeneration must not reach them). Maps reply -> pending work_key.
    QHash<BiblioTransportReply *, QString> m_lazyInFlight;
    // Pending-enrichment rows as of THIS generation's start; the daily
    // fan-out (enqueueEnrichment) consults the snapshot so works whose
    // artwork is parked ('enriched') or in flight ('pending') skip the
    // apple-search half and 'unavailable' works respect the ≤1/day gate.
    // Refresh-generation state — reset in startRefresh, never touched by the
    // lazy path (which always reads the store directly).
    QHash<QString, BiblioPendingEnrichmentRow> m_pendingEnrichmentAtStart;
};
