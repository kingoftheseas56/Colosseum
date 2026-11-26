// ImdbCatalog.cpp — see header.
#include "ImdbCatalog.h"

#include "MediaTitleNormalization.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QSqlQuery>

#include <algorithm>

namespace {

// Single canonical description for remote read, state inspection, and retry.
// No normalization: the accepted owner hashes exactly these three arguments.
struct RemoteTitleCatalog {
    QString method;
    QVariantList args;
    QVariantList empty;
};

RemoteTitleCatalog remoteTitleCatalog(const QVariantMap& query, int offset, int limit)
{
    return {QStringLiteral("titleCatalog"),
            QVariantList{query, offset, limit},
            QVariantList{}};
}

// Same canonical triple for the facts read, its state inspection, and its
// retry. The ordered id vector is hashed verbatim: order, duplicates, and the
// empty vector each form their own exact key. No filtering or normalization.
struct RemoteTitleFacts {
    QString method;
    QVariantList args;
    QVariantMap empty;
};

RemoteTitleFacts remoteTitleFacts(const QStringList& ids)
{
    return {QStringLiteral("titleFacts"),
            QVariantList{QVariant::fromValue(ids)},
            QVariantMap{}};
}

} // namespace

bool ImdbCatalog::openAt(const QString& dbPath)
{
    if (dbPath.isEmpty()) return false;
    QString path = dbPath;
    if (!QFileInfo(path).isFile()) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + dbPath;
        if (QFileInfo(beside).isFile()) path = beside;
    }
    if (!QFileInfo(path).isFile())
        return false;                    // shelves omit honestly without the index
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    return m_db.open();
}

