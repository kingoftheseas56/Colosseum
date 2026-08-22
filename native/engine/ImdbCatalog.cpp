// ImdbCatalog.cpp — see header.
#include "ImdbCatalog.h"

#include "VaultKit.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>

#include <algorithm>

bool ImdbCatalog::openAt(const QString& dbPath)
{
    QString path = dbPath;
    if (!QFileInfo::exists(path)) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + dbPath;
        if (QFileInfo::exists(beside)) path = beside;
    }
    if (!QFileInfo::exists(path))
        return false;                    // shelves omit honestly without the index
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    return m_db.open();
}

ImdbCatalog::ImdbCatalog(const QString& dbPath, QObject* parent)
    : QObject(parent), m_conn(QStringLiteral("imdb_catalog"))
{
    m_ok = openAt(dbPath);
}

ImdbCatalog::~ImdbCatalog()
{
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
}

bool ImdbCatalog::reopen(const QString& dbPath)
{
    const bool wasOk = m_ok;
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
    m_ok = openAt(dbPath);
    if (m_ok != wasOk || m_ok)
        emit readyChanged();
    return m_ok;
}

void ImdbCatalog::closeForSwap()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
    const bool wasOk = m_ok;
    m_ok = false;
    if (wasOk)
        emit readyChanged();
}

