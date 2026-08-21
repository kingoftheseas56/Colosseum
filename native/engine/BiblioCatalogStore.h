#pragma once

// BiblioCatalogStore — atomic SQLite snapshot store for the Biblio Discover /
// Explore catalogue (spec 2026-08-01 §8, plan 2026-08-03 Task 3).
//
// The store is the durable cache behind the daily refresh pipeline. Task 4's
// catalogue service produces a complete, validated candidate snapshot and hands
// it to publish(); this class writes it into staging tables inside ONE
// transaction, re-validates referential integrity and controlled-vocabulary
// facets, and atomically swaps the active snapshot id. On any validation
// failure it rolls back and leaves the prior active snapshot fully intact, so a
// partial or failed refresh NEVER replaces the last valid cache (spec §8).
//
// Reads are synchronous point queries (the catalogue is small and local): page,
// filterGroups, previewRows, top10. Catalogue ids and facet axes are
// ALLOWLISTED and all caller-supplied strings (catalogId, facetAxis, facetKey)
// are BOUND — never concatenated into SQL — mirroring ComicsCatalog::discoverPage.
// Paging matches that exact {items,nextOffset,exhausted,freshness,warning}
// contract so the QML adapter is identical across worlds.

#include "BiblioCatalogTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// One concrete edition row persisted beneath a work. This is the store's own
// flat edition shape (canonicalId FK + the BiblioEdition fields) — kept here so
// the store compiles without pulling in the Task 2 canonicalizer.
struct BiblioCatalogEdition {
    QString editionId;
    QString language;
    bool    englishReadable = false;
    int     pageCount = 0;
    QString publisher;
    QDate   published;
    QString format;   // "print" | "ebook" | "audiobook" | ...
};

// One edition row paired with its parent canonicalId (the FK the `editions`
// table carries).
struct BiblioCatalogEditionRow {
    QString canonicalId;
    BiblioCatalogEdition edition;
};

// Per-field provenance: which source set which canonical field on a work, with
// that record's sourceId and observation time (spec §6.1: every merged field
// retains source provenance). Persisted into `work_sources`.
struct BiblioCatalogSource {
    QString   field;
    QString   source;     // "apple" | "openlibrary"
    QString   sourceId;
    QDateTime observedAt;
};
struct BiblioCatalogSourceRow {
    QString canonicalId;
    BiblioCatalogSource source;
};

// A controlled-vocabulary facet binding (axis + stable key from the taxonomy
// mapper) on a work. Unknown axis/key pairs are rejected on publish.
struct BiblioCatalogFacet {
    QString axis;   // genre | audience | theme | setting | period | length | era | language | publisher
    QString key;    // stable lowercase id from BiblioTaxonomy
};
struct BiblioCatalogFacetRow {
    QString canonicalId;
    BiblioCatalogFacet facet;
};

// A computed ranking for one catalogue + work: the score and 1-based rank
// position the ranking engine assigned. Persisted into `rankings`. Only the
// four chart catalogues carry computed scores; most-read/classics rows are
// payload-ordered by the seeder and land here with rank = payload position.
struct BiblioCatalogRanking {
    QString catalogId;    // popular | top-rated | new-releases | trending | most-read | classics
    QString canonicalId;
    double  score = 0.0;
    int     rank = 0;     // 1-based
};

// One dated daily demand reading (spec §7 Trending's seven-day delta source).
// `ranking_history` keeps the most recent eight daily snapshots per work.
struct BiblioCatalogHistory {
    QString   canonicalId;
    QDateTime capturedAt;
    double    demandScore = 0.0;
};

