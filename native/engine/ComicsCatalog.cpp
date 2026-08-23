#include "engine/ComicsCatalog.h"
#include <QDate>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>
#include <algorithm>
#include <utility>
#include <vector>

bool ComicsCatalog::openAt(const QString& dbPath) {
    if (!QFileInfo::exists(dbPath)) {
        qInfo("[comics-catalog] no db at %s — catalogue lane dormant", qUtf8Printable(dbPath));
        return false;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(dbPath);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    if (!m_db.open()) {
        qInfo("[comics-catalog] open failed: %s", qUtf8Printable(m_db.lastError().text()));
        return false;
    }
    QSqlQuery probe(m_db);   // schema sanity: the three catalogue tables must exist
    const bool ok = probe.exec(QStringLiteral("select 1 from series limit 1"));
    if (!ok) qInfo("[comics-catalog] db present but series table missing — dormant");
    else qInfo("[comics-catalog] ready (%s)", qUtf8Printable(dbPath));
    return ok;
}

ComicsCatalog::ComicsCatalog(const QString& dbPath, QObject* parent) : QObject(parent) {
    m_conn = QStringLiteral("comics_catalog_%1").arg(reinterpret_cast<quintptr>(this));
    m_ok = openAt(dbPath);
}

ComicsCatalog::~ComicsCatalog() {
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();                     // release handle before removal
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
}

bool ComicsCatalog::reopen(const QString& dbPath) {
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

void ComicsCatalog::closeForSwap() {
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

static QString likeEscape(const QString& s) {
    QString out = s;
    out.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    out.replace(QLatin1Char('%'), QLatin1String("\\%"));
    out.replace(QLatin1Char('_'), QLatin1String("\\_"));
    return out;
}

static bool catalogTableExists(const QSqlDatabase& db, const QString& name) {
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "select exists(select 1 from sqlite_master where type='table' and name=:name)"));
    q.bindValue(QStringLiteral(":name"), name);
    return q.exec() && q.next() && q.value(0).toInt() != 0;
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
        m.insert(QStringLiteral("mirrors"), QVariantList());   // filled below
        out.append(m);
    }
    // mirror doors (spec 2026-07-18): one query for the whole series, grouped
    // in-process. Missing download_mirror table (older db) -> exec fails ->
    // every row keeps its empty list — graceful on stale catalogues.
    QSqlQuery mq(m_db);
    mq.prepare(QStringLiteral(
        "select m.post_id, m.url, m.host, m.label from download_mirror m"
        " join download d on d.post_id = m.post_id where d.series_id = :id"));
    mq.bindValue(QStringLiteral(":id"), gcdId);
    if (mq.exec()) {
        QHash<int, QVariantList> bucket;
        while (mq.next()) {
            QVariantMap l;
            l.insert(QStringLiteral("url"), mq.value(1).toString());
            l.insert(QStringLiteral("host"), mq.value(2).toString());
            l.insert(QStringLiteral("label"), mq.value(3).toString());
            bucket[mq.value(0).toInt()].append(l);
        }
        for (QVariant& row : out) {
            QVariantMap m = row.toMap();
            const auto it = bucket.constFind(m.value(QStringLiteral("postId")).toInt());
            if (it != bucket.constEnd()) {
                m.insert(QStringLiteral("mirrors"), it.value());
                row = m;
            }
        }
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

    const bool hasSources = catalogTableExists(m_db, QStringLiteral("curated_source"));
    const bool hasMirrors = catalogTableExists(m_db, QStringLiteral("download_mirror"));
    QVariantList editions;
    QSqlQuery e(m_db);
    e.prepare(QStringLiteral(
        "select id, title, display_title, format, collects, isbn, pages, published, chid,"
        "       cover, available, getcomics_post, creators, description"
        " from curated_edition where locg_id = :id order by id asc"));
    e.bindValue(QStringLiteral(":id"), locgId);
    if (e.exec()) {
        while (e.next()) {
            const int editionId = e.value(0).toInt();
            QVariantMap m;
            m.insert(QStringLiteral("title"), e.value(1).toString());
            m.insert(QStringLiteral("displayTitle"), e.value(2).toString());
            m.insert(QStringLiteral("format"), e.value(3).toString());
            m.insert(QStringLiteral("collects"), e.value(4).toString());
            m.insert(QStringLiteral("isbn"), e.value(5).toString());
            m.insert(QStringLiteral("pages"), e.value(6).toInt());
            m.insert(QStringLiteral("published"), e.value(7).toString());
            m.insert(QStringLiteral("chid"), e.value(8).toString());
            m.insert(QStringLiteral("cover"), e.value(9).toString());
            m.insert(QStringLiteral("available"), e.value(10).toInt() != 0);
            m.insert(QStringLiteral("getcomicsPost"), e.value(11).toString());
            m.insert(QStringLiteral("creators"), e.value(12).toString());
            m.insert(QStringLiteral("description"), e.value(13).toString());

            QVariantList sources;
            if (hasSources) {
                QSqlQuery sq(m_db);
                sq.prepare(QStringLiteral(
                    "select post_id, post_url, source_title, source_date, source_cover,"
                    "       source_kind, source_format, attachment_method, confidence_class,"
                    "       fan_made, available, parsed_series_text, parser_version"
                    " from curated_source where curated_edition_id = :eid"
                    " order by fan_made asc, source_date desc, post_id desc"));
                sq.bindValue(QStringLiteral(":eid"), editionId);
                if (sq.exec()) {
                    while (sq.next()) {
                        QVariantMap source;
                        const int postId = sq.value(0).toInt();
                        source.insert(QStringLiteral("postId"), postId);
                        source.insert(QStringLiteral("postUrl"), sq.value(1).toString());
                        source.insert(QStringLiteral("title"), sq.value(2).toString());
                        source.insert(QStringLiteral("date"), sq.value(3).toString());
                        source.insert(QStringLiteral("cover"), sq.value(4).toString());
                        source.insert(QStringLiteral("kind"), sq.value(5).toString());
                        source.insert(QStringLiteral("format"), sq.value(6).toString());
                        source.insert(QStringLiteral("attachmentMethod"), sq.value(7).toString());
                        source.insert(QStringLiteral("confidenceClass"), sq.value(8).toString());
                        source.insert(QStringLiteral("fanMade"), sq.value(9).toInt() != 0);
                        source.insert(QStringLiteral("available"), sq.value(10).toInt() != 0);
                        source.insert(QStringLiteral("parsedSeriesText"), sq.value(11).toString());
                        source.insert(QStringLiteral("parserVersion"), sq.value(12).toInt());
                        QVariantList mirrors;
                        if (hasMirrors) {
                            QSqlQuery mq(m_db);
                            mq.prepare(QStringLiteral(
                                "select url, host, label from download_mirror"
                                " where post_id = :pid order by rowid asc"));
                            mq.bindValue(QStringLiteral(":pid"), postId);
                            if (mq.exec()) {
                                while (mq.next()) {
                                    mirrors.append(QVariantMap{
                                        {QStringLiteral("url"), mq.value(0).toString()},
                                        {QStringLiteral("host"), mq.value(1).toString()},
                                        {QStringLiteral("label"), mq.value(2).toString()}});
                                }
                            }
                        }
                        source.insert(QStringLiteral("mirrors"), mirrors);
                        sources.append(source);
                    }
                }
            }
            m.insert(QStringLiteral("sources"), sources);
            editions.append(m);
        }
    }
    out.insert(QStringLiteral("editions"), editions);
    out.insert(QStringLiteral("coverage"), curatedCoverage(locgId));
    return out;
}

QVariantMap ComicsCatalog::curatedCoverage(const QString& locgId) const {
    QVariantMap out;
    if (!curatedReady() || locgId.isEmpty()
        || !catalogTableExists(m_db, QStringLiteral("curated_series_coverage")))
        return out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select gcd_series_id, canonical_issue_count, covered_issue_count, coverage_ratio,"
        "       availability_state, source_count, official_source_count, community_source_count,"
        "       latest_source_date, computed_at, parser_version"
        " from curated_series_coverage where locg_id = :id"));
    q.bindValue(QStringLiteral(":id"), locgId);
    if (!q.exec() || !q.next()) return out;
    out.insert(QStringLiteral("gcdSeriesId"), q.value(0));
    out.insert(QStringLiteral("canonicalIssueCount"), q.value(1).toInt());
    out.insert(QStringLiteral("coveredIssueCount"), q.value(2).toInt());
    out.insert(QStringLiteral("coverageRatio"), q.value(3).toDouble());
    out.insert(QStringLiteral("availabilityState"), q.value(4).toString());
    out.insert(QStringLiteral("sourceCount"), q.value(5).toInt());
    out.insert(QStringLiteral("officialSourceCount"), q.value(6).toInt());
    out.insert(QStringLiteral("communitySourceCount"), q.value(7).toInt());
    out.insert(QStringLiteral("latestSourceDate"), q.value(8).toString());
    out.insert(QStringLiteral("computedAt"), q.value(9).toString());
    out.insert(QStringLiteral("parserVersion"), q.value(10).toInt());
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
    if (catalogTableExists(m_db, QStringLiteral("curated_source"))) {
        q.prepare(QStringLiteral(
            "select exists(select 1 from curated_source s"
            " join curated_edition e on e.id = s.curated_edition_id"
            " where e.locg_id = :id and s.available = 1 and s.fan_made = 0"
            " and s.confidence_class in ('exact','strong'))"));
    } else {
        q.prepare(QStringLiteral(
            "select exists(select 1 from curated_edition"
            " where locg_id = :id and available = 1 and getcomics_post != '')"));
    }
    q.bindValue(QStringLiteral(":id"), locgId);
    return q.exec() && q.next() && q.value(0).toInt() != 0;
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

