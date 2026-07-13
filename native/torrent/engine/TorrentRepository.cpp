// TorrentRepository implementation.
//
// P0.3 establishes lifecycle (open/close/initSchema), transactions, schema_meta
// key-value helpers, and a hasTable diagnostic. CRUD methods on torrents,
// stream_groups, stream_group_items, and stream_downloads_index are stubbed
// here (return false / empty); P0.5 / P0.6 / P0.7 fill them in.
//
// See docs/superpowers/plans/2026-05-19-torrent-persistence-collapse.md Phase 0.

#include "TorrentRepository.h"

#include <QAtomicInt>
#include <QDateTime>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace tankoban::torrent {

// Schema SQL — initSchema runs in two passes around any ALTER TABLE migrations
// so post-migration columns can be referenced by post-pass indexes:
//   pass 1: kSchemaSqlTables  (PRAGMAs + every CREATE TABLE IF NOT EXISTS)
//   step  : version check + ALTER TABLE migrations
//   pass 2: kSchemaSqlIndexes (every CREATE INDEX IF NOT EXISTS — runs after
//           any column-adding migrations so an index over a newly-added
//           column finds its column rather than failing with "no such column")
//
// COLLATE NOCASE on hash columns canonicalises case at the SQLite boundary
// (audit Part B item 13 — hash case drift was already an F9 follow-up concern).
//
// PARSER CONSTRAINT: each blob is dispatched via a naive QString::split on
// the semicolon character, NOT a SQL parser. Comments AND string literals
// AND identifiers inside either blob must NOT contain semicolons or the
// dispatch will fragment statements mid-flight. Phase 3.4.0 (Agent 4) burned
// build cycles learning this. Keep literal semicolons OUT of comments and
// DEFAULT values here.
static const char* kSchemaSqlTables = R"SQL(
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS torrents (
    hash TEXT PRIMARY KEY NOT NULL COLLATE NOCASE,
    state TEXT NOT NULL,
    name TEXT NOT NULL DEFAULT '',
    added_at TEXT NOT NULL,
    category TEXT NOT NULL DEFAULT '',
    save_path TEXT NOT NULL DEFAULT '',
    content_layout TEXT NOT NULL DEFAULT '',
    stream_group_id TEXT NOT NULL DEFAULT '',
    sequential INTEGER NOT NULL DEFAULT 0,
    imdb_id TEXT NOT NULL DEFAULT '',
    season INTEGER NOT NULL DEFAULT 0,
    magnet_uri TEXT NOT NULL DEFAULT '',
    legacy_no_magnet INTEGER NOT NULL DEFAULT 0,
    error_message TEXT NOT NULL DEFAULT '',
    resume_data BLOB,
    torrent_file BLOB
);

CREATE TABLE IF NOT EXISTS stream_groups (
    group_id TEXT PRIMARY KEY NOT NULL,
    imdb_id TEXT NOT NULL DEFAULT '',
    season INTEGER NOT NULL DEFAULT 0,
    label TEXT NOT NULL DEFAULT '',
    state TEXT NOT NULL,
    retry_generation INTEGER NOT NULL DEFAULT 0,
    staging_path TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL,
    pack_mode INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS stream_group_items (
    group_id TEXT NOT NULL,
    item_id TEXT NOT NULL,
    episode INTEGER NOT NULL DEFAULT 0,
    info_hash TEXT COLLATE NOCASE,
    state TEXT NOT NULL,
    error_message TEXT NOT NULL DEFAULT '',
    file_index INTEGER NOT NULL DEFAULT -1,
    PRIMARY KEY (group_id, item_id),
    FOREIGN KEY (group_id) REFERENCES stream_groups(group_id) ON DELETE CASCADE,
    FOREIGN KEY (info_hash) REFERENCES torrents(hash) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS stream_downloads_index (
    canonical_path TEXT PRIMARY KEY NOT NULL,
    imdb_id TEXT NOT NULL DEFAULT '',
    season INTEGER NOT NULL DEFAULT 0,
    episode INTEGER NOT NULL DEFAULT 0,
    state TEXT NOT NULL,
    info_hash TEXT COLLATE NOCASE,
    added_at TEXT NOT NULL,
    -- Phase 3.4.0 (2026-05-20) added for StreamDownloadIndex full-shape
    -- absorption. Existing v1 DBs get these via ALTER TABLE in initSchema
    -- migration step. Fresh installs at v2 create the table with them present.
    source_group_id TEXT NOT NULL DEFAULT '',
    progress_pct INTEGER NOT NULL DEFAULT 0,
    FOREIGN KEY (info_hash) REFERENCES torrents(hash) ON DELETE SET NULL
);

CREATE TABLE IF NOT EXISTS schema_meta (
    key TEXT PRIMARY KEY NOT NULL,
    value TEXT NOT NULL
);
)SQL";

// Indexes run AFTER kSchemaSqlTables + any ALTER TABLE migrations so an index
// over a column added by migration (e.g. idx_stream_downloads_source_group on
// the Phase 3.4.0 source_group_id column) doesn't fail with "no such column"
// on a still-being-migrated v1 DB.
static const char* kSchemaSqlIndexes = R"SQL(
CREATE INDEX IF NOT EXISTS idx_torrents_state ON torrents(state);
CREATE INDEX IF NOT EXISTS idx_torrents_stream_group ON torrents(stream_group_id);
CREATE INDEX IF NOT EXISTS idx_torrents_imdb_season ON torrents(imdb_id, season);

CREATE INDEX IF NOT EXISTS idx_stream_groups_imdb_season ON stream_groups(imdb_id, season);

CREATE INDEX IF NOT EXISTS idx_stream_group_items_hash ON stream_group_items(info_hash);

CREATE INDEX IF NOT EXISTS idx_stream_downloads_imdb ON stream_downloads_index(imdb_id, season, episode);
-- Phase 3.4.0 — supports evictBySourceGroup O(N) -> O(log N) pivot for the
-- cohort-cancel path that filters by source_group_id.
CREATE INDEX IF NOT EXISTS idx_stream_downloads_source_group ON stream_downloads_index(source_group_id);
)SQL";