// One lazy-enrichment parking row (plan 2026-08-15 Slice 4): the raw
// apple-search body fetched when a See-All grid first came into view, waiting
// to fold into the NEXT daily publish. `workKey` is the enrichment candidate
// identity the daily path already uses (normalizedTitle + '|' +
// normalizedAuthor) — the folded record merges onto the right canonical work
// by identity, so no explicit canonicalId mapping is persisted. `state`:
//   "enriched"     — body carried usable record(s); folds into the next
//                    publish, row cleared after that publish succeeds;
//   "pending"      — request issued, reply not yet landed (crash-safe marker);
//   "unavailable"  — apple-search carried no usable record (empty parse or
//                    4xx); re-attempt gated to ≤1 per local day via
//                    `lastAttempt`.
struct BiblioPendingEnrichmentRow {
    QString   workKey;
    QByteArray body;      // raw apple-search response body (empty unless "enriched")
    QString   state;      // "enriched" | "pending" | "unavailable"
    QString   lastAttempt; // local ISO date (yyyy-MM-dd) of the recorded attempt
};

// The complete candidate snapshot Task 4's service produces and hands to
// publish(). It bundles the canonical works + nested editions + per-field
// provenance + controlled facet bindings + the four-catalogue computed rankings
// + the payload-ordered most-read/classics ranking rows (seeding flip, Slice 3)
// + the daily ranking-history readings + the capture timestamp. `explicitWorkIds`
// is the set of source-confirmed sexually-explicit canonical ids (spec §14/DoD
// #14: only those are gated by the global Explicit Content setting — Adult
// audience, horror, violence and mature-subject works are NOT explicit).
struct BiblioCatalogSnapshot {
    QDateTime capturedAt;
    QList<BiblioWork> works;
    QList<BiblioCatalogEditionRow> editions;
    QList<BiblioCatalogSourceRow> sources;
    QList<BiblioCatalogFacetRow> facets;
    QList<BiblioCatalogRanking> rankings;
    QList<BiblioCatalogHistory> history;
    QSet<QString> explicitWorkIds;
};

class BiblioCatalogStore {
public:
    BiblioCatalogStore();
    ~BiblioCatalogStore();

    BiblioCatalogStore(const BiblioCatalogStore &) = delete;
    BiblioCatalogStore &operator=(const BiblioCatalogStore &) = delete;

    // Open (creating if absent) the SQLite database at `path` and ensure the
    // seven snapshot tables plus the lazy-enrichment parking table
    // (biblio_pending_enrichment, Slice 4) exist and are at the expected
    // version. Returns false on any QSql error (the store stays closed;
    // accessors return empty). Idempotent: reopening an existing db does not
    // reset the schema or active snapshot; `create table if not exists` means
    // a database written before Slice 4 gains the pending table transparently
    // on its next open.
    bool open(const QString &path);

    // Atomically publish a complete candidate snapshot. Writes the whole payload
    // into staging tables inside one transaction, validates unique canonical ids,
    // edition/ranking foreign keys, and controlled-vocabulary facet keys, then
    // swaps the active snapshot id. On any validation failure the transaction is
    // rolled back and the prior active snapshot is left fully intact. Returns
    // true only when the new snapshot became active.
    bool publish(const BiblioCatalogSnapshot &snapshot);

    // Paged read in the ComicsCatalog::discoverPage shape:
    //   {items, nextOffset, exhausted, freshness, warning}.
    // catalogId is allowlisted to {popular, top-rated, new-releases, trending};
    // an unknown id returns an empty page (never reaches SQL). facetAxis is
    // allowlisted against the controlled axes (empty means "no filter");
    // facetKey is BOUND, never concatenated. offset is clamped >= 0, limit to
    // [1,100]. includeExplicit=false hides source-confirmed sexually-explicit
    // works only. `warning` carries a freshness/error string (empty when clean).
    QVariantMap page(const QString &catalogId, const QString &facetAxis,
                     const QString &facetKey, bool includeExplicit,
                     int offset, int limit) const;

    // The controlled filter axes/values the taxonomy advertises, gated by
    // explicit (publisher values are data-derived from the active snapshot).
    QVariantList filterGroups(bool includeExplicit) const;

