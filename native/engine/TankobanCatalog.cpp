// TankobanCatalog.cpp — see header. [Agent 1 sub-exec (Sonnet), Tankoban catalogue independence]
#include "TankobanCatalog.h"
#include "StaticCatalogueResolver.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QSqlQuery>
#include <QVector>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDateTime>
#include <QSharedPointer>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <limits>

#include <algorithm>

namespace {

constexpr int kRemoteApiVersion = 1;
constexpr int kRemoteCacheSchema = 1;
constexpr int kRemoteCacheEntries = 32;
constexpr int kRemoteCacheBytes = 4 * 1024 * 1024;
constexpr int kRemoteResponseBytes = 512 * 1024;
// A persisted entry adds identity/timestamp bookkeeping to the response facts.
constexpr int kRemoteCacheEntryBytes = kRemoteResponseBytes + 1024;
constexpr int kRemoteFreshSeconds = 300;

// Zero-pad each run of digits so a plain lexicographic compare orders
// numerically ("2" before "10"); lowercases everything else for a stable,
// case-insensitive tiebreak on any non-numeric volume label. Mirrors
// VaultIndex::naturalSortKey's technique (native/engine/VaultIndex.cpp).
QString numericAwareKey(const QString& s)
{
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

} // namespace

bool TankobanCatalog::openAt(const QString& dbPath)
{
    // resolve beside the exe first (deployed), then the repo layout (dev run) — MalCatalog's ladder
    QString path = dbPath;
    if (!QFileInfo::exists(path)) {
        const QString beside = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/../../") + dbPath;
        if (QFileInfo::exists(beside)) path = beside;
    }
    if (!QFileInfo::exists(path))
        return false;                             // no catalog — callers fall through honestly
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    m_db.setDatabaseName(path);
    m_db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
    return m_db.open();
}

TankobanCatalog::TankobanCatalog(const QString& dbPath, QObject* parent)
    : QObject(parent), m_conn(QStringLiteral("tankoban_catalog"))
{
    m_ok = openAt(dbPath);
}

TankobanCatalog::~TankobanCatalog()
{
    cancelRequest();
    disconnect(m_staticConnection);m_static=nullptr;
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(m_conn))
        QSqlDatabase::removeDatabase(m_conn);
}

bool TankobanCatalog::reopen(const QString& dbPath)
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

void TankobanCatalog::closeForSwap()
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

QVariantMap TankobanCatalog::seriesInfo(int malId) const
{
    if (!m_ok)
        return m_remoteSeries.value(malId).info;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT volume_count, count_basis FROM series WHERE mal_id = ?"));
    q.addBindValue(malId);
    if (!q.exec() || !q.next())
        return m_remoteSeries.value(malId).info;
    QVariantMap m;
    m.insert(QStringLiteral("volumeCount"), q.value(0).toInt());
    m.insert(QStringLiteral("countBasis"), q.value(1).toString());
    return m;
}

QVariantList TankobanCatalog::volumes(int malId) const
{
    QVariantList out;
    if (!m_ok)
        return m_remoteSeries.value(malId).volumes;
    // A valid local series remains authoritative; remote facts fill only misses.
    QSqlQuery local(m_db);
    local.prepare(QStringLiteral("SELECT 1 FROM series WHERE mal_id = ?"));
    local.addBindValue(malId);
    if (local.exec() && !local.next() && m_remoteSeries.contains(malId))
        return m_remoteSeries.value(malId).volumes;

    const QVariantMap info = seriesInfo(malId);
    if (info.isEmpty())
        return out; // unknown malId — nothing to synthesize or overlay

    const int count = info.value(QStringLiteral("volumeCount")).toInt();

    // Baked cover/name facts keyed by their exact number string.
    QMap<QString, QPair<QString, QString>> overlay;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT number, cover_url, name FROM volumes WHERE mal_id = ?"));
        q.addBindValue(malId);
        if (q.exec()) {
            while (q.next())
                overlay.insert(q.value(0).toString(),
                               qMakePair(q.value(1).toString(), q.value(2).toString()));
        }
    }

    QVector<QVariantMap> rows;
    QSet<QString> emitted;

    // Synthesize "1".."N" from the known count, overlaying baked facts where present.
    for (int n = 1; n <= count; ++n) {
        const QString num = QString::number(n);
        const auto it = overlay.constFind(num);
        QVariantMap m;
        m.insert(QStringLiteral("number"), num);
        m.insert(QStringLiteral("cover"), it != overlay.constEnd() ? it.value().first : QString());
        m.insert(QStringLiteral("name"), it != overlay.constEnd() ? it.value().second : QString());
        rows.append(m);
        emitted.insert(num);
    }

    // Any baked rows outside the synthesized range (e.g. count unknown, or a
    // harvest number the count doesn't cover) still surface — nothing invented,
    // nothing dropped.
    for (auto it = overlay.constBegin(); it != overlay.constEnd(); ++it) {
        if (emitted.contains(it.key()))
            continue;
        QVariantMap m;
        m.insert(QStringLiteral("number"), it.key());
        m.insert(QStringLiteral("cover"), it.value().first);
        m.insert(QStringLiteral("name"), it.value().second);
        rows.append(m);
    }

    std::sort(rows.begin(), rows.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return numericAwareKey(a.value(QStringLiteral("number")).toString())
             < numericAwareKey(b.value(QStringLiteral("number")).toString());
    });

    for (const auto& m : rows)
        out.append(m);
    return out;
}

