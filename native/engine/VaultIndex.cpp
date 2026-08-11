#include "VaultIndex.h"

#include "VaultKit.h" // CancellationToken

#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVariantMap>

namespace {
// Vault schema v3 adds durable embedded metadata state. A DB stamped higher than this was created by a
// newer owner and is refused rather than downgraded.
inline constexpr int kVaultSchemaVersion = 3;

bool execSchemaSql(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery q(db);
    return q.exec(sql);
}

int readUserVersion(QSqlDatabase& db, bool* ok)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) {
        *ok = false;
        return -1;
    }
    *ok = true;
    return q.value(0).toInt();
}

QSet<QString> tableColumns(QSqlDatabase& db, bool* ok)
{
    QSet<QString> out;
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(files)"))) {
        *ok = false;
        return out;
    }
    while (q.next())
        out.insert(q.value(1).toString());
    *ok = true;
    return out;
}

// Durable enrichment facts from the current index, keyed by id, with the identity tuple they were
// derived from. publish() carries them forward only when (size,mtimeMs) still match.
struct DurableFacts
{
    qint64 size = 0;
    qint64 mtimeMs = 0;
    bool progressed = false;
    QString verdict;
    QString detail;
    QString errorState;
    QString errorDetail;
    QString displayTitle;
    QString author;
    QString format;
    QString coverRef;
    QString synopsis;
    QString metadataSource;
};

bool loadDurableFacts(QSqlDatabase& db, QHash<QString, DurableFacts>* out)
{
    out->clear();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id, size, mtimeMs, progressed, admissionVerdict, admissionDetail, "
            "       errorState, errorDetail, displayTitle, author, format, coverRef, "
            "       synopsis, metadataSource FROM files "
            "WHERE progressed <> 0 OR admissionVerdict <> '' OR errorState <> '' "
            "   OR coverRef <> '' OR author <> '' OR synopsis <> '' OR metadataSource <> ''")))
        return false;

    while (q.next()) {
        DurableFacts a;
        a.size = q.value(1).toLongLong();
        a.mtimeMs = q.value(2).toLongLong();
        a.progressed = q.value(3).toBool();
        a.verdict = q.value(4).toString();
        a.detail = q.value(5).toString();
        a.errorState = q.value(6).toString();
        a.errorDetail = q.value(7).toString();
        a.displayTitle = q.value(8).toString();
        a.author = q.value(9).toString();
        a.format = q.value(10).toString();
        a.coverRef = q.value(11).toString();
        a.synopsis = q.value(12).toString();
        a.metadataSource = q.value(13).toString();
        out->insert(q.value(0).toString(), a);
    }
    return true;
}
} // namespace

VaultIndex::VaultIndex(const QString& dbPath, QObject* parent)
    : QObject(parent)
{
    m_conn = QStringLiteral("vaultindex_%1")
                 .arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(dbPath);
    if (m_db.open() && !ensureSchema())
        m_db.close(); // a newer-owned or unreadable schema fails closed — isOpen() stays false
}

VaultIndex::~VaultIndex()
{
    const QString conn = m_conn;
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase(); // drop our handle before removing the connection
    QSqlDatabase::removeDatabase(conn);
}

bool VaultIndex::isOpen() const
{
    return m_db.isOpen();
}

