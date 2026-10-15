// native/torrent/MangaTorrentIndex.cpp — see header. [Agent 0 (GLM), Arc 18 M3]
#include "torrent/MangaTorrentIndex.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>

namespace MangaTankoban {

const char* const MangaTorrentIndex::kParserVersion = "arc18.1";

namespace {

// Evidence/status/class ↔ TEXT. Single owner so rows never carry ad-hoc spellings.
QString evidenceText(MappingEvidence e)
{
    switch (e) {
    case MappingEvidence::ReleaseOnly: return QStringLiteral("release_only");
    case MappingEvidence::MetainfoExactFilename: return QStringLiteral("metainfo_exact_filename");
    case MappingEvidence::MetainfoExactDirectory: return QStringLiteral("metainfo_exact_directory");
    case MappingEvidence::RuntimeRevalidated: return QStringLiteral("runtime_revalidated");
    }
    return QStringLiteral("release_only");
}

MappingEvidence evidenceFromText(const QString& s)
{
    if (s == QLatin1String("metainfo_exact_filename")) return MappingEvidence::MetainfoExactFilename;
    if (s == QLatin1String("metainfo_exact_directory")) return MappingEvidence::MetainfoExactDirectory;
    if (s == QLatin1String("runtime_revalidated")) return MappingEvidence::RuntimeRevalidated;
    return MappingEvidence::ReleaseOnly;
}

QString statusText(MappingStatus s)
{
    switch (s) {
    case MappingStatus::Candidate: return QStringLiteral("candidate");
    case MappingStatus::Verified: return QStringLiteral("verified");
    case MappingStatus::RejectedAmbiguous: return QStringLiteral("rejected_ambiguous");
    case MappingStatus::RejectedCombined: return QStringLiteral("rejected_combined");
    case MappingStatus::RejectedMissing: return QStringLiteral("rejected_missing");
    case MappingStatus::NeedsRevalidation: return QStringLiteral("needs_revalidation");
    }
    return QStringLiteral("candidate");
}

MappingStatus statusFromText(const QString& s)
{
    if (s == QLatin1String("verified")) return MappingStatus::Verified;
    if (s == QLatin1String("rejected_ambiguous")) return MappingStatus::RejectedAmbiguous;
    if (s == QLatin1String("rejected_combined")) return MappingStatus::RejectedCombined;
    if (s == QLatin1String("rejected_missing")) return MappingStatus::RejectedMissing;
    if (s == QLatin1String("needs_revalidation")) return MappingStatus::NeedsRevalidation;
    return MappingStatus::Candidate;
}

QString errorClassText(SearchErrorClass c)
{
    switch (c) {
    case SearchErrorClass::None: return QStringLiteral("none");
    case SearchErrorClass::Empty: return QStringLiteral("empty");
    case SearchErrorClass::ProviderError: return QStringLiteral("provider_error");
    }
    return QStringLiteral("none");
}

SearchErrorClass errorClassFromText(const QString& s)
{
    if (s == QLatin1String("empty")) return SearchErrorClass::Empty;
    if (s == QLatin1String("provider_error")) return SearchErrorClass::ProviderError;
    return SearchErrorClass::None;
}

// A default-constructed (null) QString binds as SQL NULL through QtSql and
// violates NOT NULL on insert — a partially-filled caller struct must still
// land as ''. Normalize every string bind through here.
QString nz(const QString& s)
{
    return s.isNull() ? QString(QLatin1String("")) : s;
}

// Two-pass schema (TorrentRepository's pattern): tables first, indexes after.
// Semicolon-free comments/values — the blob is split naively on ';'.
const char* kSchemaSqlTables = R"SQL(
CREATE TABLE IF NOT EXISTS torrents (
    info_hash TEXT PRIMARY KEY NOT NULL COLLATE NOCASE,
    provider TEXT NOT NULL DEFAULT 'nyaa',
    provider_id TEXT NOT NULL DEFAULT '',
    release_title TEXT NOT NULL DEFAULT '',
    uploader TEXT NOT NULL DEFAULT '',
    total_size INTEGER NOT NULL DEFAULT 0,
    seeders INTEGER NOT NULL DEFAULT 0,
    leechers INTEGER NOT NULL DEFAULT 0,
    trackers_json TEXT NOT NULL DEFAULT '[]',
    discovered_at INTEGER NOT NULL DEFAULT 0,
    last_seen_at INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS files (
    info_hash TEXT NOT NULL COLLATE NOCASE,
    file_index INTEGER NOT NULL,
    path TEXT NOT NULL,
    size INTEGER NOT NULL DEFAULT 0,
    archive_type TEXT NOT NULL DEFAULT '',
    coverage_kind TEXT NOT NULL DEFAULT 'none',
    coverage_lo TEXT NOT NULL DEFAULT '',
    coverage_hi TEXT NOT NULL DEFAULT '',
    coverage_source TEXT NOT NULL DEFAULT 'none',
    PRIMARY KEY (info_hash, file_index),
    FOREIGN KEY (info_hash) REFERENCES torrents (info_hash) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS volume_mappings (
    volume_id TEXT NOT NULL,
    info_hash TEXT NOT NULL COLLATE NOCASE,
    file_index INTEGER NOT NULL,
    evidence TEXT NOT NULL DEFAULT 'release_only',
    verified_at INTEGER NOT NULL DEFAULT 0,
    parser_version TEXT NOT NULL DEFAULT '',
    status TEXT NOT NULL DEFAULT 'candidate',
    PRIMARY KEY (volume_id, info_hash, file_index),
    FOREIGN KEY (info_hash) REFERENCES torrents (info_hash) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS search_state (
    search_key TEXT PRIMARY KEY NOT NULL,
    last_success_at INTEGER NOT NULL DEFAULT 0,
    last_attempt_at INTEGER NOT NULL DEFAULT 0,
    expires_at INTEGER NOT NULL DEFAULT 0,
    result_count INTEGER NOT NULL DEFAULT 0,
    error_class TEXT NOT NULL DEFAULT 'none'
);
)SQL";

const char* kSchemaSqlIndexes = R"SQL(
CREATE INDEX IF NOT EXISTS idx_volume_mappings_hash ON volume_mappings (info_hash);
CREATE INDEX IF NOT EXISTS idx_volume_mappings_volume ON volume_mappings (volume_id);
)SQL";

} // namespace

MangaTorrentIndex::MangaTorrentIndex(QObject* parent)
    : QObject(parent),
      // Unique connection per store instance — two live stores (an indexer and
      // a cold-reread probe, or app + harness) must never share a connection.
      m_connectionName(QStringLiteral("MangaTorrentIndex_%1")
                           .arg(reinterpret_cast<quintptr>(this), 0, 16))
{
}

MangaTorrentIndex::~MangaTorrentIndex()
{
    close();
}

bool MangaTorrentIndex::open(const QString& dbFilePath)
{
    if (m_open)
        return true;
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbFilePath);
    if (!m_db.open()) {
        qWarning() << "[MangaTorrentIndex] open failed:" << m_db.lastError().text();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        return false;
    }
    QSqlQuery pragma(m_db);
    // per-connection durability PRAGMAs — TorrentRepository's pattern
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA busy_timeout=5000"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    if (!initSchema()) {
        qWarning() << "[MangaTorrentIndex] initSchema failed; closing DB";
        close();
        return false;
    }
    m_open = true;
    return true;
}

