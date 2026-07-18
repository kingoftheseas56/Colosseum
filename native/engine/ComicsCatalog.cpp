#include "engine/ComicsCatalog.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <utility>
#include <vector>

ComicsCatalog::ComicsCatalog(const QString& dbPath, QObject* parent) : QObject(parent) {
    m_conn = QStringLiteral("comics_catalog_%1").arg(reinterpret_cast<quintptr>(this));
    if (!QFileInfo::exists(dbPath)) {
        qInfo("[comics-catalog] no db at %s — catalogue lane dormant", qUtf8Printable(dbPath));
        return;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(dbPath);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!m_db.open()) {
        qInfo("[comics-catalog] open failed: %s", qUtf8Printable(m_db.lastError().text()));
        return;
    }
    QSqlQuery probe(m_db);   // schema sanity: the three catalogue tables must exist
    m_ok = probe.exec(QStringLiteral(
        "select 1 from series limit 1"));
    if (!m_ok) qInfo("[comics-catalog] db present but series table missing — dormant");
    else qInfo("[comics-catalog] ready (%s)", qUtf8Printable(dbPath));
}

ComicsCatalog::~ComicsCatalog() {
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();                     // release handle before removal
    QSqlDatabase::removeDatabase(m_conn);
}

static QString likeEscape(const QString& s) {
    QString out = s;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('%'), QLatin1String("\\%"));
    out.replace(QLatin1Char('_'), QLatin1String("\\_"));
    return out;
}

QVariantMap ComicsCatalog::series(int gcdId) const {
    QVariantMap out;
    if (!m_ok) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select s.gcd_id, s.title, s.year, s.year_ended, s.issue_count, s.publisher,"
        "       s.cover, s.synopsis, coalesce(t.downloads,0), coalesce(t.kinds,''),"
        "       coalesce(t.latest_post,'')"
        " from series s left join series_stats t on t.series_id = s.gcd_id"
        " where s.gcd_id = :id"));
    q.bindValue(QStringLiteral(":id"), gcdId);
    if (!q.exec() || !q.next()) return out;
    out.insert(QStringLiteral("gcdId"), q.value(0).toInt());
    out.insert(QStringLiteral("title"), q.value(1).toString());
    out.insert(QStringLiteral("year"), q.value(2).toInt());
    out.insert(QStringLiteral("yearEnded"), q.value(3).toInt());
    out.insert(QStringLiteral("issueCount"), q.value(4).toInt());
    out.insert(QStringLiteral("publisher"), q.value(5).toString());
    out.insert(QStringLiteral("cover"), q.value(6).toString());
    out.insert(QStringLiteral("synopsis"), q.value(7).toString());
    out.insert(QStringLiteral("downloads"), q.value(8).toInt());
    out.insert(QStringLiteral("kinds"), q.value(9).toString());
    out.insert(QStringLiteral("latestPost"), q.value(10).toString());
    return out;
}

