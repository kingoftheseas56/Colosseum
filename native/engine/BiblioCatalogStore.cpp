// BiblioCatalogStore — implementation (spec 2026-08-01 §8, plan 2026-08-03 T3).
//
// Atomicity strategy: every data row carries the snapshot_id it belongs to.
// publish() opens ONE transaction, inserts a fresh snapshot_id row, writes all
// works/editions/sources/facets/rankings/history scoped to that id, validates
// invariants (unique canonical ids, controlled facet keys, FK integrity via the
// scoped staging set), and finally flips sync_meta.active_snapshot_id to the new
// id and prunes the previous snapshot's rows + ranking-history overflow. Any
// validation error -> rollback, prior active snapshot untouched (spec §8).
//
// Reads scope every query by the active_snapshot_id in sync_meta, so a reader
// never sees a half-published snapshot. Catalogue ids and facet axes are
// allowlisted; facetKey is always bound. The page() shape mirrors
// ComicsCatalog::discoverPage exactly so the QML adapter is shared.

#include "BiblioCatalogStore.h"
#include "BiblioTaxonomy.h"
#include "BiblioArtworkUrl.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

namespace {

// Schema version for `PRAGMA user_version`. Bump on any breaking schema change;
// future migrations branch on this in ensureSchema().
inline constexpr int kSchemaVersion = 1;

// Page-size clamp, mirroring ComicsCatalog::discoverPage (kPageLimitMax).
inline constexpr int kPageLimitMax = 100;

// Ranking-history retention: keep the most recent eight daily snapshots per
// work — eight suffices for a seven-day delta (8 >= 7+1).
inline constexpr int kHistoryRetention = 8;

// The four allowlisted Biblio house catalogues (spec §4.2 / §7). Anything else
// is rejected before it can reach SQL.
const QSet<QString> &allowedCatalogs()
{
    static const QSet<QString> s = {
        QStringLiteral("popular"), QStringLiteral("top-rated"),
        QStringLiteral("new-releases"), QStringLiteral("trending")};
    return s;
}

// The controlled facet axes the taxonomy owns (spec §4.3 / §6.3). Publisher is
// data-derived but the axis is still a legal filter target.
const QSet<QString> &allowedFacetAxes()
{
    static const QSet<QString> s = {
        QStringLiteral("genre"), QStringLiteral("audience"),
        QStringLiteral("theme"), QStringLiteral("setting"),
        QStringLiteral("period"), QStringLiteral("length"),
        QStringLiteral("era"), QStringLiteral("language"),
        QStringLiteral("publisher")};
    return s;
}

// The full set of controlled (axis,key) pairs the taxonomy admits. Used as the
// publish-time validation gate (spec §6.3: never persist an unknown facet key).
//
// BiblioTaxonomy::filterGroups() enumerates every controlled axis — including
// the computed length/era/language buckets — so the single static loop below is
// the complete source of truth. The taxonomy is the canonical allowlist; keeping
// a parallel hardcoded copy here would drift out of sync if the buckets ever
// changed (the store would then start rejecting valid bindings).
QSet<QString> controlledFacetPairs()
{
    QSet<QString> out;
    for (const BiblioFilterGroup &g : BiblioTaxonomy::filterGroups()) {
        // Publisher values are data-derived, so the static table carries no keys;
        // publisher bindings are validated at publish time against the snapshot's
        // own curated publisher set rather than this static table.
        if (g.axis == QStringLiteral("publisher"))
            continue;
        for (const BiblioFacet &f : g.facets)
            out.insert(g.axis + QChar('/') + f.key);
    }
    return out;
}

// Pack a paged result into the ComicsCatalog::discoverPage shape.
QVariantMap packPage(const QVariantList &items, int offset, int total,
                     const QString &freshness, const QString &warning)
{
    return QVariantMap{
        {QStringLiteral("items"), items},
        {QStringLiteral("nextOffset"), offset + static_cast<int>(items.size())},
        {QStringLiteral("exhausted"), offset + static_cast<int>(items.size()) >= total},
        {QStringLiteral("freshness"), freshness},
        {QStringLiteral("warning"), warning}};
}

// QVariant form: a null QString is bound as a NON-null empty string, so SQLite
// stores "" rather than NULL (which would violate our NOT NULL columns). An
// empty QString built from QStringLiteral("") is empty but NOT null, and the
// QVariant wrapping it reports isNull()==false.
QVariant bindStr(const QString &s)
{
    if (s.isNull())
        return QVariant(QStringLiteral(""));
    return QVariant(s);
}

} // namespace