// ─── ctor / dtor ─────────────────────────────────────────────────────────────

TorrentRepository::TorrentRepository(QObject* parent)
    : QObject(parent) {
    m_connectionName = QStringLiteral("TorrentRepository_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16);
}

TorrentRepository::~TorrentRepository() {
    close();
}

// ─── lifecycle ───────────────────────────────────────────────────────────────

namespace {

// PRAGMA quick_check on an OPEN connection. A healthy DB returns a single row
// "ok"; a malformed DB ("database disk image is malformed") returns error rows
// or fails to exec — both map to false. quick_check is chosen over the
// exhaustive integrity_check for a fast per-launch gate.
bool connectionQuickCheckOk(QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA quick_check")))
        return false;
    if (!q.next())
        return false;
    return q.value(0).toString().compare(QStringLiteral("ok"),
                                         Qt::CaseInsensitive) == 0;
}

// Connection-level durability PRAGMAs (TORRENT_DB_DURABILITY Part 2). Set per
// open() — synchronous + busy_timeout are per-connection (not persisted in the
// file), journal_mode=WAL is persisted but reasserted for clarity.
//   WAL              — readers don't block the writer; far more crash-tolerant
//                      than the default rollback journal.
//   synchronous=NORMAL — the WAL sweet spot: durable across an app crash /
//                      taskkill, only the last txn is at risk on OS/power loss
//                      (never corrupts). FULL would fsync every commit for no
//                      corruption-safety gain under WAL.
//   busy_timeout=5000 — wait up to 5s on a locked DB instead of failing instantly
//                      (defends against a transient second connection: dev +
//                      personal instances, or a leaked probe connection).
void applyConnectionPragmas(QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA journal_mode=WAL"))) {
        qWarning() << "[TorrentRepository] could not set WAL:"
                   << q.lastError().text();
    } else if (q.next()
               && q.value(0).toString().compare(QStringLiteral("wal"),
                                                 Qt::CaseInsensitive) != 0) {
        qWarning() << "[TorrentRepository] journal_mode is"
                   << q.value(0).toString() << "not wal (file locked?)";
    }
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    q.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
}

}  // namespace

bool TorrentRepository::open(const QString& dbFilePath) {
    if (m_open) return true;

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbFilePath);
    if (!m_db.open()) {
        qWarning() << "[TorrentRepository] open failed:" << m_db.lastError().text();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }

    // Durability self-heal (TORRENT_DB_DURABILITY 2026-06-02). SQLite opens a
    // malformed file without complaint — the corruption only surfaces on the
    // first real query, which is what let a bad torrents.db survive into the
    // boot reconcile and hammer the UI thread into "Not Responding". Detect it
    // up front and recover: back the bad file up, recreate a fresh schema, and
    // signal the index layer to rebuild-from-disk.
    bool recovered = false;
    if (!connectionQuickCheckOk(m_db)) {
        qWarning() << "[TorrentRepository] torrents.db failed quick_check "
                      "(malformed); backing up + recreating:" << dbFilePath;
        m_db.close();
        QSqlDatabase::removeDatabase(m_connectionName);
        if (!backupCorruptDatabase(dbFilePath)) {
            qWarning() << "[TorrentRepository] could not move corrupt DB aside; "
                          "aborting open";
            return false;
        }
        // Reopen onto the now-clear path — a fresh empty file.
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                         m_connectionName);
        m_db.setDatabaseName(dbFilePath);
        if (!m_db.open()) {
            qWarning() << "[TorrentRepository] reopen after recovery failed:"
                       << m_db.lastError().text();
            QSqlDatabase::removeDatabase(m_connectionName);
            return false;
        }
        recovered = true;
    }

    applyConnectionPragmas(m_db);

    m_open = true;
    if (!initSchema()) {
        qWarning() << "[TorrentRepository] initSchema failed; closing DB";
        close();
        return false;
    }

    if (recovered)
        emit databaseRecovered();

    return true;
}