// --- Tankoban Discover: discovery filters + house ranking (spec 2026-08-01) ---
//
// includeExplicit is a deliberate NO-OP in this lane. Ground-truth (Hemanth,
// 2026-08-02): the curated comics catalogue carries NO adult/explicit
// classification — there is no such column on curated_series / curated_edition and
// no maturity genre value; it is the mainstream LOCG top-comics list. The parameter
// is accepted for interface parity with the manga (MalCatalog) and Task-9 lanes,
// but gates nothing here: results are identical whether it is true or false. This
// follows the design's conservative rule — unknown classification defaults to
// VISIBLE; a false positive (hiding a mainstream title) is worse than incomplete
// gating. Availability is likewise a BOOST, never an inclusion gate.

namespace {

constexpr double kWPopularity   = 0.65;
constexpr double kWAvailability = 0.20;
constexpr double kWRecency      = 0.10;
constexpr double kWMetadata     = 0.05;
static_assert(kWPopularity + kWAvailability + kWRecency + kWMetadata > 0.9999999
              && kWPopularity + kWAvailability + kWRecency + kWMetadata < 1.0000001,
              "house-rank base weights must sum to 1.0");
constexpr int kMetadataFacets = 4;
constexpr int kRecencyFloor   = 1980;
constexpr int kYearSaneMin    = 1900;
constexpr int kYearSaneMax    = 2100;
constexpr int kPageLimitMax   = 100;

int maxPublishedYear(const QString& published) {
    static const QRegularExpression re(QStringLiteral("(\\d{4})"));
    int best = 0;
    auto it = re.globalMatch(published);
    while (it.hasNext()) {
        const int y = it.next().captured(1).toInt();
        if (y >= kYearSaneMin && y <= kYearSaneMax && y > best) best = y;
    }
    return best;
}

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

QString availabilityLabel(const QString& key) {
    if (key == QStringLiteral("available")) return QStringLiteral("Available to read");
    if (key == QStringLiteral("complete")) return QStringLiteral("Complete");
    if (key == QStringLiteral("near_complete")) return QStringLiteral("Near Complete");
    if (key == QStringLiteral("partial")) return QStringLiteral("Partial");
    if (key == QStringLiteral("bibliography_only")) return QStringLiteral("Bibliography only");
    return key;
}

struct DiscoRow {
    QString locg, title, norm, publisher, cover, genres;
    QString availabilityState, latestSourceDate;
    int year = 0, rank = 0, edCount = 0, availCount = 0, maxYear = 0;
    int sourceCount = 0, officialSourceCount = 0, communitySourceCount = 0;
    bool hasRank = false;
    double availFrac = 0.0, coverageRatio = 0.0;
    double score = 0.0, cPop = 0.0, cAvail = 0.0, cRec = 0.0, cMeta = 0.0;
};

} // namespace