void MangaTorrentIndex::close()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_connectionName))
        QSqlDatabase::removeDatabase(m_connectionName);
    m_open = false;
}

bool MangaTorrentIndex::isOpen() const
{
    return m_open;
}

bool MangaTorrentIndex::initSchema()
{
    // QtSql execs ONE statement per call — both blobs are split on ';' and run
    // statement by statement (tables first, then indexes, TorrentRepository's
    // two-pass order). Semicolon-free comments/values keep the split safe.
    const QStringList blobs = { QString::fromLatin1(kSchemaSqlTables),
                                QString::fromLatin1(kSchemaSqlIndexes) };
    for (const QString& blob : blobs) {
        const QStringList stmts = blob.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString& raw : stmts) {
            const QString stmt = raw.trimmed();
            if (stmt.isEmpty())
                continue;
            QSqlQuery q(m_db);
            if (!q.exec(stmt)) {
                qWarning() << "[MangaTorrentIndex] schema stmt failed:"
                           << q.lastError().text();
                return false;
            }
        }
    }
    return true;
}

bool MangaTorrentIndex::upsertTorrent(const IndexedTorrent& torrent)
{
    if (!m_open || torrent.infoHash.isEmpty())
        return false;
    QSqlQuery q(m_db);
    // Availability refresh keeps stored identity fields when the incoming row
    // is emptier than what we already know (COALESCE(NULL, stored)).
    q.prepare(QStringLiteral(
        "INSERT INTO torrents (info_hash, provider, provider_id, release_title, uploader,"
        " total_size, seeders, leechers, trackers_json, discovered_at, last_seen_at)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT (info_hash) DO UPDATE SET"
        " provider_id = COALESCE(NULLIF(excluded.provider_id, ''), torrents.provider_id),"
        " release_title = COALESCE(NULLIF(excluded.release_title, ''), torrents.release_title),"
        " uploader = COALESCE(NULLIF(excluded.uploader, ''), torrents.uploader),"
        " total_size = CASE WHEN excluded.total_size > 0 THEN excluded.total_size"
        "                   ELSE torrents.total_size END,"
        " trackers_json = COALESCE(NULLIF(excluded.trackers_json, ''), torrents.trackers_json),"
        " discovered_at = CASE WHEN torrents.discovered_at = 0 THEN excluded.discovered_at"
        "                       ELSE torrents.discovered_at END,"
        " seeders = excluded.seeders, leechers = excluded.leechers,"
        " last_seen_at = MAX(torrents.last_seen_at, excluded.last_seen_at)"));
    q.addBindValue(nz(torrent.infoHash));
    q.addBindValue(nz(torrent.provider));
    q.addBindValue(nz(torrent.providerId));
    q.addBindValue(nz(torrent.releaseTitle));
    q.addBindValue(nz(torrent.uploader));
    q.addBindValue(torrent.totalSize);
    q.addBindValue(torrent.seeders);
    q.addBindValue(torrent.leechers);
    q.addBindValue(nz(torrent.trackersJson));
    q.addBindValue(torrent.discoveredAt);
    q.addBindValue(torrent.lastSeenAt);
    if (!q.exec()) {
        qWarning() << "[MangaTorrentIndex] upsertTorrent failed:" << q.lastError().text();
        return false;
    }
    return true;
}