bool TorrentRepository::databaseFileIsHealthy(const QString& dbFilePath) {
    static QAtomicInt probeCounter{0};
    const QString conn = QStringLiteral("TorrentRepoHealthProbe_%1")
                             .arg(probeCounter.fetchAndAddRelaxed(1));
    bool healthy = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    conn);
        db.setDatabaseName(dbFilePath);
        if (db.open()) {
            healthy = connectionQuickCheckOk(db);
            db.close();
        }
    }  // db destroyed here, before removeDatabase, to avoid "still in use"
    QSqlDatabase::removeDatabase(conn);
    return healthy;
}

bool TorrentRepository::backupCorruptDatabase(const QString& dbFilePath) {
    const QString suffix =
        QStringLiteral(".corrupt-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"));

    // Move the main DB file out of the way, plus its WAL/SHM sidecars so the
    // fresh open() starts from a clean slate (a stale -wal could re-corrupt).
    const QStringList parts = {
        dbFilePath,
        dbFilePath + QStringLiteral("-wal"),
        dbFilePath + QStringLiteral("-shm"),
    };
    bool mainMoved = false;
    for (const QString& p : parts) {
        if (!QFile::exists(p)) continue;
        const QString dst = p + suffix;
        QFile::remove(dst);  // clear any prior backup colliding on the stamp
        if (QFile::rename(p, dst)) {
            if (p == dbFilePath) mainMoved = true;
        } else {
            qWarning() << "[TorrentRepository] failed to move" << p << "->" << dst;
        }
    }
    return mainMoved;
}

namespace {

// Phase 3.4.0 (2026-05-20) — ALTER TABLE migration v1 -> v2 for an existing
// stream_downloads_index table. SQLite ALTER TABLE ADD COLUMN supports adding
// nullable or default-having columns without per-row patches; both new columns
// carry safe defaults so already-populated rows stay valid.
//
// SQLite has no IF NOT EXISTS clause on ALTER TABLE ADD COLUMN, so we ignore
// "duplicate column name" errors defensively — that lets the migration be
// re-runnable (a partial run that landed one column but failed on the next
// will succeed cleanly on retry).
bool migrateStreamDownloadsV1ToV2(QSqlDatabase& db) {
    struct Add { const char* name; const char* ddl; };
    const Add adds[] = {
        { "source_group_id",
          "ALTER TABLE stream_downloads_index "
          "ADD COLUMN source_group_id TEXT NOT NULL DEFAULT ''" },
        { "progress_pct",
          "ALTER TABLE stream_downloads_index "
          "ADD COLUMN progress_pct INTEGER NOT NULL DEFAULT 0" },
    };
    for (const auto& add : adds) {
        QSqlQuery q(db);
        if (q.exec(QString::fromUtf8(add.ddl)))
            continue;
        const QString err = q.lastError().text();
        // "duplicate column name" -> column was already added, treat as success.
        if (err.contains(QStringLiteral("duplicate column name"),
                         Qt::CaseInsensitive)) {
            continue;
        }
        qWarning() << "[TorrentRepository] v1->v2 migration ADD COLUMN"
                   << add.name << "failed:" << err;
        return false;
    }
    // CREATE INDEX is idempotent via IF NOT EXISTS in the schema blob; the
    // index gets created during the initial schema apply pass either way.
    return true;
}

}  // namespace

bool TorrentRepository::initSchema() {
    if (!m_open) return false;

    auto runBlob = [this](const char* blob, const char* label) -> bool {
        const QString text = QString::fromUtf8(blob);
        const QStringList stmts = text.split(';', Qt::SkipEmptyParts);
        QSqlQuery q(m_db);
        for (const QString& stmt : stmts) {
            const QString trimmed = stmt.trimmed();
            if (trimmed.isEmpty()) continue;
            if (!q.exec(trimmed)) {
                qWarning() << "[TorrentRepository]" << label
                           << "schema stmt failed:" << q.lastError().text()
                           << "stmt:" << trimmed.left(120);
                return false;
            }
        }
        return true;
    };

    // Pass 1: PRAGMAs + CREATE TABLE IF NOT EXISTS. Fresh installs get the
    // full v2 shape; pre-existing tables on older versions are untouched.
    if (!runBlob(kSchemaSqlTables, "tables")) return false;

    // Step 2: schema_version sentinel — empty == fresh install. Stamp at
    // current, then create indexes against the (fresh, fully-columned) schema.
    if (metaValue(QStringLiteral("schema_version")).isEmpty()) {
        setSchemaVersion(kSchemaVersion);
        return runBlob(kSchemaSqlIndexes, "indexes");
    }

    // Step 3: incremental ALTER TABLE migrations for older-version DBs.
    // Idempotent + re-runnable; each upgrade step ratchets schema_version
    // forward exactly once on success.
    const int current = schemaVersion();
    if (current < 2) {
        if (!migrateStreamDownloadsV1ToV2(m_db)) {
            qWarning() << "[TorrentRepository] v1->v2 migration failed; "
                          "leaving schema_version at" << current;
            return false;
        }
        setSchemaVersion(2);
        qInfo() << "[TorrentRepository] schema migrated v1 -> v2 "
                   "(stream_downloads_index gained source_group_id + "
                   "progress_pct columns)";
    }

    // Pass 2: CREATE INDEX IF NOT EXISTS — runs AFTER any column-adding
    // migrations so indexes over newly-added columns find their column.
    return runBlob(kSchemaSqlIndexes, "indexes");
}

