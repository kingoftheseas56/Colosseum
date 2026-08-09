#include "VaultIndex.h"

#include "VaultKit.h" // CancellationToken

#include <QHash>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVariantMap>

namespace {
// The first Vault-OWNED schema version. Legacy Vault DBs are user_version=0 (VaultIndex never
// stamped one before this slice); a DB stamped higher than this was created by a newer owner.
inline constexpr int kVaultSchemaVersion = 1;

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

// A durable verdict from the current index, keyed by id, with the identity tuple it was probed
// against. publish() carries it forward only when (size,mtimeMs) still match.
struct DurableAdmission
{
    qint64 size = 0;
    qint64 mtimeMs = 0;
    QString verdict;
    QString detail;
};

bool loadDurableAdmissions(QSqlDatabase& db, QHash<QString, DurableAdmission>* out)
{
    out->clear();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "SELECT id, size, mtimeMs, admissionVerdict, admissionDetail "
            "FROM files WHERE admissionVerdict <> ''")))
        return false;

    while (q.next()) {
        DurableAdmission a;
        a.size = q.value(1).toLongLong();
        a.mtimeMs = q.value(2).toLongLong();
        a.verdict = q.value(3).toString();
        a.detail = q.value(4).toString();
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

    // Fresh DBs get the admission columns inline; legacy DBs (below) get them via ALTER. The
    // base column set/order is preserved so existing queries keep working.
    if (!execSchemaSql(m_db, QStringLiteral(
            "CREATE TABLE IF NOT EXISTS files ("
            " id TEXT PRIMARY KEY,"
            " rootPath TEXT, subtreePath TEXT, groupKey TEXT, groupTitle TEXT,"
            " kind TEXT, path TEXT, displayTitle TEXT, realName TEXT, subfolder TEXT,"
            " sortKey TEXT, size INTEGER, mtimeMs INTEGER,"
            " pages INTEGER, durationSec REAL, author TEXT, format TEXT,"
            " progressed INTEGER DEFAULT 0, coverRef TEXT,"
            " admissionVerdict TEXT NOT NULL DEFAULT '',"
            " admissionDetail TEXT NOT NULL DEFAULT '')")))
        return rollback();

    bool columnsOk = false;
    const QSet<QString> columns = tableColumns(m_db, &columnsOk);
    if (!columnsOk)
        return rollback();

    if (version == 0) {
        // Legacy Vault DB (created before this slice, or a table that predates the columns):
        // add the admission columns if the pre-existing table lacked them.
        if (!columns.contains(QStringLiteral("admissionVerdict"))
            && !execSchemaSql(m_db, QStringLiteral(
                "ALTER TABLE files ADD COLUMN "
                "admissionVerdict TEXT NOT NULL DEFAULT ''")))
            return rollback();

        if (!columns.contains(QStringLiteral("admissionDetail"))
            && !execSchemaSql(m_db, QStringLiteral(
                "ALTER TABLE files ADD COLUMN "
                "admissionDetail TEXT NOT NULL DEFAULT ''")))
            return rollback();
    } else {
        // Already stamped v1: the v1 columns MUST be present, or the file is inconsistent.
        if (!columns.contains(QStringLiteral("admissionVerdict"))
            || !columns.contains(QStringLiteral("admissionDetail")))
            return rollback();
    }

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
        "durationSec, author, format, progressed, coverRef, "
        "admissionVerdict, admissionDetail"
        ") VALUES ("
        ":id, :rootPath, :subtreePath, :groupKey, :groupTitle, :kind, :path, "
        ":displayTitle, :realName, :subfolder, :sortKey, :size, :mtimeMs, :pages, "
        ":durationSec, :author, :format, :progressed, :coverRef, "
        ":admissionVerdict, :admissionDetail)"));

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
    q.bindValue(QStringLiteral(":progressed"), row.progressed ? 1 : 0);
    q.bindValue(QStringLiteral(":coverRef"), row.coverRef);
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

    // Snapshot the durable verdicts BEFORE the destructive wipe, so an unchanged file keeps the
    // admission it was already probed for and the next enrichment pass doesn't re-probe it.
    QHash<QString, DurableAdmission> durable;
    if (!loadDurableAdmissions(m_db, &durable))
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

        // Carry a prior verdict only when the caller supplied none AND the identity tuple is
        // byte-for-byte unchanged. A changed size or mtime deliberately drops it so the file is
        // re-probed; an explicit fresh verdict on `row` always wins.
        if (row.admissionVerdict.isEmpty()) {
            const auto it = durable.constFind(row.id);
            if (it != durable.constEnd()
                && it->size == row.size
                && it->mtimeMs == row.mtimeMs) {
                row.admissionVerdict = it->verdict;
                row.admissionDetail = it->detail;
            } else {
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
        "       pages, durationSec, author, format, progressed, coverRef,"
        "       admissionVerdict, admissionDetail"
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
            r.admissionVerdict = q.value(19).toString();
            r.admissionDetail = q.value(20).toString();
            out.append(r);
        }
    }
    return out;
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
            m[QStringLiteral("coverPath")] = q.value(5).toString();
            m[QStringLiteral("coverEntry")] = q.value(6).toString();
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
        "       pages, durationSec, author, format, progressed, coverRef"
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
            out.append(m);
        }
    }
    return out;
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