QVariantList ImdbCatalog::titleCatalog(const QVariantMap& query, int offset, int limit) const
{
    QVariantList out;
    if (!m_ok) return out;

    static const QSet<QString> allowed = {
        QStringLiteral("type"), QStringLiteral("order"), QStringLiteral("ratingMin"),
        QStringLiteral("votesMin"), QStringLiteral("votesMax"), QStringLiteral("yearFrom"),
        QStringLiteral("yearTo"), QStringLiteral("runtimeMax"), QStringLiteral("genre"),
        QStringLiteral("lang"), QStringLiteral("notLang"), QStringLiteral("excludeAnime"),
        QStringLiteral("episodesMin"), QStringLiteral("notGenre")
    };
    for (auto it = query.constBegin(); it != query.constEnd(); ++it)
        if (!allowed.contains(it.key())) return out;

    const QString order = query.value(QStringLiteral("order")).toString();
    QString orderSql;
    if (order.isEmpty() || order == QStringLiteral("votes")) orderSql = QStringLiteral("t.votes DESC");
    else if (order == QStringLiteral("rating"))   orderSql = QStringLiteral("t.rating DESC, t.votes DESC");
    else if (order == QStringLiteral("year"))     orderSql = QStringLiteral("t.year DESC, t.votes DESC");
    else if (order == QStringLiteral("episodes")) orderSql = QStringLiteral("t.episodes DESC, t.votes DESC");
    else return out;

    QStringList where;
    QVariantList binds;
    const QString type = query.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("movie"))       { where << QStringLiteral("t.type = 'movie'"); }
    else if (type == QStringLiteral("series")) { where << QStringLiteral("t.type IN ('series','mini')"); }
    else if (type == QStringLiteral("mini"))   { where << QStringLiteral("t.type = 'mini'"); }
    else if (!type.isEmpty())                  { return out; }
    const bool joinGenre = query.contains(QStringLiteral("genre"));
    if (joinGenre) { where << QStringLiteral("g.genre = ?");
                     binds << query.value(QStringLiteral("genre")).toString(); }
    if (query.contains(QStringLiteral("ratingMin"))) { where << QStringLiteral("t.rating >= ?");
                     binds << query.value(QStringLiteral("ratingMin")).toDouble(); }
    if (query.contains(QStringLiteral("votesMin")))  { where << QStringLiteral("t.votes >= ?");
                     binds << query.value(QStringLiteral("votesMin")).toInt(); }
    if (query.contains(QStringLiteral("votesMax")))  { where << QStringLiteral("t.votes <= ?");
                     binds << query.value(QStringLiteral("votesMax")).toInt(); }
    if (query.contains(QStringLiteral("yearFrom")))  { where << QStringLiteral("t.year >= ?");
                     binds << query.value(QStringLiteral("yearFrom")).toInt(); }
    if (query.contains(QStringLiteral("yearTo")))    { where << QStringLiteral("t.year <= ? AND t.year > 0");
                     binds << query.value(QStringLiteral("yearTo")).toInt(); }
    if (query.contains(QStringLiteral("runtimeMax"))){ where << QStringLiteral("t.runtimeMin <= ? AND t.runtimeMin > 0");
                     binds << query.value(QStringLiteral("runtimeMax")).toInt(); }
    if (query.contains(QStringLiteral("lang")))      { where << QStringLiteral("t.origLang = ?");
                     binds << query.value(QStringLiteral("lang")).toString(); }
    if (query.contains(QStringLiteral("notLang")))   { where << QStringLiteral("t.origLang != ? AND t.origLang != ''");
                     binds << query.value(QStringLiteral("notLang")).toString(); }
    if (query.value(QStringLiteral("excludeAnime")).toBool())
        where << QStringLiteral("t.isAnime = 0");
    if (query.contains(QStringLiteral("episodesMin"))){ where << QStringLiteral("t.episodes >= ?");
                     binds << query.value(QStringLiteral("episodesMin")).toInt(); }
    // notGenre (list): exclude titles carrying ANY of the named genres — kept live-action shelves
    // free of animation/reality/game-show rows that a language guess can't. Every value bound.
    if (query.contains(QStringLiteral("notGenre"))) {
        const QVariantList ng = query.value(QStringLiteral("notGenre")).toList();
        if (!ng.isEmpty()) {
            QStringList marks;
            for (int i = 0; i < ng.size(); ++i) marks << QStringLiteral("?");
            where << (QStringLiteral("t.tt NOT IN (SELECT tt FROM genre WHERE genre IN (")
                      + marks.join(QStringLiteral(",")) + QStringLiteral("))"));
            for (const QVariant& g : ng) binds << g.toString();
        }
    }

    QString sql = QStringLiteral(
        "SELECT t.tt, t.type, t.title, t.year, t.endYear, t.runtimeMin, t.genres, "
        "t.rating, t.votes, t.episodes, t.origLang, t.isAnime FROM ");
    sql += joinGenre ? QStringLiteral("genre g JOIN title t ON t.tt = g.tt")
                     : QStringLiteral("title t");
    if (!where.isEmpty()) sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY ") + orderSql + QStringLiteral(" LIMIT ? OFFSET ?");

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant& b : binds) q.addBindValue(b);
    q.addBindValue(std::clamp(limit, 1, 100));
    q.addBindValue(std::max(0, offset));
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("tt"), q.value(0).toString());
        m.insert(QStringLiteral("type"), q.value(1).toString());
        m.insert(QStringLiteral("title"), q.value(2).toString());
        m.insert(QStringLiteral("year"), q.value(3).toInt());
        m.insert(QStringLiteral("endYear"), q.value(4).toInt());
        m.insert(QStringLiteral("runtimeMin"), q.value(5).toInt());
        QVariantList genres;
        const QJsonArray arr = QJsonDocument::fromJson(q.value(6).toString().toUtf8()).array();
        for (const auto& v : arr) genres.append(v.toString());
        m.insert(QStringLiteral("genres"), genres);
        m.insert(QStringLiteral("rating"), q.value(7).toDouble());
        m.insert(QStringLiteral("votes"), q.value(8).toInt());
        m.insert(QStringLiteral("episodes"), q.value(9).toInt());
        m.insert(QStringLiteral("origLang"), q.value(10).toString());
        m.insert(QStringLiteral("isAnime"), q.value(11).toInt() != 0);
        out.append(m);
    }
    return out;
}

