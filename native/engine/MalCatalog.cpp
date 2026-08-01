// MalCatalog.cpp — see header. [Agent 0 (Claude), shell]
#include "MalCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

namespace {

// db-stored JSON array of names -> Jikan's [{name: ...}] shape
QVariantList namedList(const QString& json) {
    QVariantList out;
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto& v : arr)
        out.append(QVariantMap{{QStringLiteral("name"), v.toString()}});
    return out;
}

bool validMedium(const QString& m) {
    return m == QStringLiteral("anime") || m == QStringLiteral("manga");
}

} // namespace

MalCatalog::MalCatalog(const QString& dbPath, QObject* parent)
    : QObject(parent), m_conn(QStringLiteral("mal_catalog"))
{
    // resolve beside the exe first (deployed), then the repo layout (dev run)
    QString path = dbPath;
    if (!QFileInfo::exists(path)) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + dbPath;
        if (QFileInfo::exists(beside)) path = beside;
    }
    if (!QFileInfo::exists(path))
        return;                                  // no catalog — live ladder carries the pages
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    m_ok = m_db.open();
}

MalCatalog::~MalCatalog()
{
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_conn);
}

QVariantList MalCatalog::genreEntries(const QString& medium, const QString& genre,
                                      const QString& order, int limit) const
{
    QVariantList out;
    if (!m_ok || !validMedium(medium) || genre.isEmpty())
        return out;
    const bool anime = medium == QStringLiteral("anime");
    // score ordering demands a real score AND a vote floor — a 9.5 from 40
    // voters must not outrank Berserk (the Jikan pages never surfaced those).
    const QString rank = order == QStringLiteral("score")
        ? QStringLiteral("r.scored_by >= 5000 ORDER BY r.score DESC")
        : QStringLiteral("1=1 ORDER BY r.members DESC");
    const QString cols = anime ? QStringLiteral("r.episodes, 0, 0")
                               : QStringLiteral("0, r.volumes, r.chapters");
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT r.mal_id, r.title, r.title_english, r.type, r.score, r.scored_by, "
        "r.members, r.status, %1, r.year, r.cover, r.synopsis, r.credits, r.tags "
        "FROM tag t JOIN %2 r ON r.mal_id = t.mal_id "
        "WHERE t.medium = ? AND t.tag = ? AND %3 LIMIT ?").arg(cols, medium, rank));
    q.addBindValue(medium);
    q.addBindValue(genre);
    q.addBindValue(limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("mal_id"), q.value(0).toInt());
        m.insert(QStringLiteral("title"), q.value(1).toString());
        const QString en = q.value(2).toString();
        if (!en.isEmpty()) m.insert(QStringLiteral("title_english"), en);
        m.insert(QStringLiteral("type"), q.value(3).toString());
        if (!q.value(4).isNull()) m.insert(QStringLiteral("score"), q.value(4).toDouble());
        m.insert(QStringLiteral("scored_by"), q.value(5).toInt());
        m.insert(QStringLiteral("members"), q.value(6).toInt());
        m.insert(QStringLiteral("status"), q.value(7).toString());
        if (anime) {
            m.insert(QStringLiteral("episodes"), q.value(8).toInt());
        } else {
            m.insert(QStringLiteral("volumes"), q.value(9).toInt());
            m.insert(QStringLiteral("chapters"), q.value(10).toInt());
        }
        const int year = q.value(11).toInt();
        if (year > 0) {
            m.insert(QStringLiteral("year"), year);
            // manga cards read year via Jikan's published.prop.from.year
            m.insert(QStringLiteral("published"), QVariantMap{{QStringLiteral("prop"),
                QVariantMap{{QStringLiteral("from"),
                QVariantMap{{QStringLiteral("year"), year}}}}}});
        }
        m.insert(QStringLiteral("images"), QVariantMap{{QStringLiteral("jpg"),
            QVariantMap{{QStringLiteral("large_image_url"), q.value(12).toString()}}}});
        m.insert(QStringLiteral("synopsis"), q.value(13).toString());
        const QVariantList credits = namedList(q.value(14).toString());
        m.insert(anime ? QStringLiteral("studios") : QStringLiteral("authors"), credits);
        m.insert(QStringLiteral("genres"), namedList(q.value(15).toString()));
        out.append(m);
    }
    return out;
}

