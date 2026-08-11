// MalCatalog.cpp — see header. [Agent 0 (Claude), shell]
#include "MalCatalog.h"

#include "VaultKit.h"

#include <QCoreApplication>
#include <QDate>
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

// db-stored JSON array of strings -> flat QVariantList<QString> (discover rows'
// `classifications`, consumed by the Tankoban Discover adapter, not the card mapper)
QVariantList stringList(const QString& json) {
    QVariantList out;
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto& v : arr)
        out.append(v.toString());
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

QVariantList MalCatalog::matchByTitle(const QString& title, int year,
                                      const QString& medium) const
{
    QVariantList out;
    if (!m_ok)
        return out;
    if (!medium.isEmpty() && !validMedium(medium))
        return out;

    const QString norm = VaultKit::normalizedTitle(title);
    if (norm.isEmpty())
        return out;

    const QStringList tables = medium.isEmpty()
        ? QStringList{QStringLiteral("anime"), QStringLiteral("manga")}
        : QStringList{medium};
    for (const QString& table : tables) {
        const bool anime = table == QStringLiteral("anime");
        const QString volumeOrEpisode = anime
            ? QStringLiteral("r.episodes, 0, 0")
            : QStringLiteral("0, r.volumes, r.chapters");
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT r.mal_id, r.title, r.title_english, r.type, r.score, "
            "r.year, r.cover, r.synopsis, r.credits, r.tags, %1 "
            "FROM %2 r WHERE (r.norm_title = ? OR r.norm_title_english = ?)"
            " AND (? = 0 OR r.year = ?)"
            " ORDER BY r.year DESC, r.score DESC, r.mal_id ASC").arg(volumeOrEpisode, table));
        q.addBindValue(norm);
        q.addBindValue(norm);
        q.addBindValue(year);
        q.addBindValue(year);
        if (!q.exec())
            continue;
        while (q.next()) {
            QVariantMap m;
            m.insert(QStringLiteral("mal_id"), q.value(0).toInt());
            m.insert(QStringLiteral("title"), q.value(1).toString());
            const QString english = q.value(2).toString();
            if (!english.isEmpty())
                m.insert(QStringLiteral("title_english"), english);
            m.insert(QStringLiteral("type"), q.value(3).toString());
            if (!q.value(4).isNull())
                m.insert(QStringLiteral("score"), q.value(4).toDouble());
            const int resultYear = q.value(5).toInt();
            if (resultYear > 0)
                m.insert(QStringLiteral("year"), resultYear);
            m.insert(QStringLiteral("coverUrl"), q.value(6).toString());
            m.insert(QStringLiteral("synopsis"), q.value(7).toString());
            m.insert(QStringLiteral("credits"), namedList(q.value(8).toString()));
            m.insert(QStringLiteral("genres"), namedList(q.value(9).toString()));
            m.insert(QStringLiteral("medium"), table);
            if (anime) {
                m.insert(QStringLiteral("episodes"), q.value(10).toInt());
            } else {
                m.insert(QStringLiteral("volumes"), q.value(11).toInt());
                m.insert(QStringLiteral("chapters"), q.value(12).toInt());
            }
            out.append(m);
        }
    }
    return out;
}