void TankobanCatalog::configureRemote(QNetworkAccessManager* nam, const QUrl& endpoint,
                                     const QString& cachePath)
{
    cancelRequest();
    disconnect(m_staticConnection);m_static=nullptr;
    m_lease.clear();
    m_nam = nam;
    const auto previousEndpoint = m_endpoint;
    const auto previousCachePath = m_cachePath;
    m_endpoint = QUrl();
    // Production is HTTPS. Loopback HTTP is deliberately limited to test fixtures.
    if (endpoint.isValid() && !endpoint.host().isEmpty() && endpoint.userInfo().isEmpty() && endpoint.query().isEmpty()
        && endpoint.fragment().isEmpty() && endpoint.path().isEmpty()
        && (endpoint.scheme() == QStringLiteral("https")
            || (endpoint.scheme() == QStringLiteral("http")
                && (endpoint.host() == QStringLiteral("127.0.0.1")
                    || endpoint.host() == QStringLiteral("localhost")))))
        m_endpoint = endpoint;
    m_cachePath = m_endpoint.isEmpty() ? QString() : cachePath;
    bool changed = false;
    if ((previousEndpoint != m_endpoint || previousCachePath != m_cachePath)
        && !m_remoteSeries.isEmpty()) {
        m_remoteSeries.clear();
        changed = true;
    }
    if(previousEndpoint!=m_endpoint||previousCachePath!=m_cachePath)m_publication.clear();
    // Load into a temporary map first. Reconfiguring an unchanged source with
    // a corrupt/partial file must not destroy already validated memory facts.
    changed = loadRemoteCache() || changed;
    if(!m_endpoint.isEmpty()){
        m_static=StaticCatalogueResolver::shared(nam,m_endpoint,cachePath.isEmpty()?QString():QFileInfo(cachePath).absolutePath(),this);
        m_staticConnection=connect(m_static,&StaticCatalogueResolver::publicationChanged,this,&TankobanCatalog::adoptPublication);
        if(m_static->hasLastGood())adoptPublication(m_static->publication());
    }
    if (changed) ++m_revision;
    emit revisionChanged();
}