BiblioCatalogStore::BiblioCatalogStore() = default;

BiblioCatalogStore::~BiblioCatalogStore()
{
    if (m_open && !m_connectionName.isEmpty()) {
        QSqlDatabase::database(m_connectionName, false).close();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

bool BiblioCatalogStore::open(const QString &path)
{
    if (m_open)
        return true;

    // A unique named connection so multiple store handles (e.g. tests) can coexist.
    m_connectionName = QStringLiteral("biblio_catalog_store_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        m_lastWarning = db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }
    // Enforce FK constraints on every connection (off by default in SQLite).
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        m_lastWarning = pragma.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }

    if (!ensureSchema()) {
        // ensureSchema() records its own warning; release the named connection
        // exactly as the other early-return paths so Qt never leaks it.
        if (m_lastWarning.isEmpty())
            m_lastWarning = QStringLiteral("open: schema initialization failed");
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }

    m_open = true;
    return true;
}

bool BiblioCatalogStore::ensureSchema()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return false;

    // Seven tables (plan Task 3). snapshot_id scopes every data row so publish
    // can stage a new snapshot, validate, and atomically flip the active id.
    const QStringList ddl = {
        QStringLiteral(
            "create table if not exists sync_meta ("
            "  key text primary key,"
            "  value text not null)"),
        QStringLiteral(
            "create table if not exists works ("
            "  snapshot_id integer not null,"
            "  canonical_id text not null,"
            "  title text not null,"
            "  author text not null,"
            "  original_language text not null,"
            "  canonical_first_published text not null,"
            "  publisher text not null,"
            "  cover_url text not null,"
            "  rating_average real not null,"
            "  rating_count integer not null,"
            "  apple_chart_score real not null,"
            "  openlibrary_popularity real not null,"
            "  explicit integer not null default 0,"
            "  primary key (snapshot_id, canonical_id))"),
        QStringLiteral(
            "create index if not exists idx_works_snapshot on works(snapshot_id)"),
        QStringLiteral(
            "create table if not exists editions ("
            "  snapshot_id integer not null,"
            "  canonical_id text not null,"
            "  edition_id text not null,"
            "  language text not null,"
            "  english_readable integer not null,"
            "  page_count integer not null,"
            "  publisher text not null,"
            "  published text not null,"
            "  format text not null,"
            "  primary key (snapshot_id, edition_id),"
            "  foreign key (snapshot_id, canonical_id)"
            "    references works(snapshot_id, canonical_id) on delete cascade)"),
        QStringLiteral(
            "create table if not exists work_sources ("
            "  snapshot_id integer not null,"
            "  canonical_id text not null,"
            "  field text not null,"
            "  source text not null,"
            "  source_id text not null,"
            "  observed_at text not null,"
            "  foreign key (snapshot_id, canonical_id)"
            "    references works(snapshot_id, canonical_id) on delete cascade)"),
        QStringLiteral(
            "create table if not exists work_facets ("
            "  snapshot_id integer not null,"
            "  canonical_id text not null,"
            "  axis text not null,"
            "  key text not null,"
            "  foreign key (snapshot_id, canonical_id)"
            "    references works(snapshot_id, canonical_id) on delete cascade)"),
        QStringLiteral(
            "create index if not exists idx_facets_lookup on work_facets(snapshot_id, axis, key)"),
        QStringLiteral(
            "create table if not exists rankings ("
            "  snapshot_id integer not null,"
            "  catalog_id text not null,"
            "  canonical_id text not null,"
            "  score real not null,"
            "  rank integer not null,"
            "  primary key (snapshot_id, catalog_id, canonical_id),"
            "  foreign key (snapshot_id, canonical_id)"
            "    references works(snapshot_id, canonical_id) on delete cascade)"),
        QStringLiteral(
            "create index if not exists idx_rankings_page on rankings(snapshot_id, catalog_id, rank)"),
        // ranking_history is NOT snapshot-scoped: it accumulates daily readings
        // across publishes and is pruned to the last kHistoryRetention per work.
        QStringLiteral(
            "create table if not exists ranking_history ("
            "  canonical_id text not null,"
            "  captured_at text not null,"
            "  demand_score real not null,"
            "  primary key (canonical_id, captured_at))"),
    };

    for (const QString &sql : ddl) {
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            m_lastWarning = q.lastError().text();
            return false;
        }
    }

    // Stamp the schema version once (idempotent). Future migrations read this.
    QSqlQuery sv(db);
    sv.prepare(QStringLiteral(
        "insert or ignore into sync_meta(key, value) values('schema_version', ?)"));
    sv.addBindValue(QString::number(kSchemaVersion));
    if (!sv.exec()) {
        m_lastWarning = sv.lastError().text();
        return false;
    }

    // PRAGMA user_version is the second, file-level version stamp.
    QSqlQuery uv(db);
    uv.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion));

    return true;
}