ImdbCatalog::ImdbCatalog(const QString& dbPath, QObject* parent)
    : QObject(parent), m_conn(QStringLiteral("imdb_catalog"))
{
    m_remote = new CatalogRemote(QStringLiteral("imdb"), this);
    connect(m_remote, &CatalogRemote::changed, this, &ImdbCatalog::revisionChanged);
    connect(m_remote, &CatalogRemote::failed, this, &ImdbCatalog::requestFailed);
    connect(m_remote, &CatalogRemote::queryChanged, this, &ImdbCatalog::remoteQueryChanged);
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

QVariantMap ImdbCatalog::titleCatalogState(const QVariantMap& query, int offset, int limit) const
{
    if (m_ok) {
        // Local SQLite is authoritative for this route. Do not make consumers
        // wait for a remote query key that read() will never queue.
        return {{QStringLiteral("source"), QStringLiteral("local")},
                {QStringLiteral("key"), QString()},
                {QStringLiteral("enabled"), false},
                {QStringLiteral("status"), QStringLiteral("success")},
                {QStringLiteral("pending"), false},
                {QStringLiteral("waiting"), false},
                {QStringLiteral("cached"), false},
                {QStringLiteral("error"), QString()},
                {QStringLiteral("retryable"), false}};
    }
    const auto request = remoteTitleCatalog(query, offset, limit);
    auto state = m_remote->queryState(request.method, request.args, request.empty);
    state.insert(QStringLiteral("source"), QStringLiteral("remote"));
    return state;
}

bool ImdbCatalog::retryTitleCatalog(const QVariantMap& query, int offset, int limit)
{
    if (m_ok) return false;
    const auto request = remoteTitleCatalog(query, offset, limit);
    return m_remote->retry(request.method, request.args, request.empty);
}

QVariantList ImdbCatalog::titleCatalog(const QVariantMap& query, int offset, int limit) const
{
    if (!m_ok) {
        const auto request = remoteTitleCatalog(query, offset, limit);
        return m_remote->read(request.method, request.args, request.empty).toList();
    }
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

namespace {

QVariantMap titleRowToMap(QSqlQuery& q)
{
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
    return m;
}

const QStringList& romanNumerals()
{
    static const QStringList romans = {QStringLiteral("i"), QStringLiteral("ii"), QStringLiteral("iii"),
                                       QStringLiteral("iv"), QStringLiteral("v"), QStringLiteral("vi"),
                                       QStringLiteral("vii"), QStringLiteral("viii"), QStringLiteral("ix"),
                                       QStringLiteral("x")};
    return romans;
}

// Numerals 1-10 written the other way round in a trailing position or after "part": "godfather
// part 3" <-> "godfather part iii". A bare trailing "i" is an ordinary word, so it only converts
// after "part". Empty when the title has no such numeral. Query side only; the catalogue is
// never rewritten.
QString numeralVariantImpl(const QString& norm)
{
    QStringList tokens = norm.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.size() < 2)
        return {};
    QList<int> positions;
    positions.append(tokens.size() - 1);
    for (int i = 1; i + 1 < tokens.size(); ++i)
        if (tokens.at(i) == QLatin1String("part"))
            positions.append(i + 1);
    for (const int pos : positions) {
        const QString token = tokens.at(pos);
        const bool afterPart = tokens.at(pos - 1) == QLatin1String("part");
        bool isNumber = false;
        const int number = token.toInt(&isNumber);
        if (isNumber && token == QString::number(number) && number >= 1 && number <= 10) {
            tokens[pos] = romanNumerals().at(number - 1);
            return tokens.join(QLatin1Char(' '));
        }
        const int roman = romanNumerals().indexOf(token) + 1;
        if (roman > 0 && (roman > 1 || afterPart)) {
            tokens[pos] = QString::number(roman);
            return tokens.join(QLatin1Char(' '));
        }
    }
    return {};
}

// The catalogue title's text before its first ':' (or before " or:"), normalized the same way as
// the query. "Rogue One: A Star Wars Story" -> "rogue one"; "Dr. Strangelove or: How I ..." ->
// "dr strangelove".
QStringList subtitleHeadsImpl(const QString& title)
{
    const int colon = title.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return {};
    const QString head = title.left(colon).trimmed();
    QStringList out{MediaTitleNormalization::normalizedTitle(head)};
    static const QRegularExpression trailingOr(QStringLiteral("(?i)\\s+or$"));
    if (trailingOr.match(head).hasMatch())
        out.append(MediaTitleNormalization::normalizedTitle(
            head.left(head.size() - trailingOr.match(head).capturedLength())));
    return out;
}

} // namespace

QString ImdbCatalog::numeralVariant(const QString& normalizedTitle)
{
    return numeralVariantImpl(normalizedTitle);
}

QStringList ImdbCatalog::subtitleHeads(const QString& catalogueTitle)
{
    return subtitleHeadsImpl(catalogueTitle);
}

// The exact and prefix reads, local or remote. One flow (matchByTitle below) uses these for both,
// so a build with a local db and a build behind the content API behave the same.
QVariant ImdbCatalog::remoteRead(const QString& method, const QVariantList& args) const
{
    if (m_remoteReaderForTest)
        return m_remoteReaderForTest(method, args);
    return m_remote->read(method, args, QVariantList{});
}

QVariantList ImdbCatalog::exactMatches(const QString& key, int year) const
{
    if (!m_ok)
        return remoteRead(QStringLiteral("matchByTitle"), QVariantList{key, year}).toList();
    QVariantList rows;
    static const QString columns = QStringLiteral(
        "SELECT tt, type, title, year, endYear, runtimeMin, genres, rating, votes, "
        "episodes, origLang, isAnime FROM title ");
    const bool aliases = m_db.tables().contains(QStringLiteral("title_alias"));
    const QString titlePredicate = aliases
        ? QStringLiteral("(norm_title = ? OR EXISTS (SELECT 1 FROM title_alias a WHERE a.tt = title.tt AND a.norm_title = ?))")
        : QStringLiteral("norm_title = ?");
    QSqlQuery q(m_db);
    q.prepare(columns + QStringLiteral(
        "WHERE %1 AND (? = 0 OR year = ?) "
        "ORDER BY year DESC, rating DESC, votes DESC, tt ASC").arg(titlePredicate));
    q.addBindValue(key);
    if (aliases) q.addBindValue(key);
    q.addBindValue(year);
    q.addBindValue(year);
    if (!q.exec())
        return rows;
    while (q.next())
        rows.append(titleRowToMap(q));
    return rows;
}

// Titles whose normalized form starts with `key`, in the same order and with the same limit as the
// content API's `search` method (exact first, then newest). Local rides the norm_title index with
// a range scan instead of LIKE.
QVariantList ImdbCatalog::prefixMatches(const QString& key, int limit) const
{
    if (!m_ok)
        return remoteRead(QStringLiteral("search"), QVariantList{key, limit}).toList();
    QVariantList rows;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT tt, type, title, year, endYear, runtimeMin, genres, rating, votes, "
        "episodes, origLang, isAnime FROM title "
        "WHERE norm_title >= ? AND norm_title < ? "
        "ORDER BY CASE WHEN norm_title = ? THEN 0 ELSE 1 END, year DESC, rating DESC, votes DESC, tt ASC LIMIT ?"));
    q.addBindValue(key);
    q.addBindValue(key + QChar(0xFFFF));
    q.addBindValue(key);
    q.addBindValue(limit);
    if (!q.exec())
        return rows;
    while (q.next())
        rows.append(titleRowToMap(q));
    return rows;
}