void TankobanCatalog::cancelRequest()
{
    ++m_generation;
    if(m_staticJob&&m_static){m_static->cancel(m_staticJob);m_staticJob=0;}
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

bool TankobanCatalog::hasSeries(int malId) const
{
    return !seriesInfo(malId).isEmpty() || !volumes(malId).isEmpty();
}

void TankobanCatalog::requestSeries(int malId, bool refresh)
{
    cancelRequest();
    if (malId <= 0) { emit seriesFailed(malId, QStringLiteral("INVALID_ID")); return; }
    bool local = false;
    if (m_ok) {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral("SELECT 1 FROM series WHERE mal_id = ? UNION SELECT 1 FROM volumes WHERE mal_id = ? LIMIT 1"));
        query.addBindValue(malId);
        query.addBindValue(malId);
        local = query.exec() && query.next();
    }
    const auto cached = m_remoteSeries.constFind(malId);
    const auto age = cached == m_remoteSeries.constEnd() ? -1
        : cached->fetchedAt.secsTo(QDateTime::currentDateTimeUtc());
    if (local || (!refresh && cached != m_remoteSeries.constEnd()
        && age >= 0 && age < kRemoteFreshSeconds && !(m_static&&m_static->needsRefresh()))) {
        emit seriesChanged(malId);
        return;
    }
    if ((!m_nam&&!(m_static&&m_static->hasLastGood())) || m_endpoint.isEmpty()) {
        if (cached != m_remoteSeries.constEnd()) emit seriesChanged(malId);
        else emit seriesFailed(malId, QStringLiteral("REMOTE_DISABLED"));
        return;
    }
    const auto generation=m_generation;
    if(refresh&&m_static)m_static->refresh();
    if(m_static){
        m_staticJob=m_static->resolve("tankoban","series",{malId},this,[this,malId,generation](StaticCatalogueResolver::Outcome out){
            if(generation!=m_generation)return;m_staticJob=0;
            if(out.status==StaticCatalogueResolver::Status::Failure){emit seriesFailed(malId,out.error);return;}
            if(out.status==StaticCatalogueResolver::Status::Prepared){
                if(m_static&&out.publication!=m_static->publication()){emit seriesFailed(malId,"STATIC_REVISION_CHANGED");return;}
                adoptPublication(out.publication);if(generation!=m_generation)return;
                installRemoteSeries(malId,out.result.toObject(),out.publication);return;
            }
            if(!m_nam){emit seriesFailed(malId,"REMOTE_DISABLED");return;}
            if(m_lease.isEmpty())issueLease(malId,generation);else fetchRemoteSeries(malId,generation,true);
        });
    }else if(m_lease.isEmpty())issueLease(malId,generation);else fetchRemoteSeries(malId,generation,true);
}

void TankobanCatalog::adoptPublication(const QString& revision) {
    if(revision.isEmpty()||m_publication==revision)return;
    const auto ids=m_remoteSeries.keys();m_remoteSeries.clear();m_publication=revision;saveRemoteCache();++m_revision;emit revisionChanged();
    const auto generation=m_generation;for(const auto id:ids){emit seriesChanged(id);if(generation!=m_generation)return;}
}

void TankobanCatalog::installRemoteSeries(int malId,const QJsonObject& root,const QString& publication) {
    if(QJsonDocument(root).toJson(QJsonDocument::Compact).size()>kRemoteResponseBytes){emit seriesFailed(malId,"RESPONSE_TOO_LARGE");return;}
    if(root.value("apiVersion")!=QJsonValue(kRemoteApiVersion)||root.value("ready")!=QJsonValue(true)
       ||(root.contains("malId")&&root.value("malId")!=QJsonValue(malId))){emit seriesFailed(malId,"INCOMPATIBLE_RESPONSE");return;}
    RemoteSeries series;QString reason;if(!validateRemoteSeries(root,&series,&reason)){emit seriesFailed(malId,reason);return;}
    if(!publication.isEmpty())m_publication=publication;
    series.fetchedAt=QDateTime::currentDateTimeUtc();m_remoteSeries.insert(malId,series);boundRemoteCache(malId);saveRemoteCache();++m_revision;emit revisionChanged();emit seriesChanged(malId);
}

void TankobanCatalog::issueLease(int malId, quint64 generation)
{
    QNetworkRequest request(m_endpoint.resolved(QUrl(QStringLiteral("/v1/guest/session"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    auto* reply = m_nam->post(request, QByteArrayLiteral("{\"appId\":\"colosseum\",\"apiVersion\":1}"));
    readReply(reply, malId, generation, true, false);
}

void TankobanCatalog::fetchRemoteSeries(int malId, quint64 generation, bool renewLease)
{
    QNetworkRequest request(m_endpoint.resolved(QUrl(QStringLiteral("/v1/tankoban/series/%1").arg(malId))));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_lease);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    readReply(m_nam->get(request), malId, generation, false, renewLease);
}

void TankobanCatalog::readReply(QNetworkReply* reply, int malId, quint64 generation,
                               bool issuingLease, bool renewLease)
{
    m_reply = reply;
    const int cap = issuingLease ? 8192 : kRemoteResponseBytes;
    reply->setReadBufferSize(cap + 1);
    auto bytes = QSharedPointer<QByteArray>::create();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes, cap] {
        if (reply->isOpen()) bytes->append(reply->readAll());
        if (bytes->size() > cap) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, bytes, cap, malId, generation, issuingLease, renewLease] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply = nullptr;
        if (reply->isOpen()) bytes->append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!issuingLease && (status == 401 || status == 409) && renewLease) {
            m_lease.clear();
            issueLease(malId, generation);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status != 200 || bytes->size() > cap) {
            emit seriesFailed(malId, bytes->size() > cap ? QStringLiteral("RESPONSE_TOO_LARGE")
                : QStringLiteral("HTTP_%1").arg(status));
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(*bytes, &error);
        const auto root = doc.object();
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            emit seriesFailed(malId, QStringLiteral("INVALID_RESPONSE")); return;
        }
        if (issuingLease) {
            const auto token = root.value(QStringLiteral("token")).toString().toLatin1();
            const auto expiry = QDateTime::fromString(root.value(QStringLiteral("expiresAt")).toString(), Qt::ISODate);
            if (token.isEmpty() || token.size() > 2048 || !expiry.isValid()
                || expiry <= QDateTime::currentDateTimeUtc()) {
                emit seriesFailed(malId, QStringLiteral("INVALID_LEASE")); return;
            }
            m_lease = token;
            fetchRemoteSeries(malId, generation, false);
            return;
        }
        installRemoteSeries(malId,root);
    });
}