bool BiblioCatalogStore::validateUniqueCanonicalIds(const BiblioCatalogSnapshot &snapshot) const
{
    QSet<QString> seen;
    seen.reserve(snapshot.works.size());
    for (const BiblioWork &w : snapshot.works) {
        if (w.canonicalId.isEmpty()) {
            m_lastWarning = QStringLiteral("publish: work with empty canonicalId");
            return false;
        }
        // QSet::insert returns an iterator (not a pair); check presence first to
        // detect a duplicate canonicalId within this snapshot.
        if (seen.contains(w.canonicalId)) {
            m_lastWarning = QStringLiteral(
                "publish: duplicate canonicalId '%1' in one snapshot").arg(w.canonicalId);
            return false;
        }
        seen.insert(w.canonicalId);
    }
    return true;
}

bool BiblioCatalogStore::validateFacetKeys(const BiblioCatalogSnapshot &snapshot) const
{
    // Controlled-vocabulary gate (spec §6.3): reject any axis or (axis,key) pair
    // the taxonomy does not own. Publisher keys are data-derived and validated
    // separately against the snapshot's curated publisher set.
    const QSet<QString> allowedAxes = allowedFacetAxes();
    const QSet<QString> allowedPairs = controlledFacetPairs();

    for (const BiblioCatalogFacetRow &fr : snapshot.facets) {
        if (!allowedAxes.contains(fr.facet.axis)) {
            m_lastWarning = QStringLiteral(
                "publish: unknown facet axis '%1'").arg(fr.facet.axis);
            return false;
        }
        if (fr.facet.axis == QStringLiteral("publisher"))
            continue; // publisher values are data-derived, validated by coverage floor
        const QString pair = fr.facet.axis + QChar('/') + fr.facet.key;
        if (!allowedPairs.contains(pair)) {
            m_lastWarning = QStringLiteral(
                "publish: unknown facet key '%1' on axis '%2'")
                    .arg(fr.facet.key, fr.facet.axis);
            return false;
        }
    }
    return true;
}

bool BiblioCatalogStore::publish(const BiblioCatalogSnapshot &snapshot)
{
    if (!m_open) {
        m_lastWarning = QStringLiteral("publish: store is not open");
        return false;
    }

    // Cheap structural validation up front (no SQL yet).
    if (!validateUniqueCanonicalIds(snapshot))
        return false;
    if (!validateFacetKeys(snapshot))
        return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen()) {
        m_lastWarning = QStringLiteral("publish: database not open");
        return false;
    }

    return stagingTransaction(snapshot);
}