void TorrentRepository::close() {
    if (m_db.isOpen()) {
        // Checkpoint + truncate the WAL on a clean exit so the next launch
        // starts from a single consolidated DB file with an empty WAL — this
        // shrinks the window where a force-kill (taskkill /F) can interrupt a
        // checkpoint mid-write and corrupt the main DB. (TORRENT_DB_DURABILITY P2)
        { QSqlQuery cp(m_db); cp.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)")); }
        m_db.close();
    }
    // Drop the member's handle to the connection BEFORE removeDatabase so Qt
    // doesn't warn "connection is still in use" (the teardown-ordering warning
    // Agent 0 observed at shutdown).
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_open = false;
}

bool TorrentRepository::isOpen() const {
    return m_open;
}

// ─── schema meta ─────────────────────────────────────────────────────────────

int TorrentRepository::schemaVersion() {
    const QString v = metaValue(QStringLiteral("schema_version"));
    return v.isEmpty() ? 0 : v.toInt();
}

void TorrentRepository::setSchemaVersion(int version) {
    setMetaValue(QStringLiteral("schema_version"), QString::number(version));
}

QString TorrentRepository::metaValue(const QString& key) {
    if (!m_open) return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM schema_meta WHERE key = :key"));
    q.bindValue(QStringLiteral(":key"), key);
    if (!q.exec() || !q.next()) return {};
    return q.value(0).toString();
}

void TorrentRepository::setMetaValue(const QString& key, const QString& value) {
    if (!m_open) return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO schema_meta (key, value) VALUES (:k, :v) "
        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    q.bindValue(QStringLiteral(":k"), key);
    q.bindValue(QStringLiteral(":v"), value);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] setMetaValue failed:" << q.lastError().text();
    }
}

bool TorrentRepository::hasTable(const QString& tableName) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
    q.bindValue(QStringLiteral(":name"), tableName);
    if (!q.exec()) return false;
    return q.next();
}

// ─── transactions ────────────────────────────────────────────────────────────

bool TorrentRepository::beginTransaction() {
    return m_open && m_db.transaction();
}

bool TorrentRepository::commit() {
    return m_open && m_db.commit();
}

bool TorrentRepository::rollback() {
    return m_open && m_db.rollback();
}

// ─── torrents CRUD ───────────────────────────────────────────────────────────

namespace {

// Default-constructed QStrings are isNull() == true. Qt's SQLite driver
// serialises a null QString as SQL NULL, which violates the NOT NULL
// constraints carried by every TEXT column on the torrents table. Funnel all
// string binds through this helper so callers that leave fields default-
// constructed (the legacy importer, future TorrentClient writers, tests) reach
// the column as the empty string, matching its DEFAULT '' clause.
static QVariant bindStrOrEmpty(const QString& s) {
    return s.isNull() ? QVariant(QString::fromUtf8("")) : QVariant(s);
}

// TU-local helper — projects an executed query row onto a TorrentRow. The
// SELECT column order matches kTorrentSelectCols so list / get implementations
// stay in lockstep.
static TorrentRow torrentRowFromQuery(QSqlQuery& q) {
    TorrentRow r;
    r.hash           = q.value(0).toString();
    r.state          = torrentStateFromString(q.value(1).toString())
                           .value_or(TorrentState::Active);
    r.name           = q.value(2).toString();
    r.addedAt        = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
    r.category       = q.value(4).toString();
    r.savePath       = q.value(5).toString();
    r.contentLayout  = q.value(6).toString();
    r.streamGroupId  = q.value(7).toString();
    r.sequential     = q.value(8).toInt() != 0;
    r.imdbId         = q.value(9).toString();
    r.season         = q.value(10).toInt();
    r.magnetUri      = q.value(11).toString();
    r.legacyNoMagnet = q.value(12).toInt() != 0;
    r.errorMessage   = q.value(13).toString();
    r.resumeData     = q.value(14).toByteArray();
    r.torrentFile    = q.value(15).toByteArray();
    return r;
}

static const char* kTorrentSelectCols =
    "hash, state, name, added_at, category, save_path, content_layout, "
    "stream_group_id, sequential, imdb_id, season, magnet_uri, "
    "legacy_no_magnet, error_message, resume_data, torrent_file";

}  // namespace