IndexedTorrent MangaTorrentIndex::torrentRow(const QString& infoHash) const
{
    IndexedTorrent out;
    if (!m_open)
        return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT info_hash, provider, provider_id, release_title, uploader, total_size,"
        " seeders, leechers, trackers_json, discovered_at, last_seen_at"
        " FROM torrents WHERE info_hash = ?"));
    q.addBindValue(infoHash);
    if (!q.exec() || !q.next())
        return out;
    out.infoHash = q.value(0).toString();
    out.provider = q.value(1).toString();
    out.providerId = q.value(2).toString();
    out.releaseTitle = q.value(3).toString();
    out.uploader = q.value(4).toString();
    out.totalSize = q.value(5).toLongLong();
    out.seeders = q.value(6).toInt();
    out.leechers = q.value(7).toInt();
    out.trackersJson = q.value(8).toString();
    out.discoveredAt = q.value(9).toLongLong();
    out.lastSeenAt = q.value(10).toLongLong();
    return out;
}

bool MangaTorrentIndex::replaceFiles(const QString& infoHash, const QList<IndexedFile>& files)
{
    if (!m_open || infoHash.isEmpty())
        return false;
    if (!m_db.transaction())
        return false;
    {
        QSqlQuery del(m_db);
        del.prepare(QStringLiteral("DELETE FROM files WHERE info_hash = ?"));
        del.addBindValue(infoHash);
        if (!del.exec()) {
            m_db.rollback();
            return false;
        }
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT INTO files (info_hash, file_index, path, size, archive_type,"
            " coverage_kind, coverage_lo, coverage_hi, coverage_source)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        for (const IndexedFile& f : files) {
            ins.addBindValue(infoHash);
            ins.addBindValue(f.fileIndex);
            ins.addBindValue(nz(f.path));
            ins.addBindValue(f.size);
            ins.addBindValue(nz(f.archiveType));
            ins.addBindValue(nz(f.coverageKind));
            ins.addBindValue(nz(f.coverageLo));
            ins.addBindValue(nz(f.coverageHi));
            ins.addBindValue(nz(f.coverageSource));
            if (!ins.exec()) {
                qWarning() << "[MangaTorrentIndex] replaceFiles insert failed:"
                           << ins.lastError().text();
                m_db.rollback();
                return false;
            }
        }
    }
    return m_db.commit();
}