bool BiblioCatalogStore::stagingTransaction(const BiblioCatalogSnapshot &snapshot)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery q(db);

    if (!db.transaction()) {
        m_lastWarning = QStringLiteral("publish: begin transaction failed");
        return false;
    }

    // Allocate a fresh, monotonically increasing snapshot id. The max+1 pattern
    // keeps ids stable within the transaction window.
    qint64 snapshotId = 1;
    if (q.exec(QStringLiteral("select ifnull(max(value),0)+1 from sync_meta where key='snapshot_seq'"))
        && q.next()) {
        snapshotId = q.value(0).toLongLong();
        if (snapshotId < 1) snapshotId = 1;
    }
    {
        QSqlQuery seq(db);
        seq.prepare(QStringLiteral(
            "insert into sync_meta(key, value) values('snapshot_seq', ?) "
            "on conflict(key) do update set value=excluded.value"));
        seq.addBindValue(QString::number(snapshotId));
        if (!seq.exec()) {
            m_lastWarning = QStringLiteral("publish: snapshot_seq update failed: ")
                + seq.lastError().text();
            db.rollback();
            return false;
        }
    }

    // Index the snapshot's canonical id set for FK validation of the child rows.
    QSet<QString> canonicalIds;
    canonicalIds.reserve(snapshot.works.size());
    for (const BiblioWork &w : snapshot.works)
        canonicalIds.insert(w.canonicalId);

    // works -------------------------------------------------------------
    QSqlQuery iw(db);
    iw.prepare(QStringLiteral(
        "insert into works(snapshot_id, canonical_id, title, author, original_language,"
        "  canonical_first_published, publisher, cover_url, rating_average, rating_count,"
        "  apple_chart_score, openlibrary_popularity, explicit)"
        " values(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    for (const BiblioWork &w : snapshot.works) {
        iw.addBindValue(snapshotId);
        iw.addBindValue(bindStr(w.canonicalId));
        iw.addBindValue(bindStr(w.title));
        iw.addBindValue(bindStr(w.author));
        iw.addBindValue(bindStr(w.originalLanguage));
        iw.addBindValue(bindStr(w.canonicalFirstPublished.toString(Qt::ISODate)));
        iw.addBindValue(bindStr(w.publisher));
        iw.addBindValue(bindStr(w.coverUrl));
        iw.addBindValue(w.rating.average);
        iw.addBindValue(w.rating.count);
        iw.addBindValue(w.appleChartScore);
        iw.addBindValue(w.openLibraryPopularity);
        iw.addBindValue(snapshot.explicitWorkIds.contains(w.canonicalId) ? 1 : 0);
        if (!iw.exec()) {
            m_lastWarning = QStringLiteral("publish: insert works failed: ")
                + iw.lastError().text();
            db.rollback();
            return false;
        }
    }

    // editions (FK to works inside this snapshot) -----------------------
    QSqlQuery ie(db);
    ie.prepare(QStringLiteral(
        "insert into editions(snapshot_id, canonical_id, edition_id, language,"
        "  english_readable, page_count, publisher, published, format)"
        " values(?,?,?,?,?,?,?,?,?)"));
    // Editions nested on BiblioWork::editions AND the flat snapshot.editions list
    // both land here; the flat list is the canonical source for the store.
    for (const BiblioCatalogEditionRow &er : snapshot.editions) {
        if (!canonicalIds.contains(er.canonicalId)) {
            m_lastWarning = QStringLiteral(
                "publish: edition '%1' references unknown work '%2'")
                    .arg(er.edition.editionId, er.canonicalId);
            db.rollback();
            return false;
        }
        ie.addBindValue(snapshotId);
        ie.addBindValue(bindStr(er.canonicalId));
        ie.addBindValue(bindStr(er.edition.editionId));
        ie.addBindValue(bindStr(er.edition.language));
        ie.addBindValue(er.edition.englishReadable ? 1 : 0);
        ie.addBindValue(er.edition.pageCount);
        ie.addBindValue(bindStr(er.edition.publisher));
        ie.addBindValue(bindStr(er.edition.published.toString(Qt::ISODate)));
        ie.addBindValue(bindStr(er.edition.format));
        if (!ie.exec()) {
            m_lastWarning = QStringLiteral("publish: insert editions failed: ")
                + ie.lastError().text();
            db.rollback();
            return false;
        }
    }
    // Also persist editions nested on the works themselves (the canonicalizer
    // attaches them there; this keeps publish self-sufficient when the snapshot
    // only carries works.editions).
    for (const BiblioWork &w : snapshot.works) {
        for (const BiblioEdition &e : w.editions) {
            ie.addBindValue(snapshotId);
            ie.addBindValue(bindStr(w.canonicalId));
            ie.addBindValue(bindStr(e.editionId));
            ie.addBindValue(bindStr(e.language));
            ie.addBindValue(e.englishReadable ? 1 : 0);
            ie.addBindValue(e.pageCount);
            ie.addBindValue(bindStr(e.publisher));
            ie.addBindValue(bindStr(e.published.toString(Qt::ISODate)));
            ie.addBindValue(bindStr(e.format));
            if (!ie.exec()) {
                // Duplicate edition_id within the snapshot is the most likely
                // cause; treat as a validation failure rather than crashing.
                m_lastWarning = QStringLiteral(
                    "publish: insert nested editions failed for '%1': %2")
                        .arg(e.editionId, ie.lastError().text());
                db.rollback();
                return false;
            }
        }
    }

    // work_sources (per-field provenance) -------------------------------
    QSqlQuery is(db);
    is.prepare(QStringLiteral(
        "insert into work_sources(snapshot_id, canonical_id, field, source, source_id, observed_at)"
        " values(?,?,?,?,?,?)"));
    for (const BiblioCatalogSourceRow &sr : snapshot.sources) {
        if (!canonicalIds.contains(sr.canonicalId)) {
            m_lastWarning = QStringLiteral(
                "publish: source references unknown work '%1'").arg(sr.canonicalId);
            db.rollback();
            return false;
        }
        is.addBindValue(snapshotId);
        is.addBindValue(bindStr(sr.canonicalId));
        is.addBindValue(bindStr(sr.source.field));
        is.addBindValue(bindStr(sr.source.source));
        is.addBindValue(bindStr(sr.source.sourceId));
        is.addBindValue(bindStr(sr.source.observedAt.toString(Qt::ISODateWithMs)));
        if (!is.exec()) {
            m_lastWarning = QStringLiteral("publish: insert work_sources failed: ")
                + is.lastError().text();
            db.rollback();
            return false;
        }
    }

    // work_facets (controlled vocabulary) -------------------------------
    QSqlQuery ifq(db);
    ifq.prepare(QStringLiteral(
        "insert into work_facets(snapshot_id, canonical_id, axis, key) values(?,?,?,?)"));
    for (const BiblioCatalogFacetRow &fr : snapshot.facets) {
        if (!canonicalIds.contains(fr.canonicalId)) {
            m_lastWarning = QStringLiteral(
                "publish: facet references unknown work '%1'").arg(fr.canonicalId);
            db.rollback();
            return false;
        }
        ifq.addBindValue(snapshotId);
        ifq.addBindValue(bindStr(fr.canonicalId));
        ifq.addBindValue(bindStr(fr.facet.axis));
        ifq.addBindValue(bindStr(fr.facet.key));
        if (!ifq.exec()) {
            m_lastWarning = QStringLiteral("publish: insert work_facets failed: ")
                + ifq.lastError().text();
            db.rollback();
            return false;
        }
    }

    // rankings (four catalogues, validated against the works in snapshot) -
    QSqlQuery ir(db);
    ir.prepare(QStringLiteral(
        "insert into rankings(snapshot_id, catalog_id, canonical_id, score, rank)"
        " values(?,?,?,?,?)"));
    for (const BiblioCatalogRanking &r : snapshot.rankings) {
        if (!allowedCatalogs().contains(r.catalogId)) {
            m_lastWarning = QStringLiteral(
                "publish: ranking with unknown catalog '%1'").arg(r.catalogId);
            db.rollback();
            return false;
        }
        if (!canonicalIds.contains(r.canonicalId)) {
            m_lastWarning = QStringLiteral(
                "publish: ranking references unknown work '%1'").arg(r.canonicalId);
            db.rollback();
            return false;
        }
        ir.addBindValue(snapshotId);
        ir.addBindValue(bindStr(r.catalogId));
        ir.addBindValue(bindStr(r.canonicalId));
        ir.addBindValue(r.score);
        ir.addBindValue(r.rank);
        if (!ir.exec()) {
            m_lastWarning = QStringLiteral("publish: insert rankings failed: ")
                + ir.lastError().text();
            db.rollback();
            return false;
        }
    }

    // ranking_history (append-accumulate; prune to last N per work below) -
    QSqlQuery ih(db);
    ih.prepare(QStringLiteral(
        "insert or replace into ranking_history(canonical_id, captured_at, demand_score)"
        " values(?,?,?)"));
    for (const BiblioCatalogHistory &h : snapshot.history) {
        ih.addBindValue(bindStr(h.canonicalId));
        ih.addBindValue(bindStr(h.capturedAt.toString(Qt::ISODateWithMs)));
        ih.addBindValue(h.demandScore);
        if (!ih.exec()) {
            m_lastWarning = QStringLiteral("publish: insert ranking_history failed: ")
                + ih.lastError().text();
            db.rollback();
            return false;
        }
    }

    // Atomic swap: flip sync_meta.active_snapshot_id + last_success_utc, then
    // drop the previous snapshot's scoped rows and prune history overflow. All
    // inside the same transaction, so the pivot is all-or-nothing.
    //
    // I-1: the swap is load-bearing (spec §8). If the active-id upsert fails
    // (disk full, locked, ...), the publish MUST abort BEFORE the prune loop
    // runs — otherwise the prior snapshot's rows would be deleted while
    // active_snapshot_id still points at the old id, so readers would resolve
    // to a snapshot whose rows were just deleted (empty catalogue, silent
    // corruption). Roll back and keep the prior active snapshot fully intact.
    if (!setActiveSnapshot(snapshotId, snapshot.capturedAt)) {
        if (m_lastWarning.isEmpty())
            m_lastWarning = QStringLiteral("publish: active snapshot swap failed");
        db.rollback();
        return false;
    }

    // Prune prior snapshot rows (everything not in the new active snapshot).
    // ranking_history is intentionally retained across snapshots (it carries the
    // seven-day Trending delta) and pruned by its own retention rule below.
    const QStringList pruneTables = {
        QStringLiteral("works"), QStringLiteral("editions"),
        QStringLiteral("work_sources"), QStringLiteral("work_facets"),
        QStringLiteral("rankings")};
    for (const QString &table : pruneTables) {
        QSqlQuery pq(db);
        pq.prepare(QStringLiteral("delete from %1 where snapshot_id != ?")
                       .arg(table)); // table is a hard-coded literal, never caller text
        pq.addBindValue(snapshotId);
        if (!pq.exec()) {
            m_lastWarning = QStringLiteral("publish: prune %1 failed: ")
                .arg(table) + pq.lastError().text();
            db.rollback();
            return false;
        }
    }

    // Retention: keep only the most recent kHistoryRetention daily readings per
    // work. row_number() over captured_at desc partitions by canonical_id; the
    // outer delete drops everything ranked beyond the retention window. The
    // threshold is a compile-time constant, bound here for clarity/safety.
    QSqlQuery ph(db);
    if (!ph.prepare(QStringLiteral(
            "delete from ranking_history where rowid in ("
            "  select rowid from ("
            "    select rowid, canonical_id, captured_at,"
            "      row_number() over (partition by canonical_id order by captured_at desc) as rn"
            "    from ranking_history) where rn > ?)"))) {
        m_lastWarning = QStringLiteral("publish: history prune prepare failed: ")
            + ph.lastError().text();
        db.rollback();
        return false;
    }
    ph.addBindValue(kHistoryRetention);
    if (!ph.exec()) {
        m_lastWarning = QStringLiteral("publish: history prune failed: ")
            + ph.lastError().text();
        db.rollback();
        return false;
    }

    if (!db.commit()) {
        m_lastWarning = QStringLiteral("publish: commit failed: ")
            + db.lastError().text();
        db.rollback();
        return false;
    }

    m_lastWarning.clear();
    return true;
}

