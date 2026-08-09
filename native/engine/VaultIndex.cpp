#include "VaultIndex.h"

#include "VaultKit.h" // CancellationToken

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QVariantMap>

VaultIndex::VaultIndex(const QString& dbPath, QObject* parent)
    : QObject(parent)
{
    m_conn = QStringLiteral("vaultindex_%1")
                 .arg(reinterpret_cast<quintptr>(this), 0, 16);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(dbPath);
    if (m_db.open())
        ensureSchema();
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

void VaultIndex::ensureSchema()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS files ("
        " id TEXT PRIMARY KEY,"
        " rootPath TEXT, subtreePath TEXT, groupKey TEXT, groupTitle TEXT,"
        " kind TEXT, path TEXT, displayTitle TEXT, realName TEXT, subfolder TEXT,"
        " sortKey TEXT, size INTEGER, mtimeMs INTEGER,"
        " pages INTEGER, durationSec REAL, author TEXT, format TEXT,"
        " progressed INTEGER DEFAULT 0, coverRef TEXT)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_files_kind ON files(kind)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_files_subtree ON files(subtreePath)"));
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
        "INSERT OR REPLACE INTO files"
        " (id, rootPath, subtreePath, groupKey, groupTitle, kind, path,"
        "  displayTitle, realName, subfolder, sortKey, size, mtimeMs,"
        "  pages, durationSec, author, format, progressed, coverRef)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(row.id);
    q.addBindValue(row.rootPath);
    q.addBindValue(row.subtreePath);
    q.addBindValue(row.groupKey);
    q.addBindValue(row.groupTitle);
    q.addBindValue(row.kind);
    q.addBindValue(row.path);
    q.addBindValue(row.displayTitle);
    q.addBindValue(row.realName);
    q.addBindValue(row.subfolder);
    q.addBindValue(row.sortKey.isEmpty() ? naturalSortKey(row.realName) : row.sortKey);
    q.addBindValue(row.size);
    q.addBindValue(row.mtimeMs);
    q.addBindValue(row.pages);
    q.addBindValue(row.durationSec);
    q.addBindValue(row.author);
    q.addBindValue(row.format);
    q.addBindValue(row.progressed ? 1 : 0);
    q.addBindValue(row.coverRef);
    return q.exec();
}

bool VaultIndex::publish(const QList<FileRow>& rows,
                         const VaultKit::CancellationToken* cancel)
{
    if (!m_db.isOpen())
        return false;
    if (!m_db.transaction())
        return false;

    QSqlQuery del(m_db);
    if (!del.exec(QStringLiteral("DELETE FROM files"))) {
        m_db.rollback();
        return false;
    }
    if (cancel && cancel->isCancelled()) {
        m_db.rollback(); // previous contents restored
        return false;
    }
    for (const FileRow& row : rows) {
        if (cancel && cancel->isCancelled()) {
            m_db.rollback();
            return false;
        }
        if (!insertRow(row)) {
            m_db.rollback();
            return false;
        }
    }
    if (!m_db.commit()) {
        m_db.rollback();
        return false;
    }
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
        "       pages, durationSec, author, format, progressed, coverRef"
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