bool VaultIndex::ensureSchema()
{
    if (!m_db.isOpen())
        return false;

    bool versionOk = false;
    const int version = readUserVersion(m_db, &versionOk);
    if (!versionOk || version < 0 || version > kVaultSchemaVersion)
        return false; // never downgrade a DB created by a newer owner

    if (!m_db.transaction())
        return false;

    auto rollback = [this]() {
        m_db.rollback();
        return false;
    };

    // Fresh DBs get the v1 admission + v2 resilience + v3 metadata columns inline; legacy DBs (below) get them
    // via ALTER. The original column set/order is preserved so existing queries keep working.
    if (!execSchemaSql(m_db, QStringLiteral(
            "CREATE TABLE IF NOT EXISTS files ("
            " id TEXT PRIMARY KEY,"
            " rootPath TEXT, subtreePath TEXT, groupKey TEXT, groupTitle TEXT,"
            " kind TEXT, path TEXT, displayTitle TEXT, realName TEXT, subfolder TEXT,"
            " sortKey TEXT, size INTEGER, mtimeMs INTEGER,"
            " pages INTEGER, durationSec REAL, author TEXT, format TEXT,"
            " progressed INTEGER DEFAULT 0, coverRef TEXT,"
            " away INTEGER NOT NULL DEFAULT 0,"
            " errorState TEXT NOT NULL DEFAULT '',"
            " errorDetail TEXT NOT NULL DEFAULT '',"
            " admissionVerdict TEXT NOT NULL DEFAULT '',"
            " admissionDetail TEXT NOT NULL DEFAULT '',"
            " synopsis TEXT NOT NULL DEFAULT '',"
            " metadataSource TEXT NOT NULL DEFAULT '')")))
        return rollback();

    bool columnsOk = false;
    const QSet<QString> columns = tableColumns(m_db, &columnsOk);
    if (!columnsOk)
        return rollback();

    // A v0 DB predates admission; v1 has admission but predates resilience; v2 has everything.
    // Add missing columns monotonically, never dropping or rewriting user rows.
    if (version == 0 && !columns.contains(QStringLiteral("admissionVerdict"))
        && !execSchemaSql(m_db, QStringLiteral(
            "ALTER TABLE files ADD COLUMN admissionVerdict TEXT NOT NULL DEFAULT ''")))
        return rollback();
    if (version == 0 && !columns.contains(QStringLiteral("admissionDetail"))
        && !execSchemaSql(m_db, QStringLiteral(
            "ALTER TABLE files ADD COLUMN admissionDetail TEXT NOT NULL DEFAULT ''")))
        return rollback();

    const QSet<QString> afterAdmission = tableColumns(m_db, &columnsOk);
    if (!columnsOk)
        return rollback();
    if (!afterAdmission.contains(QStringLiteral("away"))
        && !execSchemaSql(m_db, QStringLiteral(
            "ALTER TABLE files ADD COLUMN away INTEGER NOT NULL DEFAULT 0")))
        return rollback();
    if (!afterAdmission.contains(QStringLiteral("errorState"))
        && !execSchemaSql(m_db, QStringLiteral(
            "ALTER TABLE files ADD COLUMN errorState TEXT NOT NULL DEFAULT ''")))
        return rollback();
    if (!afterAdmission.contains(QStringLiteral("errorDetail"))
        && !execSchemaSql(m_db, QStringLiteral(
            "ALTER TABLE files ADD COLUMN errorDetail TEXT NOT NULL DEFAULT ''")))
        return rollback();

    const QSet<QString> afterResilience = tableColumns(m_db, &columnsOk);
    if (!columnsOk)
        return rollback();
    if (!afterResilience.contains(QStringLiteral("synopsis"))
        && !execSchemaSql(m_db, QStringLiteral(
            "ALTER TABLE files ADD COLUMN synopsis TEXT NOT NULL DEFAULT ''")))
        return rollback();
    if (!afterResilience.contains(QStringLiteral("metadataSource"))
        && !execSchemaSql(m_db, QStringLiteral(
            "ALTER TABLE files ADD COLUMN metadataSource TEXT NOT NULL DEFAULT ''")))
        return rollback();

    const QSet<QString> finalColumns = tableColumns(m_db, &columnsOk);
    if (!columnsOk
        || !finalColumns.contains(QStringLiteral("admissionVerdict"))
        || !finalColumns.contains(QStringLiteral("admissionDetail"))
        || !finalColumns.contains(QStringLiteral("away"))
        || !finalColumns.contains(QStringLiteral("errorState"))
        || !finalColumns.contains(QStringLiteral("errorDetail"))
        || !finalColumns.contains(QStringLiteral("synopsis"))
        || !finalColumns.contains(QStringLiteral("metadataSource")))
        return rollback();

    if (!execSchemaSql(m_db, QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_files_kind ON files(kind)")))
        return rollback();
    if (!execSchemaSql(m_db, QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_files_subtree ON files(subtreePath)")))
        return rollback();

    QSqlQuery stamp(m_db);
    if (!stamp.exec(QStringLiteral("PRAGMA user_version = %1").arg(kVaultSchemaVersion)))
        return rollback();

    return m_db.commit();
}

QString VaultIndex::naturalSortKey(const QString& s)
{
    // Zero-pad each run of digits to a fixed width so a plain lexicographic
    // compare orders numerically; lowercase everything else for case-insensitive
    // order. Width 12 covers realistic volume/episode/byte counts.
    static const int kWidth = 12;
    QString out;
    out.reserve(s.size() + 16);
    int i = 0;
    const int n = s.size();
    while (i < n) {
        if (s.at(i).isDigit()) {
            int j = i;
            while (j < n && s.at(j).isDigit())
                ++j;
            const int len = j - i;
            if (len < kWidth)
                out += QString(kWidth - len, QLatin1Char('0'));
            out += s.mid(i, len);
            i = j;
        } else {
            out += s.at(i).toLower();
            ++i;
        }
    }
    return out;
}

bool VaultIndex::insertRow(const FileRow& row)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO files ("
        "id, rootPath, subtreePath, groupKey, groupTitle, kind, path, "
        "displayTitle, realName, subfolder, sortKey, size, mtimeMs, pages, "
        "durationSec, author, format, progressed, coverRef, away, errorState, errorDetail, "
        "admissionVerdict, admissionDetail, synopsis, metadataSource"
        ") VALUES ("
        ":id, :rootPath, :subtreePath, :groupKey, :groupTitle, :kind, :path, "
        ":displayTitle, :realName, :subfolder, :sortKey, :size, :mtimeMs, :pages, "
        ":durationSec, :author, :format, :progressed, :coverRef, :away, :errorState, :errorDetail, "
        ":admissionVerdict, :admissionDetail, :synopsis, :metadataSource)"));

    q.bindValue(QStringLiteral(":id"), row.id);
    q.bindValue(QStringLiteral(":rootPath"), row.rootPath);
    q.bindValue(QStringLiteral(":subtreePath"), row.subtreePath);
    q.bindValue(QStringLiteral(":groupKey"), row.groupKey);
    q.bindValue(QStringLiteral(":groupTitle"), row.groupTitle);
    q.bindValue(QStringLiteral(":kind"), row.kind);
    q.bindValue(QStringLiteral(":path"), row.path);
    q.bindValue(QStringLiteral(":displayTitle"), row.displayTitle);
    q.bindValue(QStringLiteral(":realName"), row.realName);
    q.bindValue(QStringLiteral(":subfolder"), row.subfolder);
    q.bindValue(QStringLiteral(":sortKey"),
                row.sortKey.isEmpty() ? naturalSortKey(row.realName) : row.sortKey);
    q.bindValue(QStringLiteral(":size"), row.size);
    q.bindValue(QStringLiteral(":mtimeMs"), row.mtimeMs);
    q.bindValue(QStringLiteral(":pages"), row.pages);
    q.bindValue(QStringLiteral(":durationSec"), row.durationSec);
    q.bindValue(QStringLiteral(":author"), row.author);
    q.bindValue(QStringLiteral(":format"), row.format);
    q.bindValue(QStringLiteral(":synopsis"), row.synopsis);
    q.bindValue(QStringLiteral(":metadataSource"), row.metadataSource);
    q.bindValue(QStringLiteral(":progressed"), row.progressed ? 1 : 0);
    q.bindValue(QStringLiteral(":coverRef"), row.coverRef);
    q.bindValue(QStringLiteral(":away"), row.away ? 1 : 0);
    q.bindValue(QStringLiteral(":errorState"), row.errorState);
    q.bindValue(QStringLiteral(":errorDetail"),
                row.errorState.isEmpty() ? QString() : row.errorDetail);
    q.bindValue(QStringLiteral(":admissionVerdict"), row.admissionVerdict);
    // Detail is meaningless without a verdict — never persist a dangling reason.
    q.bindValue(QStringLiteral(":admissionDetail"),
                row.admissionVerdict.isEmpty() ? QString() : row.admissionDetail);
    return q.exec();
}

bool VaultIndex::publish(const QList<FileRow>& rows,
                         const VaultKit::CancellationToken* cancel)
{
    if (!m_db.isOpen() || !m_db.transaction())
        return false;

    auto rollback = [this]() {
        m_db.rollback(); // previous contents restored
        return false;
    };

    // Snapshot durable admission/error facts BEFORE the destructive wipe, so an unchanged file
    // keeps its honest verdict after a rescan and the next enrichment pass does not re-probe it.
    QHash<QString, DurableFacts> durable;
    if (!loadDurableFacts(m_db, &durable))
        return rollback();

    if (cancel && cancel->isCancelled())
        return rollback();

    QSqlQuery clear(m_db);
    if (!clear.exec(QStringLiteral("DELETE FROM files")))
        return rollback();

    for (const FileRow& source : rows) {
        if (cancel && cancel->isCancelled())
            return rollback();

        FileRow row = source;

        // Carry prior verdicts/errors only when the caller supplied none AND the identity tuple is
        // byte-for-byte unchanged. A changed size or mtime deliberately drops them so the file is
        // re-probed; explicit fresh facts always win.
        if (!row.progressed || row.admissionVerdict.isEmpty() || row.errorState.isEmpty()
            || row.coverRef.isEmpty() || row.metadataSource.isEmpty()) {
            const auto it = durable.constFind(row.id);
            if (it != durable.constEnd()
                && it->size == row.size
                && it->mtimeMs == row.mtimeMs) {
                if (!row.progressed) {
                    row.progressed = it->progressed;
                }
                if (row.admissionVerdict.isEmpty()) {
                    row.admissionVerdict = it->verdict;
                    row.admissionDetail = it->detail;
                }
                if (row.errorState.isEmpty()) {
                    row.errorState = it->errorState;
                    row.errorDetail = it->errorDetail;
                }
                if (row.coverRef.isEmpty() && row.metadataSource.isEmpty())
                    row.coverRef = it->coverRef;
                if (row.metadataSource.isEmpty() && !it->metadataSource.isEmpty()) {
                    row.displayTitle = it->displayTitle;
                    row.author = it->author;
                    row.format = it->format;
                    row.coverRef = it->coverRef;
                    row.synopsis = it->synopsis;
                    row.metadataSource = it->metadataSource;
                }
                if (row.author.isEmpty() && row.metadataSource.isEmpty())
                    row.author = it->author;
                if (row.synopsis.isEmpty() && row.metadataSource.isEmpty())
                    row.synopsis = it->synopsis;
            } else if (row.admissionVerdict.isEmpty()) {
                row.admissionDetail.clear();
            }
        }

        if (!insertRow(row))
            return rollback();
    }

    if (!m_db.commit())
        return false;

    emit changed();
    return true;
}

bool VaultIndex::upsert(const FileRow& row)
{
    if (!m_db.isOpen())
        return false;
    if (!insertRow(row))
        return false;
    emit changed();
    return true;
}

bool VaultIndex::upsertMany(const QList<FileRow>& rows)
{
    if (!m_db.isOpen())
        return false;
    if (rows.isEmpty())
        return true;
    if (!m_db.transaction())
        return false;
    for (const FileRow& row : rows) {
        if (!insertRow(row)) {
            m_db.rollback();
            return false;
        }
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return false;
    }
    emit changed(); // one repaint for the whole batch
    return true;
}

QList<VaultIndex::FileRow> VaultIndex::rowsForKind(const QString& kind) const
{
    QList<FileRow> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, rootPath, subtreePath, groupKey, groupTitle, kind, path,"
        "       displayTitle, realName, subfolder, sortKey, size, mtimeMs,"
        "       pages, durationSec, author, format, progressed, coverRef, away,"
        "       errorState, errorDetail, admissionVerdict, admissionDetail"
        "       , synopsis, metadataSource"
        " FROM files WHERE kind = ? ORDER BY subtreePath, sortKey"));
    q.addBindValue(kind);
    if (q.exec()) {
        while (q.next()) {
            FileRow r;
            r.id = q.value(0).toString();
            r.rootPath = q.value(1).toString();
            r.subtreePath = q.value(2).toString();
            r.groupKey = q.value(3).toString();
            r.groupTitle = q.value(4).toString();
            r.kind = q.value(5).toString();
            r.path = q.value(6).toString();
            r.displayTitle = q.value(7).toString();
            r.realName = q.value(8).toString();
            r.subfolder = q.value(9).toString();
            r.sortKey = q.value(10).toString();
            r.size = q.value(11).toLongLong();
            r.mtimeMs = q.value(12).toLongLong();
            r.pages = q.value(13).toInt();
            r.durationSec = q.value(14).toDouble();
            r.author = q.value(15).toString();
            r.format = q.value(16).toString();
            r.progressed = q.value(17).toInt() != 0;
            r.coverRef = q.value(18).toString();
            r.away = q.value(19).toInt() != 0;
            r.errorState = q.value(20).toString();
            r.errorDetail = q.value(21).toString();
            r.admissionVerdict = q.value(22).toString();
            r.admissionDetail = q.value(23).toString();
            r.synopsis = q.value(24).toString();
            r.metadataSource = q.value(25).toString();
            out.append(r);
        }
    }
    return out;
}