QVariantList MalCatalog::animeCatalog(const QVariantMap& query, int offset, int limit) const
{
    QVariantList out;
    if (!m_ok)
        return out;

    // Strict key allowlist — any unknown key rejects the whole query (spec §6.2 / decision 2).
    static const QSet<QString> allowed = {
        QStringLiteral("order"), QStringLiteral("status"), QStringLiteral("type"),
        QStringLiteral("tag"), QStringLiteral("yearFrom"), QStringLiteral("yearTo"),
        QStringLiteral("voteFloor"), QStringLiteral("membersMin"), QStringLiteral("membersMax")
    };
    for (auto it = query.constBegin(); it != query.constEnd(); ++it)
        if (!allowed.contains(it.key()))
            return out;

    // order -> a fixed SQL fragment (never interpolated from caller text).
    const QString order = query.value(QStringLiteral("order")).toString();
    QString orderSql;
    if (order.isEmpty() || order == QStringLiteral("members")) orderSql = QStringLiteral("r.members DESC");
    else if (order == QStringLiteral("score"))                 orderSql = QStringLiteral("r.score DESC");
    else if (order == QStringLiteral("year"))                  orderSql = QStringLiteral("r.year DESC");
    else return out;   // unknown order value

    QStringList where;
    QVariantList binds;
    const bool joinTag = query.contains(QStringLiteral("tag"));
    if (joinTag) {
        where << QStringLiteral("t.medium = 'anime' AND t.tag = ?");
        binds << query.value(QStringLiteral("tag")).toString();
    }
    if (query.contains(QStringLiteral("status"))) {
        where << QStringLiteral("r.status = ?");
        binds << query.value(QStringLiteral("status")).toString();
    }
    if (query.contains(QStringLiteral("type"))) {
        where << QStringLiteral("r.type = ?");
        binds << query.value(QStringLiteral("type")).toString();
    }
    if (query.contains(QStringLiteral("yearFrom"))) {
        where << QStringLiteral("r.year >= ?");
        binds << query.value(QStringLiteral("yearFrom")).toInt();
    }
    if (query.contains(QStringLiteral("yearTo"))) {
        // an upper year bound also excludes undated (year 0/NULL) rows
        where << QStringLiteral("r.year <= ? AND r.year > 0");
        binds << query.value(QStringLiteral("yearTo")).toInt();
    }
    if (query.contains(QStringLiteral("voteFloor"))) {
        where << QStringLiteral("r.scored_by >= ?");
        binds << query.value(QStringLiteral("voteFloor")).toInt();
    }
    if (query.contains(QStringLiteral("membersMin"))) {
        where << QStringLiteral("r.members >= ?");
        binds << query.value(QStringLiteral("membersMin")).toInt();
    }
    if (query.contains(QStringLiteral("membersMax"))) {
        where << QStringLiteral("r.members <= ?");
        binds << query.value(QStringLiteral("membersMax")).toInt();
    }

    const int clamped = std::clamp(limit, 1, 100);
    const int off = std::max(0, offset);

    QString sql = QStringLiteral(
        "SELECT r.mal_id, r.title, r.title_english, r.type, r.score, r.scored_by, "
        "r.members, r.status, r.episodes, r.year, r.cover, r.synopsis, r.credits, r.tags FROM ");
    sql += joinTag ? QStringLiteral("tag t JOIN anime r ON r.mal_id = t.mal_id")
                   : QStringLiteral("anime r");
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY ") + orderSql + QStringLiteral(" LIMIT ? OFFSET ?");

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant& b : binds)
        q.addBindValue(b);
    q.addBindValue(clamped);
    q.addBindValue(off);
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("mal_id"), q.value(0).toInt());
        m.insert(QStringLiteral("title"), q.value(1).toString());
        const QString en = q.value(2).toString();
        if (!en.isEmpty()) m.insert(QStringLiteral("title_english"), en);
        m.insert(QStringLiteral("type"), q.value(3).toString());
        if (!q.value(4).isNull()) m.insert(QStringLiteral("score"), q.value(4).toDouble());
        m.insert(QStringLiteral("scored_by"), q.value(5).toInt());
        m.insert(QStringLiteral("members"), q.value(6).toInt());
        m.insert(QStringLiteral("status"), q.value(7).toString());
        m.insert(QStringLiteral("episodes"), q.value(8).toInt());
        const int year = q.value(9).toInt();
        if (year > 0) {
            m.insert(QStringLiteral("year"), year);
            m.insert(QStringLiteral("published"), QVariantMap{{QStringLiteral("prop"),
                QVariantMap{{QStringLiteral("from"),
                QVariantMap{{QStringLiteral("year"), year}}}}}});
        }
        m.insert(QStringLiteral("images"), QVariantMap{{QStringLiteral("jpg"),
            QVariantMap{{QStringLiteral("large_image_url"), q.value(10).toString()}}}});
        m.insert(QStringLiteral("synopsis"), q.value(11).toString());
        m.insert(QStringLiteral("studios"), namedList(q.value(12).toString()));
        m.insert(QStringLiteral("genres"), namedList(q.value(13).toString()));
        out.append(m);
    }
    return out;
}

int MalCatalog::genreCount(const QString& medium, const QString& genre) const
{
    if (!m_ok || !validMedium(medium))
        return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT total FROM tag_count WHERE medium = ? AND tag = ?"));
    q.addBindValue(medium);
    q.addBindValue(genre);
    return (q.exec() && q.next()) ? q.value(0).toInt() : 0;
}

QVariantList MalCatalog::genreCounts(const QString& medium) const
{
    QVariantList out;
    if (!m_ok || !validMedium(medium))
        return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT tag, total FROM tag_count WHERE medium = ? ORDER BY total DESC"));
    q.addBindValue(medium);
    if (!q.exec())
        return out;
    while (q.next())
        out.append(QVariantMap{{QStringLiteral("name"), q.value(0).toString()},
                               {QStringLiteral("count"), q.value(1).toInt()}});
    return out;
}