QList<IndexedFile> MangaTorrentIndex::filesForTorrent(const QString& infoHash) const
{
    QList<IndexedFile> out;
    if (!m_open)
        return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT file_index, path, size, archive_type, coverage_kind, coverage_lo,"
        " coverage_hi, coverage_source FROM files WHERE info_hash = ? ORDER BY file_index"));
    q.addBindValue(infoHash);
    if (!q.exec())
        return out;
    while (q.next()) {
        IndexedFile f;
        f.fileIndex = q.value(0).toInt();
        f.path = q.value(1).toString();
        f.size = q.value(2).toLongLong();
        f.archiveType = q.value(3).toString();
        f.coverageKind = q.value(4).toString();
        f.coverageLo = q.value(5).toString();
        f.coverageHi = q.value(6).toString();
        f.coverageSource = q.value(7).toString();
        out.append(f);
    }
    return out;
}

bool MangaTorrentIndex::insertMapping(const VolumeMapping& mapping)
{
    if (!m_open || mapping.volumeId.isEmpty() || mapping.infoHash.isEmpty())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO volume_mappings (volume_id, info_hash, file_index, evidence,"
        " verified_at, parser_version, status) VALUES (?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT (volume_id, info_hash, file_index) DO UPDATE SET"
        " evidence = excluded.evidence, verified_at = excluded.verified_at,"
        " parser_version = excluded.parser_version, status = excluded.status"));
    q.addBindValue(nz(mapping.volumeId));
    q.addBindValue(nz(mapping.infoHash));
    q.addBindValue(mapping.fileIndex);
    q.addBindValue(evidenceText(mapping.evidence));
    q.addBindValue(mapping.verifiedAt);
    q.addBindValue(mapping.parserVersion.isEmpty()
                       ? QString::fromLatin1(kParserVersion)
                       : nz(mapping.parserVersion));
    q.addBindValue(statusText(mapping.status));
    if (!q.exec()) {
        qWarning() << "[MangaTorrentIndex] insertMapping failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool MangaTorrentIndex::updateMappingStatus(const QString& volumeId, const QString& infoHash,
                                            int fileIndex, MappingStatus status, qint64 atMs)
{
    if (!m_open)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE volume_mappings SET status = ?, verified_at = ?"
        " WHERE volume_id = ? AND info_hash = ? AND file_index = ?"));
    q.addBindValue(statusText(status));
    q.addBindValue(atMs);
    q.addBindValue(volumeId);
    q.addBindValue(infoHash);
    q.addBindValue(fileIndex);
    if (!q.exec()) {
        qWarning() << "[MangaTorrentIndex] updateMappingStatus failed:" << q.lastError().text();
        return false;
    }
    return q.numRowsAffected() > 0;
}

namespace {
QList<VolumeMapping> runMappingQuery(QSqlQuery& q)
{
    QList<VolumeMapping> out;
    if (!q.exec())
        return out;
    while (q.next()) {
        VolumeMapping m;
        m.volumeId = q.value(0).toString();
        m.infoHash = q.value(1).toString();
        m.fileIndex = q.value(2).toInt();
        m.evidence = evidenceFromText(q.value(3).toString());
        m.verifiedAt = q.value(4).toLongLong();
        m.parserVersion = q.value(5).toString();
        m.status = statusFromText(q.value(6).toString());
        out.append(m);
    }
    // Verified rows first, then best evidence first — the lookup path (M5)
    // consumes index 0 as the answer.
    std::sort(out.begin(), out.end(), [](const VolumeMapping& a, const VolumeMapping& b) {
        const bool aVerified = a.status == MappingStatus::Verified;
        const bool bVerified = b.status == MappingStatus::Verified;
        if (aVerified != bVerified)
            return aVerified;
        return a.evidence > b.evidence; // higher enum = stronger evidence
    });
    return out;
}
} // namespace