QVariantList ComicsCatalog::discoverFilters(const QString& axis, bool /*includeExplicit*/) const {
    QVariantList out;
    if (!curatedReady()) return out;
    QString sql;
    if (axis == QStringLiteral("genre")) {
        sql = QStringLiteral(
            "select genre, count(*) as c from curated_genre"
            " group by genre order by c desc, genre asc");
    } else if (axis == QStringLiteral("publisher")) {
        sql = QStringLiteral(
            "select publisher, count(*) as c from curated_series"
            " where publisher != '' group by publisher order by c desc, publisher asc");
    } else if (axis == QStringLiteral("format")) {
        sql = QStringLiteral(
            "select min(trim(format)), count(distinct locg_id) as c from curated_edition"
            " where trim(format) != '' group by lower(trim(format))"
            " order by c desc, lower(min(trim(format))) asc");
    } else if (axis == QStringLiteral("availability")) {
        if (!catalogTableExists(m_db, QStringLiteral("curated_series_coverage"))) return out;
        QSqlQuery available(m_db);
        if (available.exec(QStringLiteral(
                "select count(*) from curated_series_coverage where official_source_count > 0"))
            && available.next() && available.value(0).toInt() > 0) {
            out.append(QVariantMap{{QStringLiteral("key"), QStringLiteral("available")},
                                   {QStringLiteral("label"), availabilityLabel(QStringLiteral("available"))},
                                   {QStringLiteral("count"), available.value(0).toInt()}});
        }
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "select availability_state, count(*) from curated_series_coverage"
                " where availability_state in ('complete','near_complete','partial','bibliography_only')"
                " group by availability_state order by count(*) desc, availability_state asc")))
            return out;
        while (q.next()) {
            const QString key = q.value(0).toString();
            out.append(QVariantMap{{QStringLiteral("key"), key},
                                   {QStringLiteral("label"), availabilityLabel(key)},
                                   {QStringLiteral("count"), q.value(1).toInt()}});
        }
        return out;
    } else {
        return out;
    }
    QSqlQuery q(m_db);
    if (!q.exec(sql)) return out;
    while (q.next()) {
        const QString v = q.value(0).toString();
        out.append(QVariantMap{{QStringLiteral("key"), v},
                               {QStringLiteral("label"), v},
                               {QStringLiteral("count"), q.value(1).toInt()}});
    }
    return out;
}

