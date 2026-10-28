#pragma once
// ComicsCatalog — read-only seam onto the availability-first SQLite catalogue
// (spec 2026-07-17 phase 2b). QML paints, C++ decides: the db, its schema, and
// query shape live here; QML gets QVariant maps/lists. Point queries are indexed
// and sub-ms at this scale (21.5k series / 53k downloads), so the API is
// synchronous — no async ceremony a 31 MB local file doesn't need.
// Missing/invalid db => ready()==false and every accessor returns empty (the app
// runs on without the catalogue — it is pipeline-deployed, not shipped).
#include <QObject>
#include "CatalogRemote.h"
#include <QHash>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>

class ComicsCatalog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool remoteEnabled READ remoteEnabled NOTIFY revisionChanged)
    // No Q_PROPERTY(ready) here on purpose — see MalCatalog.h for the full note
    // (data-vault Slice 4, 2026-08-22 fix): it collided with the Q_INVOKABLE of the
    // same name and broke every `.ready()` call site in QML.
public:
    explicit ComicsCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~ComicsCatalog() override;
    Q_INVOKABLE bool ready() const { return m_ok; }
    // Service availability is distinct from complete local SQLite readiness.
    Q_INVOKABLE bool available() const { return m_ok || m_remote->available(); }
    bool remoteEnabled() const { return m_remote->enabled(); }
    int revision() const { return m_remote->revision(); }
    Q_INVOKABLE bool remotePending() const { return m_remote->pending(); }
    Q_INVOKABLE QString remoteQueryKey(const QString& method, const QVariantList& args) const { return m_remote->queryKey(method, args); }
    Q_INVOKABLE QVariantMap landing(const QString& tab) const { return m_ok ? QVariantMap{} : m_remote->read(QStringLiteral("landing"), {tab}, QVariantMap{}).toMap(); }
    Q_INVOKABLE QVariantMap landingState(const QString& tab) const { auto state=m_remote->queryState(QStringLiteral("landing"), {tab}, QVariantMap{}); state.insert(QStringLiteral("source"),m_ok?QStringLiteral("local"):QStringLiteral("remote")); return state; }
    Q_INVOKABLE bool remoteWaiting() const { return m_remote->waiting(); }
    void configureRemote(QNetworkAccessManager* nam, const QUrl& endpoint, const QString& cachePath = {}) {
        m_remote->configure(nam, endpoint, cachePath);
    }

    // Data-vault Slice 2 (2026-08-22): reopen at a fresh path — see MalCatalog.
    // Unlike the other three seams, ComicsCatalog has no ../../ dev fallback
    // ladder — the caller (main.cpp) resolves the full path first.
    Q_INVOKABLE bool reopen(const QString& dbPath);
    Q_INVOKABLE void closeForSwap();
    // {gcdId,title,year,yearEnded,issueCount,publisher,cover,synopsis,downloads,kinds,latestPost} or {}
    Q_INVOKABLE QVariantMap series(int gcdId) const;
    // ranked: exact-title class first, then prefix, then contains; downloads DESC within class
    // rows: {gcdId,title,year,yearEnded,publisher,cover,downloads}
    Q_INVOKABLE QVariantList search(const QString& text, int limit = 30) const;
    Q_INVOKABLE QVariantMap searchState(const QString& text, int limit = 30) const;
    // {postId,title,link,date,kind,method,fanMade,yearStart,mirrors:[{url,host,label}]} date DESC
    Q_INVOKABLE QVariantList downloadsFor(int gcdId) const;
    // every series whose title equals text (case-insensitive), downloads DESC —
    // lets QML decide "unambiguous title -> run page, else live franchise shelf"
    Q_INVOKABLE QVariantList exactMatches(const QString& text) const;

    // --- curated catalog (locg_id-keyed, separate lane from series/download above) ---
    // curated_series table exists and has rows
    Q_INVOKABLE bool curatedReady() const;
    // rank ASC: {locgId,rank,title,year,slug,publisher,cover,genres} genres = comma-joined
    Q_INVOKABLE QVariantList curatedRanked() const;
    // series row (same keys + synopsis) plus editions: QVariantList of curated_edition
    // rows (id ASC) each {title,displayTitle,format,collects,isbn,pages,published,chid,
    // cover,available,getcomicsPost,creators,description}. {} when missing.
    Q_INVOKABLE QVariantMap curatedSeries(const QString& locgId) const;
    // Arc 21 exact acquisition/coverage summary or {} on an older catalogue.
    Q_INVOKABLE QVariantMap curatedCoverage(const QString& locgId) const;
    // lowest-rank curated_series with norm_title = normTitle -> {locgId,title,cover,publisher} or {}
    Q_INVOKABLE QVariantMap curatedByNorm(const QString& normTitle) const;
    // per genre: {name,count,covers} covers = up to maxCovers cover urls ordered by rank
    // (empty covers skipped); genres ordered by count DESC then name ASC
    Q_INVOKABLE QVariantList curatedGenreShelves(int maxCovers) const;
    // EXISTS curated_edition where locg_id=:id AND available=1 AND getcomics_post != ''
    Q_INVOKABLE bool curatedHasDownloadable(const QString& locgId) const;

    // --- Tankoban Discover (spec 2026-08-01): paged, allowlisted, fully-bound
    //     discovery + house ranking over the curated (locg_id) catalogue. ---
    // axis: "genre" | "publisher" | "format" | "availability". Availability
    // is derived from Arc 21 exact coverage when present. Returns {key,label,count}
    // ordered count DESC then label ASC. includeExplicit is accepted for interface
    // parity with the manga (MalCatalog) / Task-9 lanes but is a deliberate NO-OP
    // here: the curated comics catalogue carries NO adult/explicit classification
    // (no column, no maturity genre — it is the mainstream LOCG list), so nothing is
    // ever gated and results are identical whether it is true or false. Any other
    // axis returns an empty list.
    Q_INVOKABLE QVariantList discoverFilters(const QString& axis, bool includeExplicit) const;
    // catalogId ∈ {popular, new-releases, most-stocked, all, recently-available,
    // complete-runs, near-complete, community-collections}; filterAxis ∈
    // {genre, publisher, format, availability, ""}. filterKey is BOUND (never concatenated). offset is
    // clamped >= 0, limit to [1,100]. Returns {items, nextOffset, exhausted,
    // freshness:"bundled"}. Each item carries {locgId, title, year, publisher, cover,
    // genres, availability(bool), houseScore, houseComponents, explicit:false}.
    //  * popular      = the house ranking (65% normalized LOCG rank, 20% acquisition
    //                   availability, 10% recent release activity, 5% metadata; when a
    //                   row has no LOCG rank the 65% redistributes proportionally
    //                   across availability+recency, metadata staying at its 0.05 base
    //                   ≤ 0.10 — never an arbitrary worst rank).
    //  * new-releases = newest real publication year (from curated_edition.published),
    //                   NOT rowid/modification order.
    //  * most-stocked = edition depth, house rank + availability as tie-breakers.
    //  * all          = alphabetical by normalized title then start year.
    // houseComponents {popularity,availability,recency,metadata} are the WEIGHTED
    // contributions summing to houseScore — debug/test only, never a public rating.
    // includeExplicit is the same conservative NO-OP as discoverFilters; availability
    // is a BOOST, never an inclusion gate (an unavailable title is never removed).
    // Unknown catalogId or filterAxis returns the same map with no items.
    Q_INVOKABLE QVariantMap discoverPage(const QString& catalogId,
                                         const QString& filterAxis,
                                         const QString& filterKey,
                                         bool includeExplicit,
                                         int offset, int limit) const;
    // Same page, any number of filters ANDed: filters = [{axis, key}, ...] (one per axis;
    // Discover sidebar: one filter per group). discoverPage() is this with one pair.
    Q_INVOKABLE QVariantMap discoverPageFiltered(const QString& catalogId,
                                                 const QVariantList& filters,
                                                 bool includeExplicit,
                                                 int offset, int limit) const;
    // Exact CatalogRemote query snapshot. Inspecting state never starts a request.
    Q_INVOKABLE QVariantMap discoverPageFilteredState(const QString& catalogId,
                                                      const QVariantList& filters,
                                                      bool includeExplicit,
                                                      int offset, int limit) const;
    // Explicit retry of this failed page key; does not restart unrelated pages.
    Q_INVOKABLE bool retryDiscoverPageFiltered(const QString& catalogId,
                                               const QVariantList& filters,
                                               bool includeExplicit,
                                               int offset, int limit);

    // --- shelf (browse-landing) — same row shape as search() ---
    // kind: "stocked" | "publisher" (arg=publisher) | "decade" (arg="2010" -> [2010,2019])
    //     | "deep" (downloads>=10) | "fanmade" (series w/ a fan_made download)
    // all join series_stats, downloads DESC then year DESC, LIMIT limit.
    // unknown kind or !ready() -> empty.
    Q_INVOKABLE QVariantList shelf(const QString& kind, const QString& arg, int limit) const;
signals:
    void readyChanged();
    void revisionChanged();
    void catalogueSourceChanged();
    void requestFailed(QString method, QString reason);
    // Subscribers must compare this key to discoverPageFilteredState(...).key.
    void remoteQueryChanged(QString key);

private:
    bool openAt(const QString& dbPath);
    QVariantList discoverFiltersUncached(const QString& axis) const;

    CatalogRemote* m_remote;
    QSqlDatabase m_db;
    QString m_conn;
    bool m_ok = false;    // discoverFilters() answers per axis/explicit flag, computed once per opened database:
    // each is a GROUP BY over the whole catalogue (~0.45 s) and Discover asks repeatedly.
    mutable QHash<QString, QVariantList> m_filterCache;
};