QVariantList ImdbCatalog::matchByTitle(const QString& title, int year) const
{
    QVariantList out;
    if (!m_ok)
        return out;

    const QString norm = VaultKit::normalizedTitle(title);
    if (norm.isEmpty())
        return out;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT tt, type, title, year, endYear, runtimeMin, genres, rating, votes, "
        "episodes, origLang, isAnime FROM title "
        "WHERE norm_title = ? AND (? = 0 OR year = ?) "
        "ORDER BY year DESC, rating DESC, votes DESC, tt ASC"));
    q.addBindValue(norm);
    q.addBindValue(year);
    q.addBindValue(year);
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("tt"), q.value(0).toString());
        m.insert(QStringLiteral("type"), q.value(1).toString());
        m.insert(QStringLiteral("title"), q.value(2).toString());
        m.insert(QStringLiteral("year"), q.value(3).toInt());
        m.insert(QStringLiteral("endYear"), q.value(4).toInt());
        m.insert(QStringLiteral("runtimeMin"), q.value(5).toInt());
        QVariantList genres;
        const QJsonArray arr = QJsonDocument::fromJson(q.value(6).toString().toUtf8()).array();
        for (const auto& v : arr)
            genres.append(v.toString());
        m.insert(QStringLiteral("genres"), genres);
        m.insert(QStringLiteral("rating"), q.value(7).toDouble());
        m.insert(QStringLiteral("votes"), q.value(8).toInt());
        m.insert(QStringLiteral("episodes"), q.value(9).toInt());
        m.insert(QStringLiteral("origLang"), q.value(10).toString());
        m.insert(QStringLiteral("isAnime"), q.value(11).toInt() != 0);
        // IMDb's public title/ratings/basics bake contains no synopsis field.
        m.insert(QStringLiteral("synopsis"), QString());
        out.append(m);
    }
    return out;
}

QVariantList ImdbCatalog::search(const QString& text, int limit) const
{
    QVariantList out;
    if (!m_ok)
        return out;
    const QString norm = VaultKit::normalizedTitle(text);
    if (norm.isEmpty())
        return out;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT tt, type, title, year, endYear, runtimeMin, genres, rating, votes, "
        "episodes, origLang, isAnime FROM title "
        "WHERE norm_title LIKE ? ORDER BY CASE WHEN norm_title = ? THEN 0 ELSE 1 END, "
        "year DESC, rating DESC, votes DESC, tt ASC LIMIT ?"));
    q.addBindValue(norm + QLatin1Char('%'));
    q.addBindValue(norm);
    q.addBindValue(std::clamp(limit, 1, 100));
    if (!q.exec())
        return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("tt"), q.value(0).toString());
        m.insert(QStringLiteral("type"), q.value(1).toString());
        m.insert(QStringLiteral("title"), q.value(2).toString());
        m.insert(QStringLiteral("year"), q.value(3).toInt());
        m.insert(QStringLiteral("endYear"), q.value(4).toInt());
        m.insert(QStringLiteral("runtimeMin"), q.value(5).toInt());
        QVariantList genres;
        const QJsonArray arr = QJsonDocument::fromJson(q.value(6).toString().toUtf8()).array();
        for (const auto& v : arr)
            genres.append(v.toString());
        m.insert(QStringLiteral("genres"), genres);
        m.insert(QStringLiteral("rating"), q.value(7).toDouble());
        m.insert(QStringLiteral("votes"), q.value(8).toInt());
        m.insert(QStringLiteral("episodes"), q.value(9).toInt());
        m.insert(QStringLiteral("origLang"), q.value(10).toString());
        m.insert(QStringLiteral("isAnime"), q.value(11).toInt() != 0);
        m.insert(QStringLiteral("synopsis"), QString());
        out.append(m);
    }
    return out;
}

QVariantMap ImdbCatalog::titleFacts(const QStringList& ids) const
{
    QVariantMap out;
    if (!m_ok || ids.isEmpty()) return out;
    for (int start = 0; start < ids.size(); start += 100) {
        const QStringList chunk = ids.mid(start, 100);
        QStringList marks;
        for (int i = 0; i < chunk.size(); ++i) marks << QStringLiteral("?");
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT tt, rating, votes, isAnime FROM title WHERE tt IN (")
                  + marks.join(QStringLiteral(",")) + QStringLiteral(")"));
        for (const QString& id : chunk) q.addBindValue(id);
        if (!q.exec()) continue;
        while (q.next())
            out.insert(q.value(0).toString(), QVariantMap{
                {QStringLiteral("rating"), q.value(1).toDouble()},
                {QStringLiteral("votes"), q.value(2).toInt()},
                {QStringLiteral("isAnime"), q.value(3).toInt() != 0}});
    }
    return out;
}
