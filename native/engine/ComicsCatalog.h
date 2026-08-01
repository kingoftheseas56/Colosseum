#pragma once
// ComicsCatalog — read-only seam onto the availability-first SQLite catalogue
// (spec 2026-07-17 phase 2b). QML paints, C++ decides: the db, its schema, and
// query shape live here; QML gets QVariant maps/lists. Point queries are indexed
// and sub-ms at this scale (21.5k series / 53k downloads), so the API is
// synchronous — no async ceremony a 31 MB local file doesn't need.
// Missing/invalid db => ready()==false and every accessor returns empty (the app
// runs on without the catalogue — it is pipeline-deployed, not shipped).
#include <QObject>
#include <QSqlDatabase>
#include <QVariantList>
#include <QVariantMap>

class ComicsCatalog final : public QObject {
    Q_OBJECT
public:
    explicit ComicsCatalog(const QString& dbPath, QObject* parent = nullptr);
    ~ComicsCatalog() override;
    Q_INVOKABLE bool ready() const { return m_ok; }
    // {gcdId,title,year,yearEnded,issueCount,publisher,cover,synopsis,downloads,kinds,latestPost} or {}
    Q_INVOKABLE QVariantMap series(int gcdId) const;
    // ranked: exact-title class first, then prefix, then contains; downloads DESC within class
    // rows: {gcdId,title,year,yearEnded,publisher,cover,downloads}
    Q_INVOKABLE QVariantList search(const QString& text, int limit = 30) const;
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
    // lowest-rank curated_series with norm_title = normTitle -> {locgId,title,cover,publisher} or {}
    Q_INVOKABLE QVariantMap curatedByNorm(const QString& normTitle) const;
    // per genre: {name,count,covers} covers = up to maxCovers cover urls ordered by rank
    // (empty covers skipped); genres ordered by count DESC then name ASC
    Q_INVOKABLE QVariantList curatedGenreShelves(int maxCovers) const;
    // EXISTS curated_edition where locg_id=:id AND available=1 AND getcomics_post != ''
    Q_INVOKABLE bool curatedHasDownloadable(const QString& locgId) const;

    // --- Tankoban Discover (spec 2026-08-01): paged, allowlisted, fully-bound
    //     discovery + house ranking over the curated (locg_id) catalogue. ---
    // axis: "genre" (groups curated_genre.genre) | "publisher" (groups
    // curated_series.publisher, blank publisher EXCLUDED). Returns {key,label,count}
    // ordered count DESC then label ASC. includeExplicit is accepted for interface
    // parity with the manga (MalCatalog) / Task-9 lanes but is a deliberate NO-OP
    // here: the curated comics catalogue carries NO adult/explicit classification
    // (no column, no maturity genre — it is the mainstream LOCG list), so nothing is
    // ever gated and results are identical whether it is true or false. Any other
    // axis returns an empty list.
    Q_INVOKABLE QVariantList discoverFilters(const QString& axis, bool includeExplicit) const;
    // catalogId ∈ {popular, new-releases, most-stocked, all}; filterAxis ∈
    // {genre, publisher, ""}. filterKey is BOUND (never concatenated). offset is
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

    // --- shelf (browse-landing) — same row shape as search() ---
    // kind: "stocked" | "publisher" (arg=publisher) | "decade" (arg="2010" -> [2010,2019])
    //     | "deep" (downloads>=10) | "fanmade" (series w/ a fan_made download)
    // all join series_stats, downloads DESC then year DESC, LIMIT limit.
    // unknown kind or !ready() -> empty.
    Q_INVOKABLE QVariantList shelf(const QString& kind, const QString& arg, int limit) const;
private:
    QSqlDatabase m_db;
    QString m_conn;
    bool m_ok = false;
};