QVariantList MalCatalog::search(const QString& text, int limit, const QString& medium) const
{
    QVariantList out;
    if (!m_ok)
        return out;
    if (!medium.isEmpty() && !validMedium(medium))
        return out;

    const QString norm = VaultKit::normalizedTitle(text);
    if (norm.isEmpty())
        return out;
    const QStringList tables = medium.isEmpty()
        ? QStringList{QStringLiteral("anime"), QStringLiteral("manga")}
        : QStringList{medium};
    const int capped = std::clamp(limit, 1, 100);
    for (const QString& table : tables) {
        const bool anime = table == QStringLiteral("anime");
        const QString volumeOrEpisode = anime
            ? QStringLiteral("r.episodes, 0, 0")
            : QStringLiteral("0, r.volumes, r.chapters");
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT r.mal_id, r.title, r.title_english, r.type, r.score, "
            "r.year, r.cover, r.synopsis, r.credits, r.tags, %1 "
            "FROM %2 r WHERE r.norm_title LIKE ? OR r.norm_title_english LIKE ? "
            "ORDER BY CASE WHEN r.norm_title = ? THEN 0 ELSE 1 END, "
            "r.year DESC, r.score DESC, r.mal_id ASC LIMIT ?").arg(volumeOrEpisode, table));
        const QString prefix = norm + QLatin1Char('%');
        q.addBindValue(prefix);
        q.addBindValue(prefix);
        q.addBindValue(norm);
        q.addBindValue(capped);
        if (!q.exec())
            continue;
        while (q.next()) {
            QVariantMap m;
            m.insert(QStringLiteral("mal_id"), q.value(0).toInt());
            m.insert(QStringLiteral("title"), q.value(1).toString());
            const QString english = q.value(2).toString();
            if (!english.isEmpty())
                m.insert(QStringLiteral("title_english"), english);
            m.insert(QStringLiteral("type"), q.value(3).toString());
            if (!q.value(4).isNull())
                m.insert(QStringLiteral("score"), q.value(4).toDouble());
            const int resultYear = q.value(5).toInt();
            if (resultYear > 0)
                m.insert(QStringLiteral("year"), resultYear);
            m.insert(QStringLiteral("coverUrl"), q.value(6).toString());
            m.insert(QStringLiteral("synopsis"), q.value(7).toString());
            m.insert(QStringLiteral("credits"), namedList(q.value(8).toString()));
            m.insert(QStringLiteral("genres"), namedList(q.value(9).toString()));
            m.insert(QStringLiteral("medium"), table);
            if (anime)
                m.insert(QStringLiteral("episodes"), q.value(10).toInt());
            else {
                m.insert(QStringLiteral("volumes"), q.value(11).toInt());
                m.insert(QStringLiteral("chapters"), q.value(12).toInt());
            }
            out.append(m);
        }
    }
    return out;
}

QVariantList MalCatalog::discoverFilters(const QString& axis, bool includeExplicit) const
{
    QVariantList out;
    // Only the two browsable axes; empty/unknown returns no facets.
    if (!m_ok || !(axis == QStringLiteral("genre") || axis == QStringLiteral("demographic")))
        return out;

    // Facets reflect the browsable manga slice. The JOIN to manga both scopes the
    // count to baked rows and lets includeExplicit=false prune explicit titles —
    // which also drops facets (e.g. Hentai) that exist ONLY on explicit titles.
    QString sql = QStringLiteral(
        "SELECT c.value, COUNT(*) AS total "
        "FROM classification c JOIN manga m ON m.mal_id = c.mal_id "
        "WHERE c.medium = 'manga' AND c.axis = ?");
    if (!includeExplicit)
        sql += QStringLiteral(" AND m.explicit = 0");
    sql += QStringLiteral(" GROUP BY c.value ORDER BY total DESC, c.value ASC");

    QSqlQuery q(m_db);
    q.prepare(sql);
    q.addBindValue(axis);
    if (!q.exec())
        return out;
    while (q.next())
        out.append(QVariantMap{{QStringLiteral("value"), q.value(0).toString()},
                               {QStringLiteral("count"), q.value(1).toInt()}});
    return out;
}