bool TankobanCatalog::validateRemoteSeries(const QJsonObject& root, RemoteSeries* result,
                                           QString* reason)
{
    const auto info = root.value(QStringLiteral("seriesInfo"));
    const auto rows = root.value(QStringLiteral("volumes"));
    const auto hash = root.value(QStringLiteral("sourceHash")).toString();
    static const QRegularExpression hashPattern(QStringLiteral("^[a-f0-9]{64}$"));
    if (!hashPattern.match(hash).hasMatch() || !info.isObject() || !rows.isArray()
        || rows.toArray().size() > 2000) {
        *reason = QStringLiteral("INCOMPATIBLE_RESPONSE");
        return false;
    }
    const auto object = info.toObject();
    const double count = object.value(QStringLiteral("volumeCount")).toDouble(-1);
    const auto basis = object.value(QStringLiteral("countBasis"));
    if (!object.isEmpty() && (count < 0 || count > 2000 || count != int(count)
        || !basis.isString() || basis.toString().size() > 128)) {
        *reason = QStringLiteral("INVALID_SERIES");
        return false;
    }
    QSet<QString> numbers;
    QVariantList volumes;
    for (const auto row : rows.toArray()) {
        const auto volume = row.toObject();
        const auto number = volume.value(QStringLiteral("number")).toString();
        const auto cover = volume.value(QStringLiteral("cover"));
        const auto name = volume.value(QStringLiteral("name"));
        if (!row.isObject() || number.isEmpty() || number.size() > 64 || numbers.contains(number)
            || !cover.isString() || cover.toString().size() > 8192
            || !name.isString() || name.toString().size() > 4096) {
            *reason = QStringLiteral("INVALID_VOLUMES");
            return false;
        }
        numbers.insert(number);
        // Only catalogue facts cross the durable boundary. Unknown response
        // fields (including accidental credentials) never enter this owner.
        volumes.append(QVariantMap{{QStringLiteral("number"), number},
            {QStringLiteral("cover"), cover.toString()}, {QStringLiteral("name"), name.toString()}});
    }
    if (object.isEmpty() && !volumes.isEmpty()) {
        *reason = QStringLiteral("INVALID_SERIES");
        return false;
    }
    result->info = object.isEmpty() ? QVariantMap()
        : QVariantMap{{QStringLiteral("volumeCount"), int(count)},
                      {QStringLiteral("countBasis"), basis.toString()}};
    result->volumes = volumes;
    result->sourceHash = hash;
    return true;
}