    // Up to `limit` preview rows (publisher-sorted house preview), explicit-gated.
    QVariantList previewRows(int limit, bool includeExplicit) const;

    // Up to `limit` rank-1..limit rows for the Popular catalogue, explicit-gated.
    // Callers pass 10 to keep "Top 10" semantics; the cap is now parameterized
    // per the Task 3 interface (plan 2026-08-03); page() clamps limit to [1,100].
    QVariantList top10(int limit, bool includeExplicit) const;

    // The active snapshot's capture time (UTC), or an invalid QDateTime when no
    // snapshot has ever published successfully.
    QDateTime lastSuccessUtc() const;

    // True once at least one snapshot has been published successfully.
    bool hasSnapshot() const;

    // Diagnostic: the last warning/error string recorded by an open/publish/page
    // call (empty when the last operation was clean). Test/diagnostic surface so
    // a failed publish can be understood without a debugger.
    QString lastWarning() const { return m_lastWarning; }

    // Diagnostic: the retained daily ranking-history readings for `canonicalId`,
    // newest-first, after the eight-snapshot retention prune. Test-only window
    // into `ranking_history` (the service reads Trending through rankings, not
    // here). Each entry is {canonicalId, capturedAt, demandScore}.
    QVariantList rankingHistoryFor(const QString &canonicalId) const;

    // ── Lazy grid enrichment parking (plan 2026-08-15 Slice 4) ─────────────
    // The only window into biblio_pending_enrichment. Rows land here from
    // BiblioCatalog's lazy requestEnrichment path and fold into the NEXT
    // daily publish from the coordinator — never into the published snapshot
    // directly, so pending writes can never violate publish()'s atomicity
    // (the table sits outside the snapshot_id scoping and the active-snapshot
    // swap entirely).

    // Insert-or-replace one pending row (see BiblioPendingEnrichmentRow for
    // the state semantics). Returns false (with m_lastWarning set) on a SQL
    // error or when the store is not open.
    bool upsertPendingEnrichment(const QString &workKey, const QByteArray &body,
                                 const QString &state, const QString &lastAttempt);

    // The whole pending table keyed by work_key. The lazy path's population
    // is bounded by its per-session burst cap, so one full read is cheap and
    // keeps the caller's gate logic in one place. Empty on a closed store.
    QHash<QString, BiblioPendingEnrichmentRow> pendingEnrichmentMap() const;

    // Remove exactly the listed work_keys' rows — the ones a successful
    // publish folded in. Rows not listed (the 'unavailable' ≤1/day gate rows,
    // live 'pending' markers) are kept; unknown keys are not an error.
    // Returns false (m_lastWarning set) on a SQL error or a closed store.
    bool clearFoldedPending(const QStringList &workKeys);

private:
    bool ensureSchema();
    bool stagingTransaction(const BiblioCatalogSnapshot &snapshot);
    // Atomic active-snapshot-id + last_success_utc swap. Returns false (and sets
    // m_lastWarning) if either sync_meta upsert fails — the caller must roll back
    // and keep the prior active snapshot intact (spec §8).
    bool setActiveSnapshot(qint64 snapshotId, const QDateTime &capturedAt);

    // Validation helpers run inside the staging transaction. Each returns false
    // (leaving the caller to roll back) on the first invariant violation.
    bool validateUniqueCanonicalIds(const BiblioCatalogSnapshot &snapshot) const;
    bool validateFacetKeys(const BiblioCatalogSnapshot &snapshot) const;

    // True when `path` was opened successfully. Accessors early-return empty when
    // false so a missing/broken db never crashes the app (spec §10).
    bool m_open = false;
    QString m_connectionName;
    // mutable: the warning is a diagnostic side-channel (the const validation
    // helpers and const accessors record the last QSql error) and never affects
    // the store's logical/observable state.
    mutable QString m_lastWarning;
};
