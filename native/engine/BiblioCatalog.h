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
// Threading: everything here runs on the GUI thread, event-driven (no worker
// threads). Requests are bounded to kMaxConcurrent in flight at a time;
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

    // Proxies BiblioCatalogStore::page() in the identical {items,nextOffset,
    // exhausted,freshness,warning} shape (the DiscoverBrowser contract).
    Q_INVOKABLE QVariantMap discoverPage(const QString &catalogId, const QString &facetAxis,
                                         const QString &facetKey, bool includeExplicit,
                                         int offset, int limit) const;

    // The controlled filter axes/values (BiblioCatalogStore::filterGroups).
    Q_INVOKABLE QVariantList filterGroups(bool includeExplicit) const;

    // One row per house catalogue, in the fixed order [popular, top-rated,
    // new-releases, trending]: {catalogId, items}. `limitPerShelf` caps each
    // shelf (clamped to [1,100] by the store).
    Q_INVOKABLE QVariantList exploreRows(int limitPerShelf, bool includeExplicit) const;

    // Items for one mosaic, addressed by a controlled FACET KEY (e.g.
    // "nonfiction", "young-adult", "science-fiction" — any key
    // BiblioTaxonomy::filterGroups() advertises under genre/audience/theme/
    // setting/period/language; NOT an axis name). Resolves the owning axis
    // internally and returns BiblioCatalogStore::page("popular", axis, key,
    // includeExplicit, 0, limit).items. An unknown key returns an empty list.
    Q_INVOKABLE QVariantList mosaic(const QString &facetKey, int limit, bool includeExplicit) const;

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
        QString facetAxis;  // optional facet hint carried by genre-tagged apple-rss feeds
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
    void pumpQueue(int generation);
    void issueRequest(int generation, const FetchJob &job);
    void onReplyFinished(BiblioTransportReply *reply, int status, const QByteArray &body,
                        const QString &error);
    void handleSuccess(int generation, const FetchJob &job, const QByteArray &body);
    void scheduleRetry(int generation, FetchJob job);
    void cancelGeneration(int generation);
    void finalizeGeneration(int generation);
    void maybeFinalize(int generation);
    QString axisForFacetKey(const QString &key) const;
    // Reconciles the generation's accumulated records into a complete
    // candidate BiblioCatalogSnapshot: canonical works/editions/sources,
    // derived + hinted facets, and the four house-catalogue rankings (reading
    // prior ranking-history from m_store plus today's fresh demand reading).
    BiblioCatalogSnapshot buildSnapshot(const QList<BiblioCanonicalWork> &canonical) const;

    BiblioCatalogStore m_store;
    IBiblioTransport *m_transport = nullptr;
    BiblioNetworkTransport *m_ownedTransport = nullptr; // non-null only when we own it

    bool m_ready = false;
    bool m_refreshing = false;
    bool m_stale = false;
    bool m_offline = false;
    int m_revision = 0;
    QDateTime m_lastSuccessfulRefresh;
    QString m_lastError;

    int m_generation = 0;
    QDate m_lastAttemptDate; // local date of the last COMPLETED refresh attempt (in-memory only)

    QQueue<FetchJob> m_queue;
    int m_activeCount = 0;
    int m_pendingRetries = 0;
    QSet<QString> m_requestedUrls;        // dedupe within one generation
    QSet<QString> m_enrichedCandidates;   // dedupe apple-search/openlibrary fan-out
    int m_enrichmentRemaining = 0;
    QHash<BiblioTransportReply *, QPair<FetchJob, int>> m_inFlight; // reply -> (job, generation)
    QList<BiblioSourceRecord> m_records;  // accumulated for the in-flight generation
    // sourceId -> facet hints (axis,key) gathered from genre/audience-tagged
    // RSS feeds; applied to whichever canonical work field-sources from it.
    QHash<QString, QList<QPair<QString, QString>>> m_facetHints;
    bool m_anySuccess = false;
};