QList<VolumeMapping> MangaTorrentIndex::mappingsForVolume(const QString& volumeId) const
{
    if (!m_open)
        return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT volume_id, info_hash, file_index, evidence, verified_at, parser_version,"
        " status FROM volume_mappings WHERE volume_id = ?"));
    q.addBindValue(volumeId);
    return runMappingQuery(q);
}

QList<VolumeMapping> MangaTorrentIndex::mappingsForTorrent(const QString& infoHash) const
{
    if (!m_open)
        return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT volume_id, info_hash, file_index, evidence, verified_at, parser_version,"
        " status FROM volume_mappings WHERE info_hash = ?"));
    q.addBindValue(infoHash);
    return runMappingQuery(q);
}

bool MangaTorrentIndex::recordSearchSuccess(const QString& searchKey, int resultCount,
                                            qint64 atMs)
{
    if (!m_open || searchKey.isEmpty())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO search_state (search_key, last_success_at, last_attempt_at, expires_at,"
        " result_count, error_class) VALUES (?, ?, ?, ?, ?, ?)"
        " ON CONFLICT (search_key) DO UPDATE SET"
        " last_success_at = excluded.last_success_at,"
        " last_attempt_at = excluded.last_attempt_at,"
        " expires_at = excluded.expires_at,"
        " result_count = excluded.result_count,"
        " error_class = excluded.error_class"));
    // Negative TTL ONLY for a successful empty answer — a provider that
    // answered "nothing" is trusted briefly; a provider that failed is not.
    const qint64 ttl = (resultCount == 0) ? atMs + MangaTorrentIndex::kNegativeTtlMs : 0;
    q.addBindValue(searchKey);
    q.addBindValue(atMs);
    q.addBindValue(atMs);
    q.addBindValue(ttl);
    q.addBindValue(resultCount);
    q.addBindValue(errorClassText(resultCount == 0 ? SearchErrorClass::Empty
                                                   : SearchErrorClass::None));
    if (!q.exec()) {
        qWarning() << "[MangaTorrentIndex] recordSearchSuccess failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool MangaTorrentIndex::recordSearchError(const QString& searchKey, qint64 atMs)
{
    if (!m_open || searchKey.isEmpty())
        return false;
    // Records the attempt with NO expiry: errors are retryable immediately and
    // never mask a refresh. Touches no mapping rows — an outage cannot delete
    // verified identity (contract §5).
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO search_state (search_key, last_success_at, last_attempt_at, expires_at,"
        " result_count, error_class) VALUES (?, 0, ?, 0, 0, ?)"
        " ON CONFLICT (search_key) DO UPDATE SET"
        " last_attempt_at = excluded.last_attempt_at,"
        " error_class = excluded.error_class"));
    q.addBindValue(searchKey);
    q.addBindValue(atMs);
    q.addBindValue(errorClassText(SearchErrorClass::ProviderError));
    if (!q.exec()) {
        qWarning() << "[MangaTorrentIndex] recordSearchError failed:" << q.lastError().text();
        return false;
    }
    return true;
}

IndexedSearchState MangaTorrentIndex::searchState(const QString& searchKey) const
{
    IndexedSearchState out;
    if (!m_open)
        return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT last_success_at, last_attempt_at, expires_at, result_count, error_class"
        " FROM search_state WHERE search_key = ?"));
    q.addBindValue(searchKey);
    if (!q.exec() || !q.next())
        return out;
    out.lastSuccessAt = q.value(0).toLongLong();
    out.lastAttemptAt = q.value(1).toLongLong();
    out.expiresAt = q.value(2).toLongLong();
    out.resultCount = q.value(3).toInt();
    out.errorClass = errorClassFromText(q.value(4).toString());
    return out;
}

bool MangaTorrentIndex::searchNegativeCached(const QString& searchKey, qint64 nowMs) const
{
    const IndexedSearchState s = searchState(searchKey);
    return s.errorClass == SearchErrorClass::Empty && s.expiresAt > nowMs;
}

} // namespace MangaTankoban
