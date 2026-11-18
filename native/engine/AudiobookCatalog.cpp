#include "AudiobookCatalog.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUrl>
#include <algorithm>

namespace {
// Match catalogue-audiobooks.ts. This discovery policy is intentionally separate
// from direct work lookup and released search/person results in any language.
const auto discoveryLanguages = QStringLiteral("'en','eng','english'");
QString released(const QString& alias) {
    return QStringLiteral("%1.release_date<=date('now') "
                          "AND date(%1.release_date,'+0 days')=%1.release_date").arg(alias);
}
QString discoverable(const QString& alias) {
    return released(alias) + QStringLiteral(" AND lower(trim(%1.language)) IN (%2)")
                                .arg(alias, discoveryLanguages);
}
const auto usableCover = QStringLiteral(
    "COALESCE(r.cover_url LIKE 'https://_%' OR r.cover_url LIKE 'http://_%',0)");
const auto releaseOrder = QStringLiteral("r.release_date DESC,r.added_at DESC,r.work_id,r.id");
QVariantMap workRow(const QVariantMap& row) {
    return {{"id",row.value("id")},{"title",row.value("title")},
            {"sortTitle",row.value("sort_title")},
            {"originalLanguage",row.value("original_language")},
            {"firstYear",row.value("first_year")}};
}
QVariantMap recordingRow(const QVariantMap& row) {
    QVariantMap result = {{"id",row.value("id")},{"workId",row.value("work_id")},
            {"language",row.value("language")},{"runtimeMin",row.value("runtime_min")},
            {"releaseYear",row.value("release_year")},{"abridgement",row.value("abridgement")},
            {"asin",row.value("asin")},{"isbn",row.value("isbn")}};
    const auto put=[&](const char* key,const char* column) {
        if(!row.value(column).toString().isEmpty()) result.insert(QString::fromLatin1(key),row.value(column));
    };
    put("releaseDate","release_date");put("publisher","publisher");
    put("coverUrl","cover_url");put("addedAt","added_at");
    result.insert(QStringLiteral("dramatized"),row.value("dramatized").toInt()!=0);
    result.insert(QStringLiteral("dramatizedSignals"),
                  QJsonDocument::fromJson(row.value("dramatized_signals").toString().toUtf8()).array().toVariantList());
    return result;
}
QVariantMap personRow(const QVariantMap& row) {
    return {{"id",row.value("id")},{"name",row.value("name")}};
}
QVariantMap seriesRow(const QVariantMap& row) {
    return {{"id",row.value("id")},{"name",row.value("name")},
            {"numberText",row.value("number_text")},{"positionSort",row.value("position_sort")}};
}
QString validId(const QString& raw) { return raw.trimmed().left(256); }
int pageOffset(int page) { return std::clamp(page,0,100000)*24; }
QVariant queryShape(const QString& method) {
    if(method=="shelves" || method=="genres" || method=="byPerson" || method=="search" || method=="latest")
        return QVariantList{};
    return QVariantMap{};
}
}