QVariantMap ComicsCatalog::discoverPage(const QString& catalogId, const QString& filterAxis,
                                        const QString& filterKey, bool /*includeExplicit*/,
                                        int offset, int limit) const {
    const int lim = std::clamp(limit, 1, kPageLimitMax);
    const int off = std::max(0, offset);
    auto pack = [&](const QVariantList& items, int total) {
        return QVariantMap{
            {QStringLiteral("items"), items},
            {QStringLiteral("nextOffset"), off + static_cast<int>(items.size())},
            {QStringLiteral("exhausted"), off + static_cast<int>(items.size()) >= total},
            {QStringLiteral("freshness"), QStringLiteral("bundled")}};
    };
    if (!curatedReady()) return pack({}, 0);

    const bool hasCoverage = catalogTableExists(m_db, QStringLiteral("curated_series_coverage"));
    static const QSet<QString> catalogs = {
        QStringLiteral("popular"), QStringLiteral("new-releases"),
        QStringLiteral("most-stocked"), QStringLiteral("all"),
        QStringLiteral("recently-available"), QStringLiteral("complete-runs"),
        QStringLiteral("near-complete"), QStringLiteral("community-collections")};
    if (!catalogs.contains(catalogId)) return pack({}, 0);
    const bool coverageCatalog = catalogId == QStringLiteral("recently-available")
        || catalogId == QStringLiteral("complete-runs")
        || catalogId == QStringLiteral("near-complete")
        || catalogId == QStringLiteral("community-collections");
    if (coverageCatalog && !hasCoverage) return pack({}, 0);
    if (!(filterAxis.isEmpty() || filterAxis == QStringLiteral("genre")
          || filterAxis == QStringLiteral("publisher") || filterAxis == QStringLiteral("format")
          || filterAxis == QStringLiteral("availability")))
        return pack({}, 0);
    if (filterAxis == QStringLiteral("availability") && !hasCoverage) return pack({}, 0);

    const bool facetGenre = filterAxis == QStringLiteral("genre") && !filterKey.isEmpty();
    const bool facetPublisher = filterAxis == QStringLiteral("publisher") && !filterKey.isEmpty();
    const bool facetFormat = filterAxis == QStringLiteral("format") && !filterKey.isEmpty();
    const bool facetAvailability = filterAxis == QStringLiteral("availability") && !filterKey.isEmpty();

    QString sql = QStringLiteral(
        "select s.locg_id, s.rank, s.title, s.norm_title, s.year, s.publisher, s.cover, s.synopsis,"
        "       coalesce((select group_concat(g.genre) from curated_genre g"
        "                 where g.locg_id = s.locg_id), '') as genres,"
        "       (select count(*) from curated_edition e where e.locg_id = s.locg_id) as ed_count,"
        "       (select count(*) from curated_edition e where e.locg_id = s.locg_id"
        "        and e.available = 1 and e.getcomics_post != '') as avail_count"
        " from curated_series s");
    if (facetGenre)
        sql += QStringLiteral(" join curated_genre gf on gf.locg_id = s.locg_id where lower(gf.genre) = lower(?)");
    else if (facetPublisher)
        sql += QStringLiteral(" where lower(s.publisher) = lower(?)");
    else if (facetFormat)
        sql += QStringLiteral(" where exists(select 1 from curated_edition ef where ef.locg_id = s.locg_id"
                              " and lower(trim(ef.format)) = lower(?))");
    else if (facetAvailability && filterKey == QStringLiteral("available"))
        sql += QStringLiteral(" where exists(select 1 from curated_series_coverage cf"
                              " where cf.locg_id = s.locg_id and cf.official_source_count > 0)");
    else if (facetAvailability)
        sql += QStringLiteral(" where exists(select 1 from curated_series_coverage cf"
                              " where cf.locg_id = s.locg_id and lower(cf.availability_state) = lower(?))");

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (facetGenre || facetPublisher || facetFormat
        || (facetAvailability && filterKey != QStringLiteral("available")))
        q.addBindValue(filterKey);
    if (!q.exec()) return pack({}, 0);

    std::vector<DiscoRow> rows;
    QSet<QString> scopedIds;
    while (q.next()) {
        const QString locg = q.value(0).toString();
        if (scopedIds.contains(locg)) continue;
        scopedIds.insert(locg);
        DiscoRow r;
        r.locg = locg;
        r.hasRank = !q.value(1).isNull();
        r.rank = q.value(1).toInt();
        r.title = q.value(2).toString();
        r.norm = q.value(3).toString();
        r.year = q.value(4).toInt();
        r.publisher = q.value(5).toString();
        r.cover = q.value(6).toString();
        const QString synopsis = q.value(7).toString();
        r.genres = q.value(8).toString();
        r.edCount = q.value(9).toInt();
        r.availCount = q.value(10).toInt();
        r.availFrac = r.edCount > 0 ? double(r.availCount) / double(r.edCount) : 0.0;
        int meta = 0;
        if (!r.cover.isEmpty()) ++meta;
        if (!synopsis.isEmpty()) ++meta;
        if (!r.publisher.isEmpty()) ++meta;
        if (!r.genres.isEmpty()) ++meta;
        r.cMeta = kWMetadata * (meta / double(kMetadataFacets));
        rows.push_back(std::move(r));
    }
    if (rows.empty()) return pack({}, 0);

    if (hasCoverage) {
        QHash<QString, QVariantMap> coverage;
        QSqlQuery cq(m_db);
        if (cq.exec(QStringLiteral(
                "select locg_id, availability_state, coverage_ratio, source_count,"
                " official_source_count, community_source_count, latest_source_date"
                " from curated_series_coverage"))) {
            while (cq.next()) {
                coverage.insert(cq.value(0).toString(), QVariantMap{
                    {QStringLiteral("state"), cq.value(1).toString()},
                    {QStringLiteral("ratio"), cq.value(2).toDouble()},
                    {QStringLiteral("sources"), cq.value(3).toInt()},
                    {QStringLiteral("official"), cq.value(4).toInt()},
                    {QStringLiteral("community"), cq.value(5).toInt()},
                    {QStringLiteral("latest"), cq.value(6).toString()}});
            }
        }
        for (DiscoRow& r : rows) {
            const QVariantMap c = coverage.value(r.locg);
            r.availabilityState = c.value(QStringLiteral("state")).toString();
            r.coverageRatio = c.value(QStringLiteral("ratio")).toDouble();
            r.sourceCount = c.value(QStringLiteral("sources")).toInt();
            r.officialSourceCount = c.value(QStringLiteral("official")).toInt();
            r.communitySourceCount = c.value(QStringLiteral("community")).toInt();
            r.latestSourceDate = c.value(QStringLiteral("latest")).toString();
        }
    }

    if (coverageCatalog) {
        rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const DiscoRow& r) {
            if (catalogId == QStringLiteral("recently-available"))
                return r.officialSourceCount <= 0 || r.latestSourceDate.isEmpty();
            if (catalogId == QStringLiteral("complete-runs"))
                return r.availabilityState != QStringLiteral("complete");
            if (catalogId == QStringLiteral("near-complete"))
                return r.availabilityState != QStringLiteral("near_complete");
            return r.communitySourceCount <= 0;
        }), rows.end());
        if (rows.empty()) return pack({}, 0);
    }

    QHash<QString, int> maxYear;
    QSqlQuery e(m_db);
    if (e.exec(QStringLiteral("select locg_id, published from curated_edition where published != ''"))) {
        while (e.next()) {
            const QString locg = e.value(0).toString();
            if (!scopedIds.contains(locg)) continue;
            const int y = maxPublishedYear(e.value(1).toString());
            if (y > maxYear.value(locg, 0)) maxYear[locg] = y;
        }
    }

    int minRank = 0, maxRank = 0;
    bool anyRank = false;
    for (const DiscoRow& r : rows) {
        if (!r.hasRank) continue;
        if (!anyRank) { minRank = maxRank = r.rank; anyRank = true; }
        else { minRank = std::min(minRank, r.rank); maxRank = std::max(maxRank, r.rank); }
    }
    const int recNow = QDate::currentDate().year();
    const double recSpan = std::max(1, recNow - kRecencyFloor);
    for (DiscoRow& r : rows) {
        r.maxYear = maxYear.value(r.locg, 0);
        const double popularity = !anyRank || !r.hasRank ? 0.0
            : (maxRank == minRank ? 1.0
               : 1.0 - double(r.rank - minRank) / double(maxRank - minRank));
        const double recency = r.maxYear > 0
            ? clamp01(double(r.maxYear - kRecencyFloor) / recSpan) : 0.5;
        double wPop = kWPopularity, wAvail = kWAvailability, wRec = kWRecency;
        if (!r.hasRank) {
            const double nonMeta = wAvail + wRec;
            wAvail += kWPopularity * (wAvail / nonMeta);
            wRec += kWPopularity * (wRec / nonMeta);
            wPop = 0.0;
        }
        r.cPop = wPop * popularity;
        r.cAvail = wAvail * r.availFrac;
        r.cRec = wRec * recency;
        r.score = r.cPop + r.cAvail + r.cRec + r.cMeta;
    }

    auto canon = [](const DiscoRow& a, const DiscoRow& b) -> int {
        if (a.norm != b.norm) return a.norm < b.norm ? -1 : 1;
        if (a.year != b.year) return a.year < b.year ? -1 : 1;
        if (a.locg != b.locg) return a.locg < b.locg ? -1 : 1;
        return 0;
    };
    if (catalogId == QStringLiteral("popular")) {
        std::stable_sort(rows.begin(), rows.end(), [&](const DiscoRow& a, const DiscoRow& b) {
            if (a.score != b.score) return a.score > b.score;
            return canon(a, b) < 0;
        });
    } else if (catalogId == QStringLiteral("most-stocked")) {
        std::stable_sort(rows.begin(), rows.end(), [&](const DiscoRow& a, const DiscoRow& b) {
            if (a.edCount != b.edCount) return a.edCount > b.edCount;
            if (a.score != b.score) return a.score > b.score;
            if (a.availFrac != b.availFrac) return a.availFrac > b.availFrac;
            return canon(a, b) < 0;
        });
    } else if (catalogId == QStringLiteral("new-releases")) {
        std::stable_sort(rows.begin(), rows.end(), [&](const DiscoRow& a, const DiscoRow& b) {
            if (a.maxYear != b.maxYear) return a.maxYear > b.maxYear;
            if (a.score != b.score) return a.score > b.score;
            return canon(a, b) < 0;
        });
    } else if (catalogId == QStringLiteral("recently-available")) {
        std::stable_sort(rows.begin(), rows.end(), [&](const DiscoRow& a, const DiscoRow& b) {
            if (a.latestSourceDate != b.latestSourceDate) return a.latestSourceDate > b.latestSourceDate;
            if (a.score != b.score) return a.score > b.score;
            return canon(a, b) < 0;
        });
    } else if (catalogId == QStringLiteral("complete-runs")
               || catalogId == QStringLiteral("near-complete")) {
        std::stable_sort(rows.begin(), rows.end(), [&](const DiscoRow& a, const DiscoRow& b) {
            if (a.coverageRatio != b.coverageRatio) return a.coverageRatio > b.coverageRatio;
            if (a.score != b.score) return a.score > b.score;
            return canon(a, b) < 0;
        });
    } else if (catalogId == QStringLiteral("community-collections")) {
        std::stable_sort(rows.begin(), rows.end(), [&](const DiscoRow& a, const DiscoRow& b) {
            if (a.communitySourceCount != b.communitySourceCount)
                return a.communitySourceCount > b.communitySourceCount;
            if (a.latestSourceDate != b.latestSourceDate) return a.latestSourceDate > b.latestSourceDate;
            return canon(a, b) < 0;
        });
    } else {
        std::stable_sort(rows.begin(), rows.end(), [&](const DiscoRow& a, const DiscoRow& b) {
            return canon(a, b) < 0;
        });
    }

    const int total = static_cast<int>(rows.size());
    QVariantList items;
    for (int i = off; i < total && static_cast<int>(items.size()) < lim; ++i) {
        const DiscoRow& r = rows[static_cast<size_t>(i)];
        const bool available = hasCoverage ? r.officialSourceCount > 0 : r.availCount > 0;
        QVariantMap m;
        m.insert(QStringLiteral("locgId"), r.locg);
        m.insert(QStringLiteral("title"), r.title);
        m.insert(QStringLiteral("year"), r.year);
        m.insert(QStringLiteral("publisher"), r.publisher);
        m.insert(QStringLiteral("cover"), r.cover);
        m.insert(QStringLiteral("genres"), r.genres);
        m.insert(QStringLiteral("format"), facetFormat ? filterKey : QString());
        m.insert(QStringLiteral("availability"), available);
        m.insert(QStringLiteral("availabilityState"), r.availabilityState);
        m.insert(QStringLiteral("coverageRatio"), r.coverageRatio);
        m.insert(QStringLiteral("latestSourceDate"), r.latestSourceDate);
        m.insert(QStringLiteral("sourceDepth"), r.sourceCount);
        m.insert(QStringLiteral("officialSourceCount"), r.officialSourceCount);
        m.insert(QStringLiteral("communitySourceCount"), r.communitySourceCount);
        m.insert(QStringLiteral("houseScore"), r.score);
        m.insert(QStringLiteral("houseComponents"), QVariantMap{
            {QStringLiteral("popularity"), r.cPop},
            {QStringLiteral("availability"), r.cAvail},
            {QStringLiteral("recency"), r.cRec},
            {QStringLiteral("metadata"), r.cMeta}});
        m.insert(QStringLiteral("explicit"), false);
        items.append(m);
    }
    return pack(items, total);
}