QVariantList ImdbCatalog::matchByTitle(const QString& title, int year) const
{
    QVariantList out;
    const QString norm = MediaTitleNormalization::normalizedTitle(title);
    if (norm.isEmpty())
        return out;
    // A remote answer still in flight is "not yet", never "zero": stop and let the caller retry
    // when the revision changes, instead of firing the later steps speculatively.
    auto waiting = [this] { return !m_ok && !m_remoteReaderForTest && m_remote->waiting(); };

    out = exactMatches(norm, year);
    if (!out.isEmpty() || waiting())
        return out;

    // 3 <-> III: one more exact query with the numeral written the other way.
    const QString variant = numeralVariantImpl(norm);
    if (!variant.isEmpty()) {
        out = exactMatches(variant, year);
        if (!out.isEmpty() || waiting())
            return out;
    }

    // Exact subtitle match. Only with a parsed year, only to a catalogue title whose part before
    // the first ':' (or ' or:') equals the query, only with the same year, and only when exactly
    // one title qualifies. A prefix answer that fills the limit is not provable and counts as zero.
    if (year <= 0)
        return out;
    constexpr int kPrefixLimit = 50;
    QStringList keys{norm};
    if (!variant.isEmpty())
        keys.append(variant);
    QVariantList qualifying;
    QSet<QString> seen;
    for (const QString& key : keys) {
        const QVariantList rows = prefixMatches(key, kPrefixLimit);
        if (waiting())
            return {};
        if (rows.size() >= kPrefixLimit)
            continue;
        for (const QVariant& value : rows) {
            const QVariantMap row = value.toMap();
            const QString tt = row.value(QStringLiteral("tt")).toString();
            if (tt.isEmpty() || seen.contains(tt) || row.value(QStringLiteral("year")).toInt() != year
                || !subtitleHeadsImpl(row.value(QStringLiteral("title")).toString()).contains(key))
                continue;
            seen.insert(tt);
            QVariantMap copy = row;
            if (!copy.contains(QStringLiteral("synopsis")))
                copy.insert(QStringLiteral("synopsis"), QString());
            qualifying.append(copy);
        }
    }
    if (qualifying.size() == 1)
        out = qualifying;
    return out;
}

QVariantMap ImdbCatalog::searchState(const QString& text, int limit) const
{
    if (m_ok) return {{"source", "local"}, {"key", QString()}, {"enabled", false},
        {"status", "success"}, {"pending", false}, {"waiting", false},
        {"cached", false}, {"error", QString()}, {"retryable", false}};
    auto state = m_remote->queryState(QStringLiteral("search"), QVariantList{MediaTitleNormalization::normalizedTitle(text), limit}, QVariantList{});
    state.insert(QStringLiteral("source"), QStringLiteral("remote"));
    return state;
}

QVariantList ImdbCatalog::search(const QString& text, int limit) const
{
    if (!m_ok) return m_remote->read(QStringLiteral("search"), QVariantList{MediaTitleNormalization::normalizedTitle(text), limit}, QVariantList{}).toList();
    QVariantList out;
    if (!m_ok)
        return out;
    const QString norm = MediaTitleNormalization::normalizedTitle(text);
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
    if (!m_ok) {
        const auto request = remoteTitleFacts(ids);
        return m_remote->read(request.method, request.args, request.empty).toMap();
    }
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

QVariantMap ImdbCatalog::titleFactsState(const QStringList& ids) const
{
    if (m_ok) {
        // Local SQLite is authoritative for this route. Do not make consumers
        // wait for a remote query key that read() will never queue.
        return {{QStringLiteral("source"), QStringLiteral("local")},
                {QStringLiteral("key"), QString()},
                {QStringLiteral("enabled"), false},
                {QStringLiteral("status"), QStringLiteral("success")},
                {QStringLiteral("pending"), false},
                {QStringLiteral("waiting"), false},
                {QStringLiteral("cached"), false},
                {QStringLiteral("error"), QString()},
                {QStringLiteral("retryable"), false}};
    }
    const auto request = remoteTitleFacts(ids);
    auto state = m_remote->queryState(request.method, request.args, request.empty);
    state.insert(QStringLiteral("source"), QStringLiteral("remote"));
    return state;
}

bool ImdbCatalog::retryTitleFacts(const QStringList& ids)
{
    if (m_ok) return false;
    const auto request = remoteTitleFacts(ids);
    return m_remote->retry(request.method, request.args, request.empty);
}

QVariantMap ImdbCatalog::titlePresentation(const QString& id) const
{
    if (!m_ok) return m_remote->read(QStringLiteral("titlePresentation"), QVariantList{id}, QVariantMap{}).toMap();
    if (!m_ok || !id.startsWith(QStringLiteral("tt"))) return {};
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT title, type FROM title WHERE tt = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next()) return {};
    return {{QStringLiteral("title"), q.value(0).toString()},
            {QStringLiteral("type"), q.value(1).toString()}};
}