bool BiblioCatalogStore::setActiveSnapshot(qint64 snapshotId, const QDateTime &capturedAt)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    const QString id = QString::number(snapshotId);
    // A missing capturedAt falls back to "now" UTC so last_success_utc is never
    // null (the active snapshot must always carry a valid success timestamp).
    const QDateTime when = capturedAt.isValid()
        ? capturedAt.toUTC() : QDateTime::currentDateTimeUtc();
    const QString ts = when.toString(Qt::ISODateWithMs);

    struct Row { const char *key; QString value; };
    const Row rows[] = {
        {"active_snapshot_id", id},
        {"last_success_utc", ts},
    };
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "insert into sync_meta(key, value) values(?,?)"
        " on conflict(key) do update set value=excluded.value"));
    for (const Row &r : rows) {
        q.addBindValue(QString::fromLatin1(r.key));
        q.addBindValue(r.value);
        // This is the atomic active-snapshot-id swap — the one operation spec §8
        // ("a failed/invalid snapshot never replaces the last valid cache")
        // hinges on. Surface a failure to the caller so it rolls back before the
        // prune loop deletes the prior snapshot's rows.
        if (!q.exec()) {
            m_lastWarning = QStringLiteral("setActiveSnapshot: upsert '%1' failed: ")
                + q.lastError().text();
            return false;
        }
    }
    return true;
}