QVariantList ComicsCatalog::search(const QString& text, int limit) const {
    QVariantList out;
    const QString t = text.trimmed();
    if (!m_ok || t.isEmpty()) return out;
    // Word-based matching (spec 2026-07-18): every typed word must appear in the
    // title, any order. Ranking keeps the phrase tiers on top: exact title, then
    // titles starting with the full typed phrase, then any all-words match —
    // downloads DESC inside each tier (availability-first), year DESC last.
    static const QRegularExpression wordSplit(QStringLiteral("[^\\p{L}\\p{N}]+"));
    const QStringList words = t.split(wordSplit, Qt::SkipEmptyParts);
    if (words.isEmpty()) return out;
    QString sql = QStringLiteral(
        "select s.gcd_id, s.title, s.year, s.year_ended, s.publisher, s.cover,"
        "       coalesce(t.downloads,0) as dls"
        " from series s left join series_stats t on t.series_id = s.gcd_id"
        " where 1=1");
    for (int i = 0; i < words.size(); ++i)
        sql += QStringLiteral(" and s.title like :w%1 escape '\\'").arg(i);
    sql += QStringLiteral(
        " order by case when lower(s.title) = lower(:txt) then 0"
        "               when s.title like :prefix escape '\\' then 1 else 2 end,"
        "          dls desc, s.year desc"
        " limit :lim");
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (int i = 0; i < words.size(); ++i)
        q.bindValue(QStringLiteral(":w%1").arg(i),
                    QStringLiteral("%%%1%%").arg(likeEscape(words.at(i))));
    q.bindValue(QStringLiteral(":txt"), t);
    q.bindValue(QStringLiteral(":prefix"), likeEscape(t) + QStringLiteral("%"));
    q.bindValue(QStringLiteral(":lim"), limit);
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("gcdId"), q.value(0).toInt());
        m.insert(QStringLiteral("title"), q.value(1).toString());
        m.insert(QStringLiteral("year"), q.value(2).toInt());
        m.insert(QStringLiteral("yearEnded"), q.value(3).toInt());
        m.insert(QStringLiteral("publisher"), q.value(4).toString());
        m.insert(QStringLiteral("cover"), q.value(5).toString());
        m.insert(QStringLiteral("downloads"), q.value(6).toInt());
        out.append(m);
    }
    return out;
}

QVariantList ComicsCatalog::downloadsFor(int gcdId) const {
    QVariantList out;
    if (!m_ok) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select post_id, title, link, date, kind, method, fan_made, year_start"
        " from download where series_id = :id order by date desc, post_id desc"));
    q.bindValue(QStringLiteral(":id"), gcdId);
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("postId"), q.value(0).toInt());
        m.insert(QStringLiteral("title"), q.value(1).toString());
        m.insert(QStringLiteral("link"), q.value(2).toString());
        m.insert(QStringLiteral("date"), q.value(3).toString());
        m.insert(QStringLiteral("kind"), q.value(4).toString());
        m.insert(QStringLiteral("method"), q.value(5).toString());
        m.insert(QStringLiteral("fanMade"), q.value(6).toInt() != 0);
        m.insert(QStringLiteral("yearStart"), q.value(7).toInt());
        out.append(m);
    }
    return out;
}

QVariantList ComicsCatalog::exactMatches(const QString& text) const {
    QVariantList out;
    const QString t = text.trimmed();
    if (!m_ok || t.isEmpty()) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select s.gcd_id, s.title, s.year, s.year_ended, s.publisher, s.cover,"
        "       coalesce(t.downloads,0) as dls"
        " from series s left join series_stats t on t.series_id = s.gcd_id"
        " where lower(s.title) = lower(:txt) order by dls desc"));
    q.bindValue(QStringLiteral(":txt"), t);
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("gcdId"), q.value(0).toInt());
        m.insert(QStringLiteral("title"), q.value(1).toString());
        m.insert(QStringLiteral("year"), q.value(2).toInt());
        m.insert(QStringLiteral("yearEnded"), q.value(3).toInt());
        m.insert(QStringLiteral("publisher"), q.value(4).toString());
        m.insert(QStringLiteral("cover"), q.value(5).toString());
        m.insert(QStringLiteral("downloads"), q.value(6).toInt());
        out.append(m);
    }
    return out;
}

// --- curated catalog (locg_id-keyed) ---

bool ComicsCatalog::curatedReady() const {
    if (!m_ok) return false;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("select 1 from curated_series limit 1"))) return false;
    return q.next();
}