QVariantMap MalCatalog::discoverPage(const QString& catalogId, const QString& filterAxis,
                                     const QString& filterKey, bool includeExplicit,
                                     int offset, int limit) const
{
    // Deterministic vote-confidence floor for the Top Rated Bayesian weight (m in
    // WR = (v/(v+m))*R + (m/(v+m))*C). A few thousand votes is the confidence bar:
    // a 9.9 from ~120 voters is pulled hard toward the catalogue mean C and cannot
    // outrank a broadly-established title, while a genuine high-vote 9.x survives.
    constexpr double kVoteConfidence = 3000.0;

    const int lim = std::clamp(limit, 1, 100);
    const int off = std::max(0, offset);
    auto pack = [&](const QVariantList& items) {
        return QVariantMap{
            {QStringLiteral("items"), items},
            {QStringLiteral("nextOffset"), off + static_cast<int>(items.size())},
            {QStringLiteral("exhausted"), static_cast<int>(items.size()) < lim},
            {QStringLiteral("freshness"), QStringLiteral("bundled")},
            {QStringLiteral("fallbackCatalog"),
             catalogId == QStringLiteral("trending") ? QStringLiteral("popular") : QString()}};
    };

    if (!m_ok)
        return pack({});

    // Allowlist the catalogue id and the filter axis — never interpolate caller text.
    static const QSet<QString> catalogs = {
        QStringLiteral("popular"), QStringLiteral("top-rated"),
        QStringLiteral("new-releases"), QStringLiteral("trending")};
    if (!catalogs.contains(catalogId))
        return pack({});
    if (!(filterAxis.isEmpty() || filterAxis == QStringLiteral("genre")
          || filterAxis == QStringLiteral("demographic")))
        return pack({});

    // FROM (+ optional classification join for the facet filter; value is BOUND).
    const bool joinClass =
        (filterAxis == QStringLiteral("genre") || filterAxis == QStringLiteral("demographic"))
        && !filterKey.isEmpty();
    QString from = QStringLiteral("manga m");
    QStringList where;
    QVariantList binds;
    if (joinClass) {
        from = QStringLiteral("classification c JOIN manga m ON m.mal_id = c.mal_id");
        // Case-insensitive value match: the bake stores classification values in their
        // canonical Titlecase ("Romance", "Seinen"), but the Tankoban adapter derives a
        // STABLE lower-case filter key ("romance"). Compare LOWER-to-LOWER so the stable
        // key resolves — an exact `c.value = ?` silently returned zero rows for every
        // filter (2026-08-02: the Discover Romance filter painted an empty wall).
        where << QStringLiteral("c.medium = 'manga' AND c.axis = ? AND LOWER(c.value) = LOWER(?)");
        binds << filterAxis << filterKey;
    }
    if (!includeExplicit)
        where << QStringLiteral("m.explicit = 0");

    // Catalogue -> a fixed ORDER BY fragment. Missing values stay neutral: undated /
    // unscored rows are excluded from date / rating catalogues, not floored to zero.
    QString orderSql;
    QVariantList orderBinds;
    const bool trending = catalogId == QStringLiteral("trending");
    if (catalogId == QStringLiteral("popular") || trending) {
        // Trending has no comparable snapshot yet -> Popular order + fallbackCatalog:popular.
        orderSql = QStringLiteral("m.members DESC, COALESCE(m.score, 0) DESC, m.mal_id ASC");
    } else if (catalogId == QStringLiteral("new-releases")) {
        // newest real publication first; reject empty / malformed / future dates.
        where << QStringLiteral("date(m.start_date) IS NOT NULL AND date(m.start_date) <= date(?)");
        binds << QDate::currentDate().toString(Qt::ISODate);
        orderSql = QStringLiteral("m.start_date DESC, m.mal_id DESC");
    } else { // top-rated
        where << QStringLiteral("m.score IS NOT NULL");
        // Catalogue mean C over the SAME scoped population, computed before the page.
        double meanScore = 0.0;
        {
            QString cSql = QStringLiteral("SELECT AVG(m.score) FROM ") + from;
            if (!where.isEmpty())
                cSql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
            QSqlQuery cq(m_db);
            cq.prepare(cSql);
            for (const QVariant& b : binds)
                cq.addBindValue(b);
            if (cq.exec() && cq.next() && !cq.value(0).isNull())
                meanScore = cq.value(0).toDouble();
        }
        orderSql = QStringLiteral(
            "((CAST(m.scored_by AS REAL) / (m.scored_by + ?)) * m.score "
            "+ (? / (CAST(m.scored_by AS REAL) + ?)) * ?) DESC, m.mal_id ASC");
        orderBinds << kVoteConfidence << kVoteConfidence << kVoteConfidence << meanScore;
    }

    QString sql = QStringLiteral(
        "SELECT m.mal_id, m.title, m.title_english, m.type, m.score, m.scored_by, "
        "m.members, m.favorites, m.year, m.start_date, m.cover, m.tags, m.explicit FROM ") + from;
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY ") + orderSql + QStringLiteral(" LIMIT ? OFFSET ?");

    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant& b : binds)       q.addBindValue(b);   // WHERE binds, in text order
    for (const QVariant& b : orderBinds)  q.addBindValue(b);   // ORDER BY binds (top-rated)
    q.addBindValue(lim);
    q.addBindValue(off);
    if (!q.exec())
        return pack({});

    QVariantList rows;
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
        m.insert(QStringLiteral("favorites"), q.value(7).toInt());
        const int year = q.value(8).toInt();
        if (year > 0) m.insert(QStringLiteral("year"), year);
        m.insert(QStringLiteral("start_date"), q.value(9).toString());
        m.insert(QStringLiteral("cover"), q.value(10).toString());
        m.insert(QStringLiteral("classifications"), stringList(q.value(11).toString()));
        m.insert(QStringLiteral("explicit"), q.value(12).toInt() != 0);
        m.insert(QStringLiteral("availability"), false);   // the adapter enriches this later
        rows.append(m);
    }
    return pack(rows);
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