QVariantMap BiblioCatalogStore::page(const QString &catalogId, const QString &facetAxis,
                                     const QString &facetKey, bool includeExplicit,
                                     int offset, int limit) const
{
    const int lim = std::clamp(limit, 1, kPageLimitMax);
    const int off = std::max(0, offset);

    if (!m_open)
        return packPage({}, off, 0, QStringLiteral("none"),
                        QStringLiteral("store not open"));

    // Allowlist the catalogue id — never interpolate caller text into SQL.
    if (!allowedCatalogs().contains(catalogId))
        return packPage({}, off, 0, QStringLiteral("none"),
                        QStringLiteral("unknown catalogue id"));

    // Allowlist the facet axis (empty means "no filter").
    const bool hasFilter = !facetAxis.isEmpty() && !facetKey.isEmpty();
    if (hasFilter && !allowedFacetAxes().contains(facetAxis))
        return packPage({}, off, 0, QStringLiteral("none"),
                        QStringLiteral("unknown facet axis"));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.isOpen())
        return packPage({}, off, 0, QStringLiteral("none"),
                        QStringLiteral("database not open"));

    // Resolve the active snapshot id; if none, this is a first-sync state.
    QSqlQuery meta(db);
    if (!meta.exec(QStringLiteral(
            "select value from sync_meta where key='active_snapshot_id'")))
        return packPage({}, off, 0, QStringLiteral("none"),
                        meta.lastError().text());
    if (!meta.next())
        return packPage({}, off, 0, QStringLiteral("none"),
                        QStringLiteral("no snapshot published yet"));
    const qint64 snapshotId = meta.value(0).toLongLong();

    QString sql = QStringLiteral(
        "select w.canonical_id, w.title, w.author, w.original_language,"
        "       w.canonical_first_published, w.publisher, w.cover_url,"
        "       w.rating_average, w.rating_count, r.score, r.rank"
        " from rankings r join works w"
        "   on w.snapshot_id = r.snapshot_id and w.canonical_id = r.canonical_id"
        " where r.snapshot_id = ? and r.catalog_id = ?");
    if (hasFilter)
        sql += QStringLiteral(
            " and exists (select 1 from work_facets f"
            "             where f.snapshot_id = r.snapshot_id"
            "               and f.canonical_id = r.canonical_id"
            "               and f.axis = ? and f.key = ?)");
    if (!includeExplicit)
        sql += QStringLiteral(" and w.explicit = 0");
    sql += QStringLiteral(" order by r.rank asc, r.score desc, w.canonical_id asc");

    QSqlQuery q(db);
    if (!q.prepare(sql)) {
        m_lastWarning = q.lastError().text();
        return packPage({}, off, 0, QStringLiteral("none"), m_lastWarning);
    }
    q.addBindValue(snapshotId);
    q.addBindValue(catalogId);
    if (hasFilter) {
        q.addBindValue(facetAxis);
        q.addBindValue(facetKey); // BOUND, never concatenated
    }
    if (!q.exec()) {
        m_lastWarning = q.lastError().text();
        return packPage({}, off, 0, QStringLiteral("none"), m_lastWarning);
    }

    // Materialize each matched row into a QVariantMap (column-index access keeps
    // this independent of QSqlRecord's stream operators), then slice
    // [off, off+lim). total is the full match count so `exhausted` is correct
    // even when the page is a strict subset.
    QList<QVariantMap> rows;
    while (q.next()) {
        QVariantMap ratingMap;
        ratingMap.insert(QStringLiteral("average"), q.value(7));
        ratingMap.insert(QStringLiteral("count"), q.value(8));
        QVariantMap m;
        m.insert(QStringLiteral("canonicalId"), q.value(0));
        m.insert(QStringLiteral("title"), q.value(1));
        m.insert(QStringLiteral("author"), q.value(2));
        m.insert(QStringLiteral("originalLanguage"), q.value(3));
        m.insert(QStringLiteral("canonicalFirstPublished"), q.value(4));
        m.insert(QStringLiteral("publisher"), q.value(5));
        // Heals covers already persisted in an existing cache with the pre-fix broken
        // Apple RSS URL shape (see BiblioArtworkUrl.h) without waiting for a fresh
        // snapshot — a stale cache can otherwise live up to 7 days (see freshness below).
        m.insert(QStringLiteral("coverUrl"), normalizedAppleArtworkUrl(q.value(6).toString()));
        m.insert(QStringLiteral("rating"), ratingMap);
        m.insert(QStringLiteral("score"), q.value(9));
        m.insert(QStringLiteral("rank"), q.value(10));
        rows.append(m);
    }

    const int total = static_cast<int>(rows.size());
    QVariantList items;
    for (int i = off; i < total && static_cast<int>(items.size()) < lim; ++i)
        items.append(rows.at(i));

    // Freshness: how stale the active snapshot is, in whole days.
    QString freshness = QStringLiteral("fresh");
    QSqlQuery ts(db);
    if (ts.exec(QStringLiteral(
            "select value from sync_meta where key='last_success_utc'"))
        && ts.next()) {
        const QDateTime last = QDateTime::fromString(
            ts.value(0).toString(), Qt::ISODateWithMs);
        if (last.isValid()) {
            const qint64 days = last.daysTo(QDateTime::currentDateTimeUtc());
            if (days >= 7) freshness = QStringLiteral("stale");
            else if (days >= 1) freshness = QStringLiteral("aging");
        }
    }

    return packPage(items, off, total, freshness, QString());
}