bool TorrentRepository::upsertTorrent(const TorrentRow& row) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO torrents (
            hash, state, name, added_at, category, save_path, content_layout,
            stream_group_id, sequential, imdb_id, season, magnet_uri,
            legacy_no_magnet, error_message, resume_data, torrent_file
        ) VALUES (
            :hash, :state, :name, :added_at, :category, :save_path, :content_layout,
            :stream_group_id, :sequential, :imdb_id, :season, :magnet_uri,
            :legacy_no_magnet, :error_message, :resume_data, :torrent_file
        )
        ON CONFLICT(hash) DO UPDATE SET
            state = excluded.state,
            name = excluded.name,
            category = excluded.category,
            save_path = excluded.save_path,
            content_layout = excluded.content_layout,
            stream_group_id = excluded.stream_group_id,
            sequential = excluded.sequential,
            imdb_id = excluded.imdb_id,
            season = excluded.season,
            magnet_uri = CASE WHEN excluded.magnet_uri != '' THEN excluded.magnet_uri ELSE torrents.magnet_uri END,
            legacy_no_magnet = excluded.legacy_no_magnet,
            error_message = excluded.error_message,
            resume_data = excluded.resume_data,
            torrent_file = excluded.torrent_file
    )SQL"));
    q.bindValue(QStringLiteral(":hash"), row.hash.toLower());
    q.bindValue(QStringLiteral(":state"), torrentStateToString(row.state));
    q.bindValue(QStringLiteral(":name"), bindStrOrEmpty(row.name));
    q.bindValue(QStringLiteral(":added_at"), row.addedAt.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":category"), bindStrOrEmpty(row.category));
    q.bindValue(QStringLiteral(":save_path"), bindStrOrEmpty(row.savePath));
    q.bindValue(QStringLiteral(":content_layout"), bindStrOrEmpty(row.contentLayout));
    q.bindValue(QStringLiteral(":stream_group_id"), bindStrOrEmpty(row.streamGroupId));
    q.bindValue(QStringLiteral(":sequential"), row.sequential ? 1 : 0);
    q.bindValue(QStringLiteral(":imdb_id"), bindStrOrEmpty(row.imdbId));
    q.bindValue(QStringLiteral(":season"), row.season);
    q.bindValue(QStringLiteral(":magnet_uri"), bindStrOrEmpty(row.magnetUri));
    q.bindValue(QStringLiteral(":legacy_no_magnet"), row.legacyNoMagnet ? 1 : 0);
    q.bindValue(QStringLiteral(":error_message"), bindStrOrEmpty(row.errorMessage));
    q.bindValue(QStringLiteral(":resume_data"), row.resumeData);
    q.bindValue(QStringLiteral(":torrent_file"), row.torrentFile);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] upsertTorrent failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::updateTorrentState(const QString& hash,
                                           TorrentState state,
                                           const QString& errorMessage) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE torrents SET state = :state, error_message = :err WHERE hash = :hash"));
    q.bindValue(QStringLiteral(":state"), torrentStateToString(state));
    q.bindValue(QStringLiteral(":err"), bindStrOrEmpty(errorMessage));
    q.bindValue(QStringLiteral(":hash"), hash.toLower());
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] updateTorrentState failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::updateTorrentResumeData(const QString& hash, const QByteArray& blob) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE torrents SET resume_data = :blob WHERE hash = :hash"));
    q.bindValue(QStringLiteral(":blob"), blob);
    q.bindValue(QStringLiteral(":hash"), hash.toLower());
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] updateTorrentResumeData failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::updateTorrentName(const QString& hash, const QString& name) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE torrents SET name = :name WHERE hash = :hash"));
    q.bindValue(QStringLiteral(":name"), bindStrOrEmpty(name));
    q.bindValue(QStringLiteral(":hash"), hash.toLower());
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] updateTorrentName failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::updateTorrentSavePath(const QString& hash, const QString& savePath) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE torrents SET save_path = :sp WHERE hash = :hash"));
    q.bindValue(QStringLiteral(":sp"), bindStrOrEmpty(savePath));
    q.bindValue(QStringLiteral(":hash"), hash.toLower());
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] updateTorrentSavePath failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::removeTorrent(const QString& hash) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM torrents WHERE hash = :hash"));
    q.bindValue(QStringLiteral(":hash"), hash.toLower());
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] removeTorrent failed:" << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<TorrentRow> TorrentRepository::getTorrent(const QString& hash) {
    if (!m_open) return std::nullopt;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM torrents WHERE hash = :hash")
                  .arg(QString::fromUtf8(kTorrentSelectCols)));
    q.bindValue(QStringLiteral(":hash"), hash.toLower());
    if (!q.exec() || !q.next()) return std::nullopt;
    return torrentRowFromQuery(q);
}

bool TorrentRepository::hasTorrent(const QString& hash) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM torrents WHERE hash = :hash LIMIT 1"));
    q.bindValue(QStringLiteral(":hash"), hash.toLower());
    return q.exec() && q.next();
}