QList<VaultIndex::FileRow> VaultIndex::rowsForRoot(const QString& rootPath) const
{
    QList<FileRow> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, rootPath, subtreePath, groupKey, groupTitle, kind, path,"
        "       displayTitle, realName, subfolder, sortKey, size, mtimeMs,"
        "       pages, durationSec, author, format, progressed, coverRef, away,"
        "       errorState, errorDetail, admissionVerdict, admissionDetail"
        "       , synopsis, metadataSource"
        " FROM files WHERE rootPath = ? ORDER BY subtreePath, sortKey"));
    q.addBindValue(rootPath);
    if (!q.exec())
        return out;
    while (q.next()) {
        FileRow r;
        r.id = q.value(0).toString();
        r.rootPath = q.value(1).toString();
        r.subtreePath = q.value(2).toString();
        r.groupKey = q.value(3).toString();
        r.groupTitle = q.value(4).toString();
        r.kind = q.value(5).toString();
        r.path = q.value(6).toString();
        r.displayTitle = q.value(7).toString();
        r.realName = q.value(8).toString();
        r.subfolder = q.value(9).toString();
        r.sortKey = q.value(10).toString();
        r.size = q.value(11).toLongLong();
        r.mtimeMs = q.value(12).toLongLong();
        r.pages = q.value(13).toInt();
        r.durationSec = q.value(14).toDouble();
        r.author = q.value(15).toString();
        r.format = q.value(16).toString();
        r.progressed = q.value(17).toInt() != 0;
        r.coverRef = q.value(18).toString();
        r.away = q.value(19).toInt() != 0;
        r.errorState = q.value(20).toString();
        r.errorDetail = q.value(21).toString();
        r.admissionVerdict = q.value(22).toString();
        r.admissionDetail = q.value(23).toString();
        r.synopsis = q.value(24).toString();
        r.metadataSource = q.value(25).toString();
        out.append(r);
    }
    return out;
}