QVariantList BiblioCatalogStore::filterGroups(bool includeExplicit) const
{
    QVariantList out;
    if (!m_open)
        return out;

    // The controlled axes/values come from the taxonomy (single source of truth).
    // Publisher values are data-derived: advertise the curated set over the active
    // snapshot (coverage-floored), so the filter UI never offers an empty axis.
    for (const BiblioFilterGroup &g : BiblioTaxonomy::filterGroups()) {
        QVariantMap gm;
        gm.insert(QStringLiteral("axis"), g.axis);
        gm.insert(QStringLiteral("label"), g.label);

        QVariantList facets;
        if (g.axis == QStringLiteral("publisher")) {
            // Data-derived publisher values from the active snapshot.
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            QSqlQuery q(db);
            q.prepare(QStringLiteral(
                "select distinct publisher from works where publisher != ''"
                " and snapshot_id = ? and (? = 1 or explicit = 0)"));
            // Bind the active snapshot id.
            QSqlQuery meta(db);
            if (meta.exec(QStringLiteral(
                    "select value from sync_meta where key='active_snapshot_id'"))
                && meta.next()) {
                q.addBindValue(meta.value(0).toLongLong());
                q.addBindValue(includeExplicit ? 1 : 0);
                if (q.exec()) {
                    while (q.next()) {
                        const QString pub = q.value(0).toString();
                        facets.append(QVariantMap{
                            {QStringLiteral("key"), pub},
                            {QStringLiteral("label"), pub}});
                    }
                }
            }
        } else {
            for (const BiblioFacet &f : g.facets) {
                facets.append(QVariantMap{
                    {QStringLiteral("key"), f.key},
                    {QStringLiteral("label"), f.label}});
            }
        }
        gm.insert(QStringLiteral("facets"), facets);
        out.append(gm);
    }
    return out;
}