std::vector<TorrentRow> TorrentRepository::listTorrents() {
    std::vector<TorrentRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM torrents")
                    .arg(QString::fromUtf8(kTorrentSelectCols)))) {
        qWarning() << "[TorrentRepository] listTorrents failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(torrentRowFromQuery(q));
    return out;
}

std::vector<TorrentRow> TorrentRepository::listTorrentsByState(TorrentState state) {
    std::vector<TorrentRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM torrents WHERE state = :state")
                  .arg(QString::fromUtf8(kTorrentSelectCols)));
    q.bindValue(QStringLiteral(":state"), torrentStateToString(state));
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listTorrentsByState failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(torrentRowFromQuery(q));
    return out;
}

std::vector<TorrentRow> TorrentRepository::listTorrentsByImdb(const QString& imdbId, int season) {
    std::vector<TorrentRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
                  "SELECT %1 FROM torrents WHERE imdb_id = :imdb AND season = :season")
                  .arg(QString::fromUtf8(kTorrentSelectCols)));
    q.bindValue(QStringLiteral(":imdb"), imdbId);
    q.bindValue(QStringLiteral(":season"), season);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listTorrentsByImdb failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(torrentRowFromQuery(q));
    return out;
}

std::vector<TorrentRow> TorrentRepository::listTorrentsByStreamGroup(const QString& streamGroupId) {
    std::vector<TorrentRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM torrents WHERE stream_group_id = :sg")
                  .arg(QString::fromUtf8(kTorrentSelectCols)));
    q.bindValue(QStringLiteral(":sg"), streamGroupId);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listTorrentsByStreamGroup failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(torrentRowFromQuery(q));
    return out;
}

// ─── stream_groups + stream_group_items CRUD ─────────────────────────────

namespace {

// TU-local projection from an executed SELECT against stream_groups. Column
// order must match kStreamGroupSelectCols below to keep get / list paths in
// lockstep.
static StreamGroupRow streamGroupFromQuery(QSqlQuery& q) {
    StreamGroupRow r;
    r.groupId         = q.value(0).toString();
    r.imdbId          = q.value(1).toString();
    r.season          = q.value(2).toInt();
    r.label           = q.value(3).toString();
    r.state           = q.value(4).toString();
    r.retryGeneration = q.value(5).toInt();
    r.stagingPath     = q.value(6).toString();
    r.createdAt       = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
    r.packMode        = q.value(8).toInt() != 0;
    return r;
}

// Same idea for stream_group_items. info_hash is read back as an empty string
// when the underlying column is SQL NULL (which is what happens after the
// referenced torrents row is deleted via ON DELETE SET NULL).
static StreamGroupItemRow streamGroupItemFromQuery(QSqlQuery& q) {
    StreamGroupItemRow r;
    r.groupId      = q.value(0).toString();
    r.itemId       = q.value(1).toString();
    r.episode      = q.value(2).toInt();
    r.infoHash     = q.value(3).toString();
    r.state        = q.value(4).toString();
    r.errorMessage = q.value(5).toString();
    r.fileIndex    = q.value(6).toInt();
    return r;
}

static const char* kStreamGroupSelectCols =
    "group_id, imdb_id, season, label, state, retry_generation, "
    "staging_path, created_at, pack_mode";

static const char* kStreamGroupItemSelectCols =
    "group_id, item_id, episode, info_hash, state, error_message, file_index";

}  // namespace

bool TorrentRepository::upsertStreamGroup(const StreamGroupRow& row) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO stream_groups (
            group_id, imdb_id, season, label, state, retry_generation,
            staging_path, created_at, pack_mode
        ) VALUES (
            :group_id, :imdb_id, :season, :label, :state, :retry_generation,
            :staging_path, :created_at, :pack_mode
        )
        ON CONFLICT(group_id) DO UPDATE SET
            imdb_id = excluded.imdb_id,
            season = excluded.season,
            label = excluded.label,
            state = excluded.state,
            retry_generation = excluded.retry_generation,
            staging_path = excluded.staging_path,
            created_at = excluded.created_at,
            pack_mode = excluded.pack_mode
    )SQL"));
    // Funnel TEXT NOT NULL columns through bindStrOrEmpty so a null QString
    // from the legacy importer (or any defensive caller that didn't pre-
    // normalise an absent JSON key) doesn't trip the NOT NULL constraint.
    // Matches upsertTorrent's pattern; staging_path defaults to '' in schema
    // so the same defence applies there too.
    q.bindValue(QStringLiteral(":group_id"), bindStrOrEmpty(row.groupId));
    q.bindValue(QStringLiteral(":imdb_id"), bindStrOrEmpty(row.imdbId));
    q.bindValue(QStringLiteral(":season"), row.season);
    q.bindValue(QStringLiteral(":label"), bindStrOrEmpty(row.label));
    q.bindValue(QStringLiteral(":state"), bindStrOrEmpty(row.state));
    q.bindValue(QStringLiteral(":retry_generation"), row.retryGeneration);
    q.bindValue(QStringLiteral(":staging_path"), bindStrOrEmpty(row.stagingPath));
    q.bindValue(QStringLiteral(":created_at"), row.createdAt.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":pack_mode"), row.packMode ? 1 : 0);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] upsertStreamGroup failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::removeStreamGroup(const QString& groupId) {
    if (!m_open) return false;
    // FK on stream_group_items(group_id) is ON DELETE CASCADE, so this single
    // DELETE sweeps the items rows as well — verified empirically in
    // tests/core/torrent/test_torrent_repository_groups.cpp RemoveGroupCascadesItems.
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM stream_groups WHERE group_id = :gid"));
    q.bindValue(QStringLiteral(":gid"), groupId);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] removeStreamGroup failed:" << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<StreamGroupRow> TorrentRepository::getStreamGroup(const QString& groupId) {
    if (!m_open) return std::nullopt;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM stream_groups WHERE group_id = :gid")
                  .arg(QString::fromUtf8(kStreamGroupSelectCols)));
    q.bindValue(QStringLiteral(":gid"), groupId);
    if (!q.exec() || !q.next()) return std::nullopt;
    return streamGroupFromQuery(q);
}

