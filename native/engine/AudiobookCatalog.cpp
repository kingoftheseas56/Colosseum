#include "AudiobookCatalog.h"
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUrl>
#include <algorithm>

namespace {
QVariantMap workRow(const QVariantMap& row) {
    return {{"id",row.value("id")},{"title",row.value("title")},
            {"sortTitle",row.value("sort_title")},
            {"originalLanguage",row.value("original_language")},
            {"firstYear",row.value("first_year")}};
}
QVariantMap recordingRow(const QVariantMap& row) {
    return {{"id",row.value("id")},{"workId",row.value("work_id")},
            {"language",row.value("language")},{"runtimeMin",row.value("runtime_min")},
            {"releaseYear",row.value("release_year")},{"abridgement",row.value("abridgement")},
            {"asin",row.value("asin")},{"isbn",row.value("isbn")}};
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
               query.value(0).toInt()==1;
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
    m_ok=open(sqlitePath);
    if(m_ok!=before || m_ok) emit readyChanged();
    return m_ok;
}
void AudiobookCatalog::configureRemote(QNetworkAccessManager* manager, const QUrl& endpoint,
                                       const QString& cachePath) {
    m_remote->configure(manager,endpoint,cachePath);
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
        "WHERE search_fts MATCH ? AND search_fts.kind='work' "
        "ORDER BY bm25(search_fts),w.title COLLATE NOCASE,w.id LIMIT ?"),
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
        "SELECT w.id,w.title,ws.number_text,ws.position_sort FROM work_series ws "
        "JOIN works w ON w.id=ws.work_id WHERE ws.series_id=? "
        "ORDER BY (ws.position_sort IS NULL),ws.position_sort,w.title COLLATE NOCASE,w.id"),{key})) {
        const auto r=item.toMap();
        items.append(QVariantMap{{"id",r.value("id")},{"title",r.value("title")},
                 {"numberText",r.value("number_text")},{"positionSort",r.value("position_sort")}});
    }
    return {{"id",s.value("id")},{"name",s.value("name")},{"works",items}};
}
QVariantList AudiobookCatalog::latest(int page) const {
    if(!m_ok) return m_remote->read(QStringLiteral("latest"),{page},QVariantList{}).toList();
    QVariantList items;
    for(const QVariant& item:select(QStringLiteral(
        "SELECT r.*,w.title FROM recordings r JOIN works w ON w.id=r.work_id "
        "ORDER BY (r.release_year IS NULL),r.release_year DESC,r.work_id,r.id LIMIT 24 OFFSET ?"),
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
        "SELECT DISTINCT w.* FROM works w WHERE "
        "EXISTS(SELECT 1 FROM work_authors a WHERE a.work_id=w.id AND a.person_id=?) "
        "OR EXISTS(SELECT 1 FROM recording_narrators n WHERE n.work_id=w.id AND n.person_id=?) "
        "ORDER BY (w.first_year IS NULL),w.first_year DESC,w.title COLLATE NOCASE,w.id LIMIT 24 OFFSET ?"),
        {key,key,pageOffset(page)})) items.append(workRow(value.toMap()));
    return items;
}