bool VaultIndex::markRootAway(const QString& rootPath, bool away)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE files SET away = ? WHERE rootPath = ? AND away <> ?"));
    q.addBindValue(away ? 1 : 0);
    q.addBindValue(rootPath);
    q.addBindValue(away ? 1 : 0);
    if (!q.exec() || q.numRowsAffected() <= 0)
        return false;
    emit changed();
    return true;
}

int VaultIndex::itemCount() const
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM files")) && q.next())
        return q.value(0).toInt();
    return 0;
}

int VaultIndex::itemCountForKind(const QString& kind) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM files WHERE kind = ?"));
    q.addBindValue(kind);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

QStringList VaultIndex::kinds() const
{
    QStringList out;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT DISTINCT kind FROM files ORDER BY kind")))
        while (q.next())
            out.append(q.value(0).toString());
    return out;
}

QVariantList VaultIndex::groupsForKind(const QString& kind) const
{
    QVariantList out;
    QSqlQuery q(m_db);
    // A representative cover for the shelf tile: the first enriched file (lowest sortKey
    // with a coverRef) in the group — comics carry a CBZ cover entry; books/video have none
    // yet (their art is a later slice), so coverPath/coverEntry come back empty for them.
    q.prepare(QStringLiteral(
        "SELECT groupKey, subtreePath, groupTitle, kind, COUNT(*) AS n,"
        " SUM(CASE WHEN away <> 0 THEN 1 ELSE 0 END) AS awayCount,"
        " SUM(CASE WHEN errorState <> '' OR"
        "   (kind = 'video' AND admissionVerdict <> '' AND admissionVerdict <> 'Admitted')"
        "   THEN 1 ELSE 0 END) AS errorCount,"
        " (SELECT f2.path FROM files f2 WHERE f2.groupKey = files.groupKey"
        "   AND f2.coverRef <> '' ORDER BY f2.sortKey LIMIT 1) AS coverPath,"
        " (SELECT f2.coverRef FROM files f2 WHERE f2.groupKey = files.groupKey"
        "   AND f2.coverRef <> '' ORDER BY f2.sortKey LIMIT 1) AS coverEntry"
        " FROM files WHERE kind = ? GROUP BY groupKey"
        " ORDER BY groupTitle COLLATE NOCASE"));
    q.addBindValue(kind);
    if (q.exec()) {
        while (q.next()) {
            QVariantMap m;
            m[QStringLiteral("groupKey")] = q.value(0).toString();
            m[QStringLiteral("subtreePath")] = q.value(1).toString();
            m[QStringLiteral("groupTitle")] = q.value(2).toString();
            m[QStringLiteral("kind")] = q.value(3).toString();
            m[QStringLiteral("count")] = q.value(4).toInt();
            m[QStringLiteral("awayCount")] = q.value(5).toInt();
            m[QStringLiteral("errorCount")] = q.value(6).toInt();
            m[QStringLiteral("coverPath")] = q.value(7).toString();
            m[QStringLiteral("coverEntry")] = q.value(8).toString();
            out.append(m);
        }
    }
    return out;
}