bool TankobanCatalog::loadRemoteCache()
{
    if (m_cachePath.isEmpty() || m_endpoint.isEmpty()) return false;
    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kRemoteCacheBytes)
        return false;
    const auto bytes = file.read(kRemoteCacheBytes + 1);
    if (bytes.size() > kRemoteCacheBytes || !file.atEnd()) return false;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto root = document.object();
    if (root.value(QStringLiteral("schemaVersion")) != QJsonValue(kRemoteCacheSchema)
        || root.value(QStringLiteral("apiVersion")) != QJsonValue(kRemoteApiVersion)
        || root.value(QStringLiteral("endpoint")).toString() != m_endpoint.toString(QUrl::FullyEncoded)
        || !root.value(QStringLiteral("entries")).isArray()) return false;
    const auto entries = root.value(QStringLiteral("entries")).toArray();
    if (entries.size() > kRemoteCacheEntries) return false;
    QHash<int, RemoteSeries> restored;
    const auto now = QDateTime::currentDateTimeUtc();
    for (const auto item : entries) {
        if (!item.isObject()) return false;
        const auto entry = item.toObject();
        const double id = entry.value(QStringLiteral("malId")).toDouble(-1);
        if (id <= 0 || id > std::numeric_limits<int>::max() || id != int(id)
            || restored.contains(int(id))
            || QJsonDocument(entry).toJson(QJsonDocument::Compact).size() > kRemoteCacheEntryBytes)
            return false;
        RemoteSeries series;
        QString reason;
        if (!validateRemoteSeries(entry, &series, &reason)) return false;
        series.fetchedAt = QDateTime::fromString(entry.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODateWithMs).toUTC();
        if (!series.fetchedAt.isValid() || series.fetchedAt > now.addSecs(kRemoteFreshSeconds))
            return false;
        restored.insert(int(id), series);
    }
    // A previous failed write may leave newer valid facts only in memory.
    // Reconfiguring the same source must not roll them back to an older file.
    for (auto it = m_remoteSeries.constBegin(); it != m_remoteSeries.constEnd(); ++it) {
        const auto saved = restored.constFind(it.key());
        if (saved == restored.constEnd() || it->fetchedAt >= saved->fetchedAt)
            restored.insert(it.key(), it.value());
    }
    // Publish only after the whole document validates; no partial startup state.
    m_remoteSeries = restored;
    const auto publication=root.value("publication").toString();static const QRegularExpression publicationHash(QStringLiteral("^[a-f0-9]{64}$"));if(publicationHash.match(publication).hasMatch())m_publication=publication;
    boundRemoteCache();
    return !restored.isEmpty();
}

QByteArray TankobanCatalog::remoteCacheBytes() const
{
    auto ids = m_remoteSeries.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray entries;
    for (int id : ids) {
        const auto& series = m_remoteSeries[id];
        entries.append(QJsonObject{{QStringLiteral("malId"), id},
            {QStringLiteral("sourceHash"), series.sourceHash},
            {QStringLiteral("fetchedAt"), series.fetchedAt.toString(Qt::ISODateWithMs)},
            {QStringLiteral("seriesInfo"), QJsonObject::fromVariantMap(series.info)},
            {QStringLiteral("volumes"), QJsonArray::fromVariantList(series.volumes)}});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), kRemoteCacheSchema},
        {QStringLiteral("apiVersion"), kRemoteApiVersion},
        {QStringLiteral("endpoint"), m_endpoint.toString(QUrl::FullyEncoded)},
        {QStringLiteral("publication"), m_publication},
        {QStringLiteral("entries"), entries}}).toJson(QJsonDocument::Compact);
}

void TankobanCatalog::boundRemoteCache(int protectedMalId)
{
    // At most 32 series and 4 MiB on disk or in the serializable memory cache.
    // Deterministic oldest-success eviction never discards the refreshed entry
    // just because QHash happened to place it first.
    while (m_remoteSeries.size() > kRemoteCacheEntries || remoteCacheBytes().size() > kRemoteCacheBytes) {
        auto oldest = m_remoteSeries.end();
        for (auto it = m_remoteSeries.begin(); it != m_remoteSeries.end(); ++it) {
            if (it.key() == protectedMalId) continue;
            if (oldest == m_remoteSeries.end() || it->fetchedAt < oldest->fetchedAt
                || (it->fetchedAt == oldest->fetchedAt && it.key() < oldest.key()))
                oldest = it;
        }
        if (oldest == m_remoteSeries.end()) break;
        m_remoteSeries.erase(oldest);
    }
}

void TankobanCatalog::saveRemoteCache() const
{
    if (m_cachePath.isEmpty() || m_endpoint.isEmpty()) return;
    if (!QDir().mkpath(QFileInfo(m_cachePath).absolutePath())) {
        qWarning("Tankoban catalogue cache directory is unavailable");
        return;
    }
    const auto bytes = remoteCacheBytes();
    QSaveFile file(m_cachePath);
    // Never use direct-write fallback: failed/partial writes leave the previous
    // complete file intact. A later successful fetch retries persistence.
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        qWarning("Tankoban catalogue cache could not be saved atomically");
}