QVariantList ComicsCatalog::curatedRanked() const {
    QVariantList out;
    if (!curatedReady()) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select s.locg_id, s.rank, s.title, s.year, s.slug, s.publisher, s.cover,"
        "       coalesce((select group_concat(genre) from curated_genre g"
        "                 where g.locg_id = s.locg_id), '')"
        " from curated_series s order by s.rank asc"));
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("locgId"), q.value(0).toString());
        m.insert(QStringLiteral("rank"), q.value(1).toInt());
        m.insert(QStringLiteral("title"), q.value(2).toString());
        m.insert(QStringLiteral("year"), q.value(3).toInt());
        m.insert(QStringLiteral("slug"), q.value(4).toString());
        m.insert(QStringLiteral("publisher"), q.value(5).toString());
        m.insert(QStringLiteral("cover"), q.value(6).toString());
        m.insert(QStringLiteral("genres"), q.value(7).toString());
        out.append(m);
    }
    return out;
}

QVariantMap ComicsCatalog::curatedSeries(const QString& locgId) const {
    QVariantMap out;
    if (!curatedReady() || locgId.isEmpty()) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select s.locg_id, s.rank, s.title, s.year, s.slug, s.publisher, s.cover, s.synopsis,"
        "       coalesce((select group_concat(genre) from curated_genre g"
        "                 where g.locg_id = s.locg_id), '')"
        " from curated_series s where s.locg_id = :id"));
    q.bindValue(QStringLiteral(":id"), locgId);
    if (!q.exec() || !q.next()) return out;
    out.insert(QStringLiteral("locgId"), q.value(0).toString());
    out.insert(QStringLiteral("rank"), q.value(1).toInt());
    out.insert(QStringLiteral("title"), q.value(2).toString());
    out.insert(QStringLiteral("year"), q.value(3).toInt());
    out.insert(QStringLiteral("slug"), q.value(4).toString());
    out.insert(QStringLiteral("publisher"), q.value(5).toString());
    out.insert(QStringLiteral("cover"), q.value(6).toString());
    out.insert(QStringLiteral("synopsis"), q.value(7).toString());
    out.insert(QStringLiteral("genres"), q.value(8).toString());

    QVariantList editions;
    QSqlQuery e(m_db);
    e.prepare(QStringLiteral(
        "select title, display_title, format, collects, isbn, pages, published, chid,"
        "       cover, available, getcomics_post, creators, description"
        " from curated_edition where locg_id = :id order by id asc"));
    e.bindValue(QStringLiteral(":id"), locgId);
    if (e.exec()) {
        while (e.next()) {
            QVariantMap m;
            m.insert(QStringLiteral("title"), e.value(0).toString());
            m.insert(QStringLiteral("displayTitle"), e.value(1).toString());
            m.insert(QStringLiteral("format"), e.value(2).toString());
            m.insert(QStringLiteral("collects"), e.value(3).toString());
            m.insert(QStringLiteral("isbn"), e.value(4).toString());
            m.insert(QStringLiteral("pages"), e.value(5).toInt());
            m.insert(QStringLiteral("published"), e.value(6).toString());
            m.insert(QStringLiteral("chid"), e.value(7).toString());
            m.insert(QStringLiteral("cover"), e.value(8).toString());
            m.insert(QStringLiteral("available"), e.value(9).toInt() != 0);
            m.insert(QStringLiteral("getcomicsPost"), e.value(10).toString());
            m.insert(QStringLiteral("creators"), e.value(11).toString());
            m.insert(QStringLiteral("description"), e.value(12).toString());
            editions.append(m);
        }
    }
    out.insert(QStringLiteral("editions"), editions);
    return out;
}

QVariantMap ComicsCatalog::curatedByNorm(const QString& normTitle) const {
    QVariantMap out;
    const QString t = normTitle.trimmed();
    if (!curatedReady() || t.isEmpty()) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select locg_id, title, cover, publisher from curated_series"
        " where norm_title = :t order by rank asc limit 1"));
    q.bindValue(QStringLiteral(":t"), t);
    if (!q.exec() || !q.next()) return out;
    out.insert(QStringLiteral("locgId"), q.value(0).toString());
    out.insert(QStringLiteral("title"), q.value(1).toString());
    out.insert(QStringLiteral("cover"), q.value(2).toString());
    out.insert(QStringLiteral("publisher"), q.value(3).toString());
    return out;
}