QVariantList VaultIndex::filesInSubtree(const QString& subtreePath) const
{
    QVariantList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, path, displayTitle, realName, subfolder, kind, size, mtimeMs,"
        "       pages, durationSec, author, format, progressed, coverRef, away,"
        "       errorState, errorDetail, admissionVerdict, admissionDetail"
        "       , synopsis, metadataSource"
        " FROM files WHERE subtreePath = ?"
        " ORDER BY subfolder COLLATE NOCASE, sortKey"));
    q.addBindValue(subtreePath);
    if (q.exec()) {
        while (q.next()) {
            QVariantMap m;
            m[QStringLiteral("id")] = q.value(0).toString();
            m[QStringLiteral("path")] = q.value(1).toString();
            m[QStringLiteral("displayTitle")] = q.value(2).toString();
            m[QStringLiteral("realName")] = q.value(3).toString();
            m[QStringLiteral("subfolder")] = q.value(4).toString();
            m[QStringLiteral("kind")] = q.value(5).toString();
            m[QStringLiteral("size")] = q.value(6).toLongLong();
            m[QStringLiteral("mtimeMs")] = q.value(7).toLongLong();
            m[QStringLiteral("pages")] = q.value(8).toInt();
            m[QStringLiteral("durationSec")] = q.value(9).toDouble();
            m[QStringLiteral("author")] = q.value(10).toString();
            m[QStringLiteral("format")] = q.value(11).toString();
            m[QStringLiteral("progressed")] = q.value(12).toInt() != 0;
            m[QStringLiteral("coverRef")] = q.value(13).toString();
            m[QStringLiteral("away")] = q.value(14).toInt() != 0;
            m[QStringLiteral("errorState")] = q.value(15).toString();
            m[QStringLiteral("errorDetail")] = q.value(16).toString();
            m[QStringLiteral("admissionVerdict")] = q.value(17).toString();
            m[QStringLiteral("admissionDetail")] = q.value(18).toString();
            m[QStringLiteral("synopsis")] = q.value(19).toString();
            m[QStringLiteral("metadataSource")] = q.value(20).toString();
            out.append(m);
        }
    }
    return out;
}

QSet<QString> VaultIndex::fileIdsInRoot(const QString& rootPath) const
{
    QSet<QString> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM files WHERE rootPath = ?"));
    q.addBindValue(rootPath);
    if (q.exec()) {
        while (q.next())
            out.insert(q.value(0).toString());
    }
    return out;
}

QString VaultIndex::dominantKindForSubtree(const QString& subtreePath) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT kind FROM files WHERE subtreePath = ?"
        " GROUP BY kind ORDER BY COUNT(*) DESC, kind LIMIT 1"));
    q.addBindValue(subtreePath);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

QVariantMap VaultIndex::admissionById() const
{
    QVariantMap out;
    if (!m_db.isOpen())
        return out;

    // Video-only, verdict-bearing rows: unprobed ("" verdict) and non-video rows are omitted, so
    // QML's Vault Continue gate never sees an ambiguous or irrelevant entry.
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id, admissionVerdict "
            "FROM files "
            "WHERE kind = 'video' AND admissionVerdict <> ''")))
        return out;

    while (q.next())
        out.insert(q.value(0).toString(), q.value(1).toString());

    return out;
}