AudiobookCatalog::AudiobookCatalog(const QString& sqlitePath, QObject* parent)
    : QObject(parent),
      m_connection(QStringLiteral("audiobook_catalog_%1").arg(quintptr(this),0,16)),
      m_remote(new CatalogRemote(QStringLiteral("audiobooks"),this)) {
    connect(m_remote,&CatalogRemote::changed,this,&AudiobookCatalog::revisionChanged);
    connect(m_remote,&CatalogRemote::failed,this,&AudiobookCatalog::requestFailed);
    connect(m_remote,&CatalogRemote::queryChanged,this,&AudiobookCatalog::remoteQueryChanged);
    m_ok=open(sqlitePath);
}
AudiobookCatalog::~AudiobookCatalog() {
    if(m_db.isOpen()) m_db.close();
    m_db=QSqlDatabase();
    if(QSqlDatabase::contains(m_connection)) QSqlDatabase::removeDatabase(m_connection);
}
bool AudiobookCatalog::open(const QString& sqlitePath) {
    if(sqlitePath.isEmpty()||!QFileInfo(sqlitePath).isFile()) return false;
    m_db=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),m_connection);
    m_db.setDatabaseName(sqlitePath);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    const bool valid = m_db.open() && [this]{
        QSqlQuery query(m_db);
        return query.exec(QStringLiteral("PRAGMA user_version")) && query.next() &&
               query.value(0).toInt()==2;
    }();
    if(!valid) {
        m_db.close();
        m_db=QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }
    return valid;
}
bool AudiobookCatalog::reopen(const QString& sqlitePath) {
    const bool before=m_ok;
    if(m_db.isOpen()) m_db.close();
    m_db=QSqlDatabase();
    if(QSqlDatabase::contains(m_connection)) QSqlDatabase::removeDatabase(m_connection);
    m_genreMosaic.clear();
    m_genreMosaicDate = {};
    m_ok=open(sqlitePath);
    if(m_ok!=before || m_ok) emit readyChanged();
    return m_ok;
}
void AudiobookCatalog::configureRemote(QNetworkAccessManager* manager, const QUrl& endpoint,
                                       const QString& cachePath) {
    m_remote->configure(manager,endpoint,cachePath);
}
QVariantMap AudiobookCatalog::remoteQueryState(const QString& method, const QVariantList& args) const {
    return m_ok ? QVariantMap{} : m_remote->queryState(method,args,queryShape(method));
}
bool AudiobookCatalog::retryRemoteQuery(const QString& method, const QVariantList& args) {
    return !m_ok && m_remote->retry(method,args,queryShape(method));
}
QVariantList AudiobookCatalog::select(const QString& statement, const QVariantList& binds) const {
    QVariantList rows;
    if(!m_ok) return rows;
    QSqlQuery query(m_db);
    query.setForwardOnly(true);
    if(!query.prepare(statement)) return rows;
    for(const QVariant& value:binds) query.addBindValue(value);
    if(!query.exec()) return rows;
    const auto record=query.record();
    while(query.next()) {
        QVariantMap row;
        for(int i=0;i<record.count();++i) row.insert(record.fieldName(i),query.value(i));
        rows.append(row);
    }
    return rows;
}
QVariantList AudiobookCatalog::search(const QString& text, int limit) const {
    if(!m_ok) return m_remote->read(QStringLiteral("search"),{text,limit},QVariantList{}).toList();
    QVariantList result;
    QRegularExpression token(QStringLiteral("[\\p{L}\\p{N}]+"));
    auto matches=token.globalMatch(text.normalized(QString::NormalizationForm_KC));
    QStringList parts;
    while(matches.hasNext() && parts.size()<8) {
        parts << QStringLiteral("\"")+matches.next().captured().left(96)+QStringLiteral("\"*");
    }
    if(parts.isEmpty()) return result;
    const auto rows=select(QStringLiteral(
        "SELECT w.* FROM search_fts JOIN works w ON w.id=search_fts.id "
        "WHERE search_fts MATCH ? AND search_fts.kind='work' AND EXISTS ("
        "SELECT 1 FROM recordings r WHERE r.work_id=w.id AND %1) "
        "ORDER BY bm25(search_fts),w.title COLLATE NOCASE,w.id LIMIT ?").arg(released("r")),
        {parts.join(QLatin1Char(' ')),std::clamp(limit,1,100)});
    for(const QVariant& value:rows) result.append(workRow(value.toMap()));
    return result;
}
QVariantMap AudiobookCatalog::localRecording(const QVariantMap& row) const {
    auto result=recordingRow(row);
    QVariantList authors;
    for(const QVariant& value:select(QStringLiteral(
        "SELECT p.id,p.name FROM recording_narrators n JOIN people p ON p.id=n.person_id "
        "WHERE n.work_id=? AND n.recording_id=? ORDER BY n.ord,p.id"),
        {row.value("work_id"),row.value("id")})) {
        authors.append(personRow(value.toMap()));
    }
    result.insert(QStringLiteral("narrators"),authors);
    QVariantList asins;
    for(const QVariant& value:select(QStringLiteral(
        "SELECT asin,region FROM recording_asins WHERE work_id=? AND recording_id=? ORDER BY asin,region"),
        {row.value("work_id"),row.value("id")})) {
        const auto link=value.toMap();
        asins.append(QVariantMap{{"asin",link.value("asin")},{"region",link.value("region")}});
    }
    result.insert(QStringLiteral("asins"),asins);
    QVariantList isbns;
    for(const QVariant& value:select(QStringLiteral(
        "SELECT isbn FROM recording_isbns WHERE work_id=? AND recording_id=? ORDER BY isbn"),
        {row.value("work_id"),row.value("id")})) isbns.append(value.toMap().value("isbn"));
    result.insert(QStringLiteral("isbns"),isbns);
    return result;
}
QVariantMap AudiobookCatalog::work(const QString& identifier) const {
    const QString key=validId(identifier);
    if(!m_ok) return m_remote->read(QStringLiteral("work"),{key},QVariantMap{}).toMap();
    if(key.isEmpty()) return {};
    const auto rows=select(QStringLiteral("SELECT * FROM works WHERE id=?"),{key});
    if(rows.isEmpty()) return {};
    auto result=workRow(rows.first().toMap());
    QVariantList authors;
    for(const QVariant& value:select(QStringLiteral(
        "SELECT p.id,p.name FROM work_authors a JOIN people p ON p.id=a.person_id "
        "WHERE a.work_id=? ORDER BY a.ord,p.id"),{key})) {
        authors.append(personRow(value.toMap()));
    }
    result.insert(QStringLiteral("authors"),authors);
    QVariantList series;
    for(const QVariant& value:select(QStringLiteral(
        "SELECT s.id,s.name,ws.number_text,ws.position_sort FROM work_series ws "
        "JOIN series s ON s.id=ws.series_id WHERE ws.work_id=? "
        "ORDER BY s.name COLLATE NOCASE,s.id"),{key})) {
        series.append(seriesRow(value.toMap()));
    }
    result.insert(QStringLiteral("series"),series);
    QVariantList recordings;
    for(const QVariant& value:select(QStringLiteral(
        "SELECT * FROM recordings WHERE work_id=? ORDER BY release_year DESC,id"),{key})) {
        recordings.append(localRecording(value.toMap()));
    }
    result.insert(QStringLiteral("recordings"),recordings);
    return result;
}
QVariantMap AudiobookCatalog::recordingBy(const QString& kind, const QString& key) const {
    if(key.isEmpty()) return {};
    const QString table=kind==QStringLiteral("asin")?QStringLiteral("recording_asins"):
                         QStringLiteral("recording_isbns");
    const auto results=select(
        QStringLiteral("SELECT DISTINCT r.* FROM ")+table+
        QStringLiteral(" x JOIN recordings r ON r.work_id=x.work_id AND r.id=x.recording_id "
                       "WHERE x.")+kind+
        QStringLiteral("=? ORDER BY r.work_id,r.id LIMIT 2"),{key});
    return results.size()==1?localRecording(results.at(0).toMap()):QVariantMap{};
}
QVariantMap AudiobookCatalog::recordingByAsin(const QString& asin) const {
    const auto key=validId(asin).toUpper();
    if(!m_ok) return m_remote->read(QStringLiteral("recordingByAsin"),{key},QVariantMap{}).toMap();
    return recordingBy(QStringLiteral("asin"),key);
}
QVariantMap AudiobookCatalog::recordingByIsbn(const QString& isbn) const {
    const auto key=validId(isbn);
    if(!m_ok) return m_remote->read(QStringLiteral("recordingByIsbn"),{key},QVariantMap{}).toMap();
    return recordingBy(QStringLiteral("isbn"),key);
}
QVariantMap AudiobookCatalog::series(const QString& identifier) const {
    const auto key=validId(identifier);
    if(!m_ok) return m_remote->read(QStringLiteral("series"),{key},QVariantMap{}).toMap();
    if(key.isEmpty()) return {};
    const auto matches=select(QStringLiteral("SELECT id,name FROM series WHERE id=?"),{key});
    if(matches.isEmpty()) return {};
    const auto s=matches.first().toMap();
    QVariantList items;
    for(const QVariant& item:select(QStringLiteral(
        "SELECT w.id,w.title,ws.number_text,ws.position_sort, "
        "(SELECT r.cover_url FROM recordings r WHERE r.work_id=w.id "
        "AND %1 AND %2 ORDER BY %3 LIMIT 1) AS cover_url "
        "FROM work_series ws JOIN works w ON w.id=ws.work_id "
        "WHERE ws.series_id=? AND EXISTS ("
        "SELECT 1 FROM recordings r WHERE r.work_id=w.id AND %1) "
        "ORDER BY (ws.position_sort IS NULL),ws.position_sort,w.title COLLATE NOCASE,w.id")
        .arg(discoverable("r"), usableCover, releaseOrder),{key})) {
        const auto r=item.toMap();
        QVariantMap seriesWork{{"id",r.value("id")},{"title",r.value("title")},
                               {"numberText",r.value("number_text")},{"positionSort",r.value("position_sort")}};
        if (!r.value("cover_url").toString().isEmpty()) seriesWork.insert("coverUrl", r.value("cover_url"));
        items.append(seriesWork);
    }
    return {{"id",s.value("id")},{"name",s.value("name")},{"works",items}};
}
QVariantList AudiobookCatalog::latest(int page) const {
    if(!m_ok) return m_remote->read(QStringLiteral("latest"),{page},QVariantList{}).toList();
    QVariantList items;
    for(const QVariant& item:select(QStringLiteral(
        "SELECT r.*,w.title FROM recordings r JOIN works w ON w.id=r.work_id "
        "WHERE %1 ORDER BY r.release_year DESC,r.work_id,r.id LIMIT 24 OFFSET ?").arg(released("r")),
        {pageOffset(page)})) {
        const auto row=item.toMap();
        auto result=recordingRow(row);
        result.insert(QStringLiteral("title"),row.value("title"));
        items.append(result);
    }
    return items;
}
QVariantList AudiobookCatalog::byPerson(const QString& identifier, int page) const {
    const auto key=validId(identifier);
    if(!m_ok) return m_remote->read(QStringLiteral("byPerson"),{key,page},QVariantList{}).toList();
    QVariantList items;
    if(key.isEmpty()) return items;
    for(const QVariant& value:select(QStringLiteral(
        "SELECT w.* FROM works w JOIN ("
        "SELECT a.work_id FROM work_authors a WHERE a.person_id=? AND EXISTS ("
        "SELECT 1 FROM recordings r WHERE r.work_id=a.work_id AND %1) "
        "UNION "
        "SELECT n.work_id FROM recording_narrators n JOIN recordings r "
        "ON r.work_id=n.work_id AND r.id=n.recording_id WHERE n.person_id=? AND %1"
        ") credited ON credited.work_id=w.id "
        "ORDER BY (w.first_year IS NULL),w.first_year DESC,w.title COLLATE NOCASE,w.id LIMIT 24 OFFSET ?")
        .arg(released("r")),
        {key,key,pageOffset(page)})) items.append(workRow(value.toMap()));
    return items;
}