QVariantList ComicsCatalog::curatedGenreShelves(int maxCovers) const {
    QVariantList out;
    if (!curatedReady()) return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select genre, count(*) as c from curated_genre group by genre"
        " order by c desc, genre asc"));
    if (!q.exec()) return out;
    std::vector<std::pair<QString, int>> genres;
    while (q.next()) genres.emplace_back(q.value(0).toString(), q.value(1).toInt());

    for (const auto& g : genres) {
        QVariantMap m;
        m.insert(QStringLiteral("name"), g.first);
        m.insert(QStringLiteral("count"), g.second);
        QVariantList covers;
        QSqlQuery c(m_db);
        c.prepare(QStringLiteral(
            "select s.cover from curated_series s"
            " join curated_genre g on g.locg_id = s.locg_id"
            " where g.genre = :genre and s.cover != ''"
            " order by s.rank asc limit :lim"));
        c.bindValue(QStringLiteral(":genre"), g.first);
        c.bindValue(QStringLiteral(":lim"), maxCovers);
        if (c.exec()) {
            while (c.next()) covers.append(c.value(0).toString());
        }
        m.insert(QStringLiteral("covers"), covers);
        out.append(m);
    }
    return out;
}

bool ComicsCatalog::curatedHasDownloadable(const QString& locgId) const {
    if (!curatedReady() || locgId.isEmpty()) return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select exists(select 1 from curated_edition"
        " where locg_id = :id and available = 1 and getcomics_post != '')"));
    q.bindValue(QStringLiteral(":id"), locgId);
    if (!q.exec() || !q.next()) return false;
    return q.value(0).toInt() != 0;
}

// --- shelf (browse-landing) ---

QVariantList ComicsCatalog::shelf(const QString& kind, const QString& arg, int limit) const {
    QVariantList out;
    if (!m_ok) return out;
    QString where;
    bool bindArg = false;
    if (kind == QStringLiteral("stocked")) {
        where = QStringLiteral("1=1");
    } else if (kind == QStringLiteral("publisher")) {
        where = QStringLiteral("s.publisher = :arg");
        bindArg = true;
    } else if (kind == QStringLiteral("decade")) {
        bool ok = false;
        const int d = arg.toInt(&ok);
        if (!ok) return out;
        where = QStringLiteral("s.year between %1 and %2").arg(d).arg(d + 9);
    } else if (kind == QStringLiteral("deep")) {
        where = QStringLiteral("coalesce(t.downloads,0) >= 10");
    } else if (kind == QStringLiteral("fanmade")) {
        where = QStringLiteral("exists(select 1 from download dl where dl.series_id = s.gcd_id and dl.fan_made = 1)");
    } else {
        return out;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select s.gcd_id, s.title, s.year, s.year_ended, s.publisher, s.cover,"
        "       coalesce(t.downloads,0) as dls"
        " from series s left join series_stats t on t.series_id = s.gcd_id"
        " where ") + where + QStringLiteral(
        " order by dls desc, s.year desc limit :lim"));
    if (bindArg) q.bindValue(QStringLiteral(":arg"), arg);
    q.bindValue(QStringLiteral(":lim"), limit);
    if (!q.exec()) return out;
    while (q.next()) {
        QVariantMap m;
        m.insert(QStringLiteral("gcdId"), q.value(0).toInt());
        m.insert(QStringLiteral("title"), q.value(1).toString());
        m.insert(QStringLiteral("year"), q.value(2).toInt());
        m.insert(QStringLiteral("yearEnded"), q.value(3).toInt());
        m.insert(QStringLiteral("publisher"), q.value(4).toString());
        m.insert(QStringLiteral("cover"), q.value(5).toString());
        m.insert(QStringLiteral("downloads"), q.value(6).toInt());
        out.append(m);
    }
    return out;
}
