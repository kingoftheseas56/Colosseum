#include "engine/ComicsCatalog.h"
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

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
        "select 1 from series limit 1")) && true;
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
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "select s.gcd_id, s.title, s.year, s.year_ended, s.publisher, s.cover,"
        "       coalesce(t.downloads,0) as dls"
        " from series s left join series_stats t on t.series_id = s.gcd_id"
        " where s.title like :pat escape '\\'"
        " order by case when lower(s.title) = lower(:txt) then 0"
        "               when s.title like :prefix escape '\\' then 1 else 2 end,"
        "          dls desc, s.year desc"
        " limit :lim"));
    const QString esc = likeEscape(t);
    q.bindValue(QStringLiteral(":pat"), QStringLiteral("%%%1%%").arg(esc));
    q.bindValue(QStringLiteral(":txt"), t);
    q.bindValue(QStringLiteral(":prefix"), esc + QStringLiteral("%"));
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