std::vector<StreamGroupRow> TorrentRepository::listStreamGroups() {
    std::vector<StreamGroupRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM stream_groups")
                    .arg(QString::fromUtf8(kStreamGroupSelectCols)))) {
        qWarning() << "[TorrentRepository] listStreamGroups failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamGroupFromQuery(q));
    return out;
}

std::vector<StreamGroupRow> TorrentRepository::listStreamGroupsByImdbSeason(const QString& imdbId,
                                                                           int season) {
    std::vector<StreamGroupRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
                  "SELECT %1 FROM stream_groups WHERE imdb_id = :imdb AND season = :season")
                  .arg(QString::fromUtf8(kStreamGroupSelectCols)));
    q.bindValue(QStringLiteral(":imdb"), imdbId);
    q.bindValue(QStringLiteral(":season"), season);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listStreamGroupsByImdbSeason failed:"
                   << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamGroupFromQuery(q));
    return out;
}

bool TorrentRepository::upsertStreamGroupItem(const StreamGroupItemRow& row) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO stream_group_items (
            group_id, item_id, episode, info_hash, state, error_message, file_index
        ) VALUES (
            :group_id, :item_id, :episode, :info_hash, :state, :error_message, :file_index
        )
        ON CONFLICT(group_id, item_id) DO UPDATE SET
            episode = excluded.episode,
            info_hash = excluded.info_hash,
            state = excluded.state,
            error_message = excluded.error_message,
            file_index = excluded.file_index
    )SQL"));
    q.bindValue(QStringLiteral(":group_id"), bindStrOrEmpty(row.groupId));
    q.bindValue(QStringLiteral(":item_id"), bindStrOrEmpty(row.itemId));
    q.bindValue(QStringLiteral(":episode"), row.episode);
    // Bind SQL NULL (not '') when infoHash is empty so the FK constraint stays
    // satisfied and the ON DELETE SET NULL action has a target to overwrite.
    if (row.infoHash.isEmpty()) {
        q.bindValue(QStringLiteral(":info_hash"), QVariant());
    } else {
        q.bindValue(QStringLiteral(":info_hash"), row.infoHash.toLower());
    }
    // state / error_message are TEXT NOT NULL — funnel through bindStrOrEmpty
    // so a default-constructed QString from the importer doesn't trip the
    // constraint (matches upsertTorrent's defence; see upsertStreamGroup).
    q.bindValue(QStringLiteral(":state"), bindStrOrEmpty(row.state));
    q.bindValue(QStringLiteral(":error_message"), bindStrOrEmpty(row.errorMessage));
    q.bindValue(QStringLiteral(":file_index"), row.fileIndex);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] upsertStreamGroupItem failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::removeStreamGroupItem(const QString& groupId, const QString& itemId) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM stream_group_items WHERE group_id = :gid AND item_id = :iid"));
    q.bindValue(QStringLiteral(":gid"), groupId);
    q.bindValue(QStringLiteral(":iid"), itemId);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] removeStreamGroupItem failed:" << q.lastError().text();
        return false;
    }
    return true;
}

std::vector<StreamGroupItemRow> TorrentRepository::listStreamGroupItems(const QString& groupId) {
    std::vector<StreamGroupItemRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM stream_group_items WHERE group_id = :gid")
                  .arg(QString::fromUtf8(kStreamGroupItemSelectCols)));
    q.bindValue(QStringLiteral(":gid"), groupId);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listStreamGroupItems failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamGroupItemFromQuery(q));
    return out;
}

// ─── stream_downloads_index CRUD ─────────────────────────────────────────────