QVariantMap AudiobookCatalog::localItem(const QVariantMap& row) const {
    const auto workId=row.value("work_id");
    const auto recId=row.value("id");
    QVariantMap item{{"workId",workId},{"recordingId",recId},{"title",row.value("title")},
                     {"dramatized",row.value("dramatized").toInt()!=0}};
    QVariantList authors,narrators;
    for(const auto& v:select(QStringLiteral(
        "SELECT p.name FROM work_authors a JOIN people p ON p.id=a.person_id "
        "WHERE a.work_id=? ORDER BY a.ord,p.id"),{workId}))
        authors.append(v.toMap().value("name"));
    for(const auto& v:select(QStringLiteral(
        "SELECT p.name FROM recording_narrators n JOIN people p ON p.id=n.person_id "
        "WHERE n.work_id=? AND n.recording_id=? ORDER BY n.ord,p.id"),{workId,recId}))
        narrators.append(v.toMap().value("name"));
    if(!authors.isEmpty()) item.insert("authors",authors);
    if(!narrators.isEmpty()) item.insert("narrators",narrators);
    const auto cover=row.value("cover_url").toString();
    if ((cover.size()>8 && cover.startsWith("https://",Qt::CaseInsensitive))
        || (cover.size()>7 && cover.startsWith("http://",Qt::CaseInsensitive)))
        item.insert("coverUrl",cover);
    if(row.value("runtime_min").toInt()>0)
        item.insert("runtimeMin",row.value("runtime_min"));
    const auto release=row.value("release_date").toString();
    if(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$")).match(release).hasMatch())
        item.insert("releaseDate",release);
    const auto series=select(QStringLiteral(
        "SELECT s.name,ws.number_text FROM work_series ws JOIN series s ON s.id=ws.series_id "
        "WHERE ws.work_id=? ORDER BY s.name COLLATE NOCASE,s.id"),{workId});
    // An ambiguous multi-series work must not silently receive the wrong numbering system.
    if(series.size()==1) {
        const auto entry=series.first().toMap();
        if(!entry.value("name").toString().isEmpty())
            item.insert("seriesName",entry.value("name"));
        if(!entry.value("number_text").toString().isEmpty())
            item.insert("seriesNumber",entry.value("number_text"));
    }
    return item;
}
QVariantMap AudiobookCatalog::localPage(const QString& condition, const QVariantList& binds, int page) const {
    constexpr int pageSize = 20;
    const int n = std::clamp(page, 0, 100000);
    const int offset = n * pageSize;
    const int count = pageSize + 1;
    const auto eligible = QStringLiteral(" WHERE %1 AND %2").arg(condition, discoverable("r"));
    const auto statement = QStringLiteral(
        "SELECT r.*,w.title FROM recordings r JOIN works w ON w.id=r.work_id "
        "%1 AND %2 ORDER BY %3 LIMIT ? OFFSET ?");
    // Keep both cover groups separately date-ordered so an indexed shelf need
    // not sort its entire eligible population before returning the first page.
    auto rows = select(statement.arg(eligible, usableCover, releaseOrder),
                       binds + QVariantList{count, offset});
    if (rows.size() < count) {
        int uncoveredOffset = 0;
        if (offset > 0 && rows.isEmpty()) {
            const auto covered = select(QStringLiteral(
                "SELECT COUNT(*) AS n FROM recordings r %1 AND %2").arg(eligible, usableCover), binds);
            if (!covered.isEmpty()) uncoveredOffset = offset - covered.first().toMap().value("n").toInt();
        }
        rows.append(select(statement.arg(eligible, QStringLiteral("NOT ") + usableCover, releaseOrder),
                           binds + QVariantList{count - int(rows.size()), uncoveredOffset}));
    }
    QVariantList items;
    for (int i = 0; i < std::min(pageSize, int(rows.size())); ++i)
        items.append(localItem(rows.at(i).toMap()));
    return {{"items",items},{"page",n},{"hasMore",rows.size()>pageSize}};
}

QVariantList AudiobookCatalog::shelves() const {
    if(!m_ok)return m_remote->read(QStringLiteral("shelves"),{},QVariantList{}).toList();
    QVariantList rows{QVariantMap{{"key","new-releases"},{"title","New releases"}},
                      QVariantMap{{"key","dramatized"},{"title","Fully dramatized"}}};
    for(const auto& value:select(QStringLiteral(
        "SELECT person_id,name FROM narrator_shelves ORDER BY rank"))) {
        const auto v=value.toMap();
        rows.append(QVariantMap{{"key","narrator:"+v.value("person_id").toString()},
                                {"title","Narrated by "+v.value("name").toString()}});
    }
    return rows;
}
QVariantMap AudiobookCatalog::shelf(const QString& key, int page) const {
    const auto k=validId(key);
    if(!m_ok)return m_remote->read(QStringLiteral("shelf"),{k,page},QVariantMap{}).toMap();
    if(k==QStringLiteral("new-releases")) return localPage(QStringLiteral("1"),{},page);
    if(k==QStringLiteral("dramatized"))return localPage(QStringLiteral("r.dramatized=1"),{},page);
    if(k.startsWith(QStringLiteral("narrator:")) && k.size()>9) {
        const auto person=k.mid(9);
        if(!select(QStringLiteral("SELECT person_id FROM narrator_shelves WHERE person_id=?"),{person}).isEmpty())
            return localPage(QStringLiteral(
                "(r.librivox_id IS NULL OR r.librivox_id='') AND r.rowid IN "
                "(SELECT nr.rowid FROM recording_narrators n JOIN recordings nr "
                "ON nr.work_id=n.work_id AND nr.id=n.recording_id WHERE n.person_id=?)"),
                {person},page);
    }
    return {{"items",QVariantList{}},{"page",std::clamp(page,0,100000)},{"hasMore",false}};
}
QVariantList AudiobookCatalog::genres() const {
    if(!m_ok)return m_remote->read(QStringLiteral("genres"),{},QVariantList{}).toList();
    const auto today = QDateTime::currentDateTimeUtc().date();
    if (m_genreMosaicDate == today) return m_genreMosaic;
    QVariantList entries;
    // Count the supplied bake in work order. Rank recent distinct URLs first;
    // only genres that still need covers look farther back into the catalogue.
    for(const auto& value:select(QStringLiteral(R"SQL(
        WITH eligible_counts AS MATERIALIZED (
          SELECT g.genre,COUNT(*) AS n
          FROM recordings r NOT INDEXED CROSS JOIN work_genres g USING(work_id)
          WHERE %1 GROUP BY g.genre HAVING COUNT(*)>=4
        ), recent_recordings AS MATERIALIZED (
          SELECT r.work_id,r.cover_url,r.release_date FROM recordings r
          WHERE %1 AND %2
          ORDER BY r.release_date DESC,r.work_id,r.id LIMIT 4096
        ), recent_covers AS MATERIALIZED (
          SELECT g.genre,r.cover_url AS url,MAX(r.release_date) AS release_date
          FROM recent_recordings r JOIN work_genres g USING(work_id) GROUP BY g.genre,r.cover_url
        ), sparse_genres AS MATERIALIZED (
          SELECT c.genre FROM eligible_counts c
          LEFT JOIN (SELECT genre FROM recent_covers GROUP BY genre HAVING COUNT(*)>=4) plentiful USING(genre)
          WHERE plentiful.genre IS NULL
        ), ranked AS (
          SELECT genre,url,ROW_NUMBER() OVER (PARTITION BY genre ORDER BY release_date DESC,url) AS position
          FROM (
            SELECT genre,url,release_date FROM recent_covers WHERE genre NOT IN (SELECT genre FROM sparse_genres)
            UNION ALL
            SELECT s.genre,r.cover_url,MAX(r.release_date)
            FROM sparse_genres s CROSS JOIN work_genres g ON g.genre=s.genre
            CROSS JOIN recordings r ON r.work_id=g.work_id
            WHERE %1 AND %2 GROUP BY s.genre,r.cover_url
          )
        )
        SELECT genre,MAX(n) AS recording_count,json_group_array(url) AS covers_json
        FROM (SELECT genre,url,n FROM ranked JOIN eligible_counts USING(genre) WHERE position<=4 ORDER BY genre,position)
        GROUP BY genre HAVING COUNT(*)=4 ORDER BY recording_count DESC,genre
        )SQL").arg(discoverable("r"), usableCover))) {
        const auto v=value.toMap();
        entries.append(QVariantMap{{"genre",v.value("genre")},{"count",v.value("recording_count")},
            {"covers",QJsonDocument::fromJson(v.value("covers_json").toString().toUtf8()).array().toVariantList()}});
    }
    m_genreMosaic = entries;
    m_genreMosaicDate = today;
    return entries;
}

QVariantMap AudiobookCatalog::genre(const QString& name, int page) const {
    const auto k=validId(name);
    if(!m_ok)return m_remote->read(QStringLiteral("genre"),{k,page},QVariantMap{}).toMap();
    if(k.isEmpty())return {{"items",QVariantList{}},{"page",std::clamp(page,0,100000)},{"hasMore",false}};
    // Large genres can fill their first page from a short release-index scan.
    // Small or unknown genres should read only their matching work IDs.
    const auto summary = select(QStringLiteral("SELECT recording_count FROM genres_summary WHERE genre=?"),{k});
    if (page <= 0 && !summary.isEmpty() && summary.first().toMap().value("recording_count").toInt() >= 1024)
        return localPage(QStringLiteral(
            "EXISTS (SELECT 1 FROM work_genres g WHERE g.work_id=r.work_id AND g.genre=?)"),{k},page);
    return localPage(QStringLiteral("r.work_id IN (SELECT work_id FROM work_genres WHERE genre=?)"),{k},page);
}