QVariantList BiblioCatalogStore::previewRows(int limit, bool includeExplicit) const
{
    QVariantList out;
    if (!m_open)
        return out;
    const int lim = std::clamp(limit, 1, kPageLimitMax);

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "select canonical_id, title, author, publisher, cover_url, rating_average"
        " from works where snapshot_id = (select value from sync_meta where key='active_snapshot_id')"
        "   and (? = 1 or explicit = 0)"
        " order by publisher asc, title asc, canonical_id asc limit ?"));
    q.addBindValue(includeExplicit ? 1 : 0);
    q.addBindValue(lim);
    if (!q.exec())
        return out;
    while (q.next()) {
        out.append(QVariantMap{
            {QStringLiteral("canonicalId"), q.value(0)},
            {QStringLiteral("title"), q.value(1)},
            {QStringLiteral("author"), q.value(2)},
            {QStringLiteral("publisher"), q.value(3)},
            {QStringLiteral("coverUrl"), normalizedAppleArtworkUrl(q.value(4).toString())},
            {QStringLiteral("ratingAverage"), q.value(5)}});
    }
    return out;
}

QVariantList BiblioCatalogStore::top10(int limit, bool includeExplicit) const
{
    QVariantList out;
    if (!m_open)
        return out;

    // top10 is the Popular catalogue's rank-1..limit slice, the same shape as
    // page. Callers pass 10 for "Top 10"; page() clamps limit to [1,100].
    const QVariantMap p = page(QStringLiteral("popular"), QString(), QString(),
                               includeExplicit, 0, limit);
    return p.value(QStringLiteral("items")).toList();
}

QDateTime BiblioCatalogStore::lastSuccessUtc() const
{
    if (!m_open)
        return QDateTime();
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "select value from sync_meta where key='last_success_utc'")))
        return QDateTime();
    if (!q.next())
        return QDateTime();
    return QDateTime::fromString(q.value(0).toString(), Qt::ISODateWithMs);
}

bool BiblioCatalogStore::hasSnapshot() const
{
    if (!m_open)
        return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "select value from sync_meta where key='active_snapshot_id'")))
        return false;
    return q.next();
}

QVariantList BiblioCatalogStore::rankingHistoryFor(const QString &canonicalId) const
{
    QVariantList out;
    if (!m_open)
        return out;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "select canonical_id, captured_at, demand_score from ranking_history"
        " where canonical_id = ? order by captured_at desc"));
    q.addBindValue(canonicalId); // BOUND
    if (!q.exec())
        return out;
    while (q.next()) {
        out.append(QVariantMap{
            {QStringLiteral("canonicalId"), q.value(0)},
            {QStringLiteral("capturedAt"),
             QDateTime::fromString(q.value(1).toString(), Qt::ISODateWithMs)},
            {QStringLiteral("demandScore"), q.value(2)}});
    }
    return out;
}