namespace {

// TU-local projection from an executed SELECT against stream_downloads_index.
// Column order must match kStreamDownloadSelectCols below to keep get / list
// paths in lockstep.
static StreamDownloadRow streamDownloadFromQuery(QSqlQuery& q) {
    StreamDownloadRow r;
    r.canonicalPath  = q.value(0).toString();
    r.imdbId         = q.value(1).toString();
    r.season         = q.value(2).toInt();
    r.episode        = q.value(3).toInt();
    r.state          = q.value(4).toString();
    r.infoHash       = q.value(5).toString();
    r.addedAt        = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    r.sourceGroupId  = q.value(7).toString();
    r.progressPct    = q.value(8).toInt();
    return r;
}

static const char* kStreamDownloadSelectCols =
    "canonical_path, imdb_id, season, episode, state, info_hash, added_at, "
    "source_group_id, progress_pct";

}  // namespace

bool TorrentRepository::upsertStreamDownload(const StreamDownloadRow& row) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(R"SQL(
        INSERT INTO stream_downloads_index (
            canonical_path, imdb_id, season, episode, state, info_hash, added_at,
            source_group_id, progress_pct
        ) VALUES (
            :canonical_path, :imdb_id, :season, :episode, :state, :info_hash, :added_at,
            :source_group_id, :progress_pct
        )
        ON CONFLICT(canonical_path) DO UPDATE SET
            imdb_id = excluded.imdb_id,
            season = excluded.season,
            episode = excluded.episode,
            state = excluded.state,
            info_hash = excluded.info_hash,
            added_at = excluded.added_at,
            source_group_id = excluded.source_group_id,
            progress_pct = excluded.progress_pct
    )SQL"));
    // canonical_path is PRIMARY KEY NOT NULL; state is TEXT NOT NULL; imdb_id
    // has DEFAULT '' but binding a null QString still violates it. All three
    // funneled through bindStrOrEmpty for the same defence as
    // upsertStreamGroup / upsertTorrent. source_group_id same treatment.
    q.bindValue(QStringLiteral(":canonical_path"), bindStrOrEmpty(row.canonicalPath));
    q.bindValue(QStringLiteral(":imdb_id"), bindStrOrEmpty(row.imdbId));
    q.bindValue(QStringLiteral(":season"), row.season);
    q.bindValue(QStringLiteral(":episode"), row.episode);
    q.bindValue(QStringLiteral(":state"), bindStrOrEmpty(row.state));
    q.bindValue(QStringLiteral(":source_group_id"), bindStrOrEmpty(row.sourceGroupId));
    q.bindValue(QStringLiteral(":progress_pct"), qBound(0, row.progressPct, 100));
    // Bind SQL NULL (not '') when infoHash is empty so the FK constraint stays
    // satisfied and the ON DELETE SET NULL action has a target to overwrite.
    if (row.infoHash.isEmpty()) {
        q.bindValue(QStringLiteral(":info_hash"), QVariant());
    } else {
        q.bindValue(QStringLiteral(":info_hash"), row.infoHash.toLower());
    }
    q.bindValue(QStringLiteral(":added_at"), row.addedAt.toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] upsertStreamDownload failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool TorrentRepository::removeStreamDownload(const QString& canonicalPath) {
    if (!m_open) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM stream_downloads_index WHERE canonical_path = :cp"));
    q.bindValue(QStringLiteral(":cp"), canonicalPath);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] removeStreamDownload failed:" << q.lastError().text();
        return false;
    }
    return true;
}

std::optional<StreamDownloadRow> TorrentRepository::getStreamDownload(const QString& canonicalPath) {
    if (!m_open) return std::nullopt;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
                  "SELECT %1 FROM stream_downloads_index WHERE canonical_path = :cp")
                  .arg(QString::fromUtf8(kStreamDownloadSelectCols)));
    q.bindValue(QStringLiteral(":cp"), canonicalPath);
    if (!q.exec() || !q.next()) return std::nullopt;
    return streamDownloadFromQuery(q);
}

std::vector<StreamDownloadRow> TorrentRepository::listStreamDownloads() {
    std::vector<StreamDownloadRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM stream_downloads_index")
                    .arg(QString::fromUtf8(kStreamDownloadSelectCols)))) {
        qWarning() << "[TorrentRepository] listStreamDownloads failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamDownloadFromQuery(q));
    return out;
}

std::vector<StreamDownloadRow> TorrentRepository::listStreamDownloadsByImdb(const QString& imdbId,
                                                                            int season) {
    std::vector<StreamDownloadRow> out;
    if (!m_open) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
                  "SELECT %1 FROM stream_downloads_index "
                  "WHERE imdb_id = :imdb AND season = :season")
                  .arg(QString::fromUtf8(kStreamDownloadSelectCols)));
    q.bindValue(QStringLiteral(":imdb"), imdbId);
    q.bindValue(QStringLiteral(":season"), season);
    if (!q.exec()) {
        qWarning() << "[TorrentRepository] listStreamDownloadsByImdb failed:"
                   << q.lastError().text();
        return out;
    }
    while (q.next()) out.push_back(streamDownloadFromQuery(q));
    return out;
}

} // namespace tankoban::torrent
