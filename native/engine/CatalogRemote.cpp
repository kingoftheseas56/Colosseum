#include "CatalogRemote.h"
#include "StaticCatalogueResolver.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QTimer>
#include <algorithm>
#include <utility>
namespace {
constexpr int responseCap = 4 * 1024 * 1024;
constexpr int cacheCap = 16 * 1024 * 1024;
constexpr int entryCap = 256;
bool validRevision(const QString& value) { static const QRegularExpression hash(QStringLiteral("^[a-f0-9]{64}$")); return hash.match(value).hasMatch(); }
bool validEtag(const QString& value) {
    if (value.isEmpty() || value.size() > 1024) return false;
    for (const auto character : value) if (character.unicode() < 32 || character.unicode() == 127) return false;
    return true;
}
bool shape(const QJsonValue& result, const QVariant& empty) {
    if (empty.metaType().id() == QMetaType::QVariantList) return result.isArray();
    if (empty.metaType().id() == QMetaType::QVariantMap) return result.isObject();
    if (empty.metaType().id() == QMetaType::Bool) return result.isBool();
    return result.isDouble() && result.toDouble() >= 0 && result.toDouble() == result.toInt(-1);
}
bool validLanding(const QJsonValue& value, const QString& dataset, const QVariantList& args) {
    const auto obj = value.toObject();
    const auto from = QDate::fromString(obj.value("validFrom").toString(), Qt::ISODate);
    const auto until = QDate::fromString(obj.value("validUntil").toString(), Qt::ISODate);
    const auto today = QDateTime::currentDateTimeUtc().date();
    if (obj.value("version") != QJsonValue(1) || obj.value("dataset") != dataset
        || args.size() != 1 || obj.value("tab") != args[0].toString()
        || !from.isValid() || !until.isValid() || today < from || today >= until
        || !obj.value("queries").isArray() || obj.value("queries").toArray().size() > 128) return false;
    QSet<QString> seen;
    for (const auto& item : obj.value("queries").toArray()) {
        const auto q = item.toObject(); const auto method = q.value("method").toString();
        const bool page = dataset == "comics" && method == "discoverPageFiltered";
        const bool flag = dataset == "comics" && method == "curatedReady";
        const bool array = (dataset == "comics" && QStringList{"shelf", "curatedRanked", "curatedGenreShelves"}.contains(method))
            || (dataset == "imdb" && method == "titleCatalog") || (dataset == "mal" && method == "animeCatalog");
        if ((!page && !flag && !array) || !q.value("args").isArray()
            || (page && !q.value("result").isObject()) || (flag && !q.value("result").isBool())
            || (array && !q.value("result").isArray())) return false;
        const auto queryArgs=q.value("args").toArray();
        const auto natural=[](const QJsonValue& v) { return v.isDouble()&&v.toDouble()>=0&&v.toDouble()==v.toInt(-1); };
        if(flag && !queryArgs.isEmpty()) return false;
        if(method=="curatedRanked" && !queryArgs.isEmpty()) return false;
        if(method=="curatedGenreShelves" && (queryArgs.size()!=1||!natural(queryArgs[0]))) return false;
        if(method=="shelf" && (queryArgs.size()!=3||!queryArgs[0].isString()||!queryArgs[1].isString()||!natural(queryArgs[2]))) return false;
        if(method=="titleCatalog"||method=="animeCatalog")
            if(queryArgs.size()!=3||!queryArgs[0].isObject()||!natural(queryArgs[1])||!natural(queryArgs[2])) return false;
        if(page) {
            const auto result=q.value("result").toObject();
            if(queryArgs.size()!=5||!queryArgs[0].isString()||!queryArgs[1].isArray()||!queryArgs[2].isBool()
                ||!natural(queryArgs[3])||!natural(queryArgs[4])||!result.value("items").isArray()
                ||!result.value("exhausted").isBool()||!natural(result.value("nextOffset"))) return false;
        }
        const auto identity = QString::fromUtf8(QJsonDocument(QJsonObject{{"method",method},{"args",q.value("args")}}).toJson(QJsonDocument::Compact));
        if (seen.contains(identity)) return false;
        seen.insert(identity);
    }
    return !seen.isEmpty();
}
}
CatalogRemote::CatalogRemote(QString dataset, QObject* parent) : QObject(parent), m_dataset(std::move(dataset)) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(100);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] { flushSave(); });
}
CatalogRemote::~CatalogRemote() { ++m_generation; if(m_static)for(const auto& query:std::as_const(m_pending))m_static->cancel(query.staticJob);for(const auto& reply : std::as_const(m_replies)) if(reply) reply->abort(); flushSave(true); }
void CatalogRemote::setResultValidator(std::function<bool(const QString&, const QJsonValue&)> validator) { m_validator = std::move(validator); }
bool CatalogRemote::enabled() const { return m_nam && !m_endpoint.isEmpty(); }
bool CatalogRemote::available() const { return enabled()||!m_cache.isEmpty()||(m_static&&m_static->hasLastGood()); }
QString CatalogRemote::key(const QString& method, const QVariantList& args) const {
    const QByteArray bytes = QJsonDocument(QJsonObject{{"method",method},{"args",QJsonArray::fromVariantList(args)}}).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
void CatalogRemote::configure(QNetworkAccessManager* nam, const QUrl& endpoint, const QString& path) {
    // Retire the old writer before switching its path/endpoint. Never let an old
    // snapshot overwrite a subsequently configured source's cache.
    flushSave(true);
    ++m_generation;
    if(m_static)for(const auto& query:std::as_const(m_pending))m_static->cancel(query.staticJob);
    disconnect(m_staticConnection);m_static=nullptr;
    for (const auto& reply : std::as_const(m_replies)) if (reply) reply->abort();
    m_replies.clear(); m_pending.clear(); m_failures.clear(); m_token.clear(); m_leasing=false;
    m_nam=nam; m_path=path; m_endpoint=endpoint; m_cache.clear(); m_sourceRevision.clear(); m_publication.clear(); ++m_sourceGeneration;
    if (!endpoint.isEmpty() && (!endpoint.isValid() || !endpoint.userInfo().isEmpty()
        || !endpoint.query().isEmpty() || !endpoint.fragment().isEmpty()
        || (endpoint.scheme()!="https" && !(endpoint.scheme()=="http" && (endpoint.host()=="127.0.0.1" || endpoint.host()=="localhost"))))) m_endpoint=QUrl();
    if (!m_endpoint.isEmpty()) load();
    if(!m_endpoint.isEmpty()){
        m_static=StaticCatalogueResolver::shared(nam,m_endpoint,path.isEmpty()?QString():QFileInfo(path).absolutePath(),this);
        const auto generation=m_generation;
        m_staticConnection=connect(m_static,&StaticCatalogueResolver::publicationChanged,this,[this,generation](const QString& revision){if(generation==m_generation)adoptPublication(revision);});
        if(m_static->hasLastGood())adoptPublication(m_static->publication());
    }
    ++m_revision; emit sourceChanged(); emit changed();
}
bool CatalogRemote::queue(const QString& id, const QString& method, const QVariantList& args,
                          const QVariant& empty, bool bypassCooldown) {
    if ((!enabled()&&!(m_static&&m_static->hasLastGood())) || m_pending.contains(id) || m_pending.size() >= 128) return false;
    const auto failure=m_failures.constFind(id);
    if (!bypassCooldown && failure!=m_failures.constEnd()
        && failure->when.secsTo(QDateTime::currentDateTimeUtc()) < 30) return false;
    m_pending.insert(id,Query{method,args,empty,false,false});
    const auto generation=m_generation;
    QTimer::singleShot(0,this,[this,id,generation]{
        if (generation==m_generation && m_pending.contains(id)) {
            // Notify after read() has returned: subscribers may re-enter the
            // cache or replace this source. Recheck ownership before dispatch.
            emit queryChanged(id);
            if (generation!=m_generation || !m_pending.contains(id)) return;
            tryStatic(id);
        }
    });
    return true;
}
QVariant CatalogRemote::read(const QString& method, const QVariantList& args, const QVariant& empty, bool force) {
    const auto id=key(method,args);
    const auto cached=m_cache.constFind(id);
    const bool valid=cached!=m_cache.constEnd() && shape(cached->result,empty)
        && (method != "landing" || validLanding(cached->result, m_dataset, args));
    const auto age=valid ? cached->fetched.secsTo(QDateTime::currentDateTimeUtc()) : -1;
    const auto value=valid ? cached->result : QJsonValue();
    if (valid && method == "landing") {
        // The bundle may outlive independently evicted child query entries.
        // Reuse its validated immutable values, without a network trip or
        // synchronous signals from inside read(). Never replace newer children.
        const auto stamp=cached->fetched;
        QSet<QString> workingSet{id};
        for(const auto& item:value.toObject().value("queries").toArray()) {
            const auto child=item.toObject(); const auto childMethod=child.value("method").toString();
            const auto childKey=key(childMethod,child.value("args").toArray().toVariantList());
            workingSet.insert(childKey);
            if(!m_cache.contains(childKey))m_cache.insert(childKey,Entry{childMethod,child.value("result"),stamp,m_sourceRevision,{}});
        }
        bound(workingSet);
    }
    if(force&&m_static)m_static->refresh();
    const bool staticRefresh=m_static&&m_static->needsRefresh()&&!(m_dataset=="comics"&&method=="search");
    if (force || !valid || age<0 || age>=86400 || staticRefresh) queue(id,method,args,empty,false);
    return valid ? value.toVariant() : empty;
}
QVariantMap CatalogRemote::queryState(const QString& method, const QVariantList& args, const QVariant& empty) const {
    const auto id=key(method,args);
    const auto cached=m_cache.constFind(id);
    const bool hasCache=cached!=m_cache.constEnd() && shape(cached->result,empty)
        && (method != "landing" || validLanding(cached->result, m_dataset, args));
    const bool inFlight=m_pending.contains(id);
    const auto failure=m_failures.constFind(id);
    const bool hasFailure=failure!=m_failures.constEnd();
    const QString status=inFlight ? QStringLiteral("pending")
        : hasFailure ? QStringLiteral("failure")
        : hasCache ? QStringLiteral("success") : QStringLiteral("missing");
    return {{QStringLiteral("key"),id},
            // F2 consumers must keep waiting while cached immutable files are
            // assembled, including an offline owner without a network manager.
            {QStringLiteral("enabled"),enabled()||(m_static&&m_static->hasLastGood())},
            {QStringLiteral("status"),status},
            {QStringLiteral("pending"),inFlight},
            {QStringLiteral("waiting"),inFlight && !hasCache},
            {QStringLiteral("cached"),hasCache},
            {QStringLiteral("error"),hasFailure ? failure->reason : QString()},
            {QStringLiteral("retryable"),hasFailure && !inFlight && (enabled()||(m_static&&m_static->hasLastGood())) && m_pending.size()<128}};
}
bool CatalogRemote::retry(const QString& method, const QVariantList& args, const QVariant& empty) {
    const auto id=key(method,args);
    if (!m_failures.contains(id)) return false;
    if(m_static)m_static->refresh();
    return queue(id,method,args,empty,true);
}
bool CatalogRemote::waiting() const {
    for (auto query = m_pending.cbegin(); query != m_pending.cend(); ++query) {
        const auto cached = m_cache.constFind(query.key());
        if (cached == m_cache.constEnd() || !shape(cached->result, query->empty)) return true;
    }
    return false;
}
void CatalogRemote::lease() {
    if(m_leasing || !enabled()) return;
    m_leasing=true;
    QNetworkRequest req(m_endpoint.resolved(QUrl("/v1/guest/session")));
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    observe(m_nam->post(req,QByteArrayLiteral("{\"appId\":\"colosseum\",\"apiVersion\":1}")),{},true);
}
void CatalogRemote::adoptPublication(const QString& revision) {
    if(!validRevision(revision)||m_publication==revision)return;
    const auto keys=m_cache.keys();m_cache.clear();m_failures.clear();m_sourceRevision.clear();m_publication=revision;++m_sourceGeneration;
    save();++m_revision;const auto generation=m_generation;
    emit sourceChanged(); if(generation!=m_generation)return;
    for(const auto& id:keys){emit queryChanged(id);if(generation!=m_generation)return;}
    emit changed();
}
void CatalogRemote::tryStatic(const QString& id) {
    if(!m_pending.contains(id))return;
    if(!m_static){m_pending[id].staticChecked=true;if(m_token.isEmpty())lease();else dispatch(id);return;}
    if(m_pending[id].staticJob)m_static->cancel(m_pending[id].staticJob);
    m_pending[id].staticChecked=false;m_pending[id].dispatched=false;const auto attempt=++m_pending[id].attempt;
    const auto query=m_pending.value(id);const auto generation=m_generation;
    m_pending[id].staticJob=m_static->resolve(m_dataset,query.method,query.args,this,[this,id,attempt,generation](StaticCatalogueResolver::Outcome out){
        if(generation!=m_generation||!m_pending.contains(id)||m_pending[id].attempt!=attempt)return;
        m_pending[id].staticJob=0;
        if(m_static&&!out.publication.isEmpty()&&out.publication!=m_static->publication()){
            if(++m_pending[id].restarts>2){finishFailure(id,"STATIC_REVISION_CHANGED");return;}tryStatic(id);return;
        }
        if(!out.publication.isEmpty())adoptPublication(out.publication);
        if(generation!=m_generation||!m_pending.contains(id)||m_pending[id].attempt!=attempt)return;
        if(out.status==StaticCatalogueResolver::Status::Failure){finishFailure(id,out.error);return;}
        if(out.status==StaticCatalogueResolver::Status::Prepared){complete(id,out.result,out.sourceRevision);return;}
        // New clients fall back to the existing per-query path when an older
        // publication has no bundle. Never POST an unknown method to old Workers.
        if(m_pending[id].method=="landing"){finishFailure(id,"STATIC_NOT_PREPARED");return;}
        m_pending[id].staticChecked=true;m_pending[id].expectedSource=m_dataset=="content"?QString():out.sourceRevision;
        if(!enabled()){finishFailure(id,"REMOTE_DISABLED");return;}
        if(m_token.isEmpty())lease();else dispatch(id);
    });
}
void CatalogRemote::complete(const QString& id,const QJsonValue& result,const QString& revision,const QString& etag) {
    if(!m_pending.contains(id))return;const auto query=m_pending.value(id);
    if(!validRevision(revision)||!shape(result,query.empty)){finishFailure(id,"INCOMPATIBLE_RESPONSE");return;}
    if(query.method=="landing"&&!validLanding(result,m_dataset,query.args)) {
        const auto obj=result.toObject();
        const auto from=QDate::fromString(obj.value("validFrom").toString(),Qt::ISODate);
        const auto until=QDate::fromString(obj.value("validUntil").toString(),Qt::ISODate);
        const auto today=QDateTime::currentDateTimeUtc().date();
        const bool expired=from.isValid()&&until.isValid()&&from<until&&(today<from||today>=until);
        finishFailure(id,expired?"STATIC_NOT_PREPARED":"INVALID_LANDING");return;
    }
    if(m_validator&&!m_validator(query.method,result)){finishFailure(id,"INVALID_RESULT");return;}
    const bool invalidated=!m_sourceRevision.isEmpty()&&m_sourceRevision!=revision;
    if(invalidated){m_cache.clear();m_failures.clear();++m_sourceGeneration;}
    m_sourceRevision=revision;m_pending.remove(id);m_failures.remove(id);
    m_cache.insert(id,Entry{query.method,result,QDateTime::currentDateTimeUtc(),revision,validEtag(etag)?etag:QString()});
    QStringList primed;
    if(query.method=="landing") {
        for(const auto& item:result.toObject().value("queries").toArray()) {
            const auto child=item.toObject(); const auto method=child.value("method").toString();
            const auto childKey=key(method,child.value("args").toArray().toVariantList());
            m_cache.insert(childKey,Entry{method,child.value("result"),QDateTime::currentDateTimeUtc(),revision,{}});
            m_failures.remove(childKey);primed.append(childKey);
        }
    }
    bound();save();++m_revision;
    const auto generation=m_generation;
    if(invalidated){emit sourceChanged();if(generation!=m_generation)return;}
    for(const auto& childKey:primed){emit queryChanged(childKey);if(generation!=m_generation)return;}
    emit queryChanged(id);emit changed();
}
void CatalogRemote::dispatch(const QString& id) {
    if(!enabled() || !m_pending.contains(id) || !m_pending[id].staticChecked || m_pending[id].dispatched) return;
    m_pending[id].dispatched=true;
    const auto cached = m_cache.constFind(id);
    m_pending[id].conditionalEtag = cached != m_cache.constEnd() && validEtag(cached->etag) ? cached->etag : QString();
    const auto query=m_pending.value(id);
    QNetworkRequest req(m_endpoint.resolved(QUrl("/v1/catalogue/"+m_dataset+"/"+query.method)));
    req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
    req.setRawHeader("Authorization",QByteArrayLiteral("Bearer ")+m_token);
    if (!query.conditionalEtag.isEmpty()) req.setRawHeader("If-None-Match", query.conditionalEtag.toLatin1());
    req.setTransferTimeout(15000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    const auto body=QJsonDocument(QJsonObject{{"args",QJsonArray::fromVariantList(query.args)}}).toJson(QJsonDocument::Compact);
    observe(m_nam->post(req,body),id,false);
}
void CatalogRemote::finishFailure(const QString& id,const QString& reason) {
    if (!m_pending.contains(id)) return;
    const auto query=m_pending.take(id);
    m_failures.insert(id,Failure{QDateTime::currentDateTimeUtc(),reason});
    while (m_failures.size() > 512) {
        auto oldest = m_failures.begin();
        for (auto it = m_failures.begin(); it != m_failures.end(); ++it)
            if (it->when < oldest->when) oldest = it;
        m_failures.erase(oldest);
    }
    emit queryChanged(id);
    emit failed(query.method,reason);
}
void CatalogRemote::observe(QNetworkReply* reply,const QString& id,bool session) {
    m_replies.append(reply);
    const auto generation=m_generation;
    const auto sourceGeneration=m_sourceGeneration;
    const int cap=session ? 8192 : responseCap;
    reply->setReadBufferSize(cap+1);
    const auto bytes=QSharedPointer<QByteArray>::create();
    connect(reply,&QNetworkReply::readyRead,this,[reply,bytes,cap]{ bytes->append(reply->readAll()); if(bytes->size()>cap) reply->abort(); });
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,id,session,generation,sourceGeneration,cap]{
        reply->deleteLater();
        if(generation!=m_generation) return;
        m_replies.removeAll(reply);
        if (!session && !m_pending.contains(id)) return; // Ignore late completed-query replies.
        bytes->append(reply->readAll());
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto fail=[&](const QString& reason){
            if(session) { m_leasing=false; const auto keys=m_pending.keys(); for(const auto& k:keys) if(m_pending[k].staticChecked&&!m_pending[k].dispatched) finishFailure(k,reason); }
            else finishFailure(id,reason);
        };
        if(!session && m_pending.contains(id)) m_pending[id].dispatched=false;
        if(!session && sourceGeneration!=m_sourceGeneration && m_pending.contains(id)) { tryStatic(id); return; }
        if(!session && (status==401 || status==409) && m_pending.contains(id) && !m_pending[id].renewed) {
            m_pending[id].renewed=true; m_token.clear(); lease(); return;
        }
        if (!session && status == 304) {
            const auto query = m_pending.value(id);
            auto cached = m_cache.find(id);
            const auto etag = QString::fromLatin1(reply->rawHeader("ETag"));
            if (reply->error() != QNetworkReply::NoError || bytes->size() > cap
                || !m_pending.contains(id) || query.conditionalEtag.isEmpty() || cached == m_cache.end()
                || cached->etag != query.conditionalEtag || (!etag.isEmpty() && etag != cached->etag)
                || !shape(cached->result, query.empty)
                || (m_validator && !m_validator(query.method, cached->result))) {
                fail("UNEXPECTED_NOT_MODIFIED"); return;
            }
            cached->fetched = QDateTime::currentDateTimeUtc();
            m_pending.remove(id); m_failures.remove(id); save(); ++m_revision;
            emit queryChanged(id); emit changed(); return;
        }
        if(reply->error()!=QNetworkReply::NoError || status!=200 || bytes->size()>cap) { fail(bytes->size()>cap ? "RESPONSE_TOO_LARGE" : "HTTP_"+QString::number(status)); return; }
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(*bytes,&error); const auto obj=doc.object();
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) { fail("INVALID_RESPONSE"); return; }
        if(session) {
            m_leasing=false;
            const auto token=obj.value("token").toString().toLatin1();
            const auto expiry=QDateTime::fromString(obj.value("expiresAt").toString(),Qt::ISODate);
            if(token.isEmpty() || token.size()>2048 || !expiry.isValid() || expiry<=QDateTime::currentDateTimeUtc()) { fail("INVALID_LEASE"); return; }
            m_token=token; const auto keys=m_pending.keys(); for(const auto& k:keys) if(m_pending[k].staticChecked)dispatch(k); return;
        }
        if(!m_pending.contains(id)) return;
        const auto query=m_pending.value(id); const auto revision=obj.value("revision").toString();
        if(obj.value("apiVersion")!=QJsonValue(1) || obj.value("method")!=QJsonValue(query.method)
            || !validRevision(revision) || !shape(obj.value("result"),query.empty)) { fail("INCOMPATIBLE_RESPONSE"); return; }
        if(!query.expectedSource.isEmpty()&&query.expectedSource!=revision){if(m_static)m_static->refresh();fail("SOURCE_REVISION_MISMATCH");return;}
        const auto responseEtag = QString::fromLatin1(reply->rawHeader("ETag"));
        complete(id,obj.value("result"),revision,responseEtag);
    });
}
void CatalogRemote::bound(const QSet<QString>& protectedKeys) {
    while(m_cache.size()>entryCap) {
        auto oldest=m_cache.end();
        for(auto it=m_cache.begin();it!=m_cache.end();++it)
            if(!protectedKeys.contains(it.key()) && (oldest==m_cache.end()||it->fetched<oldest->fetched)) oldest=it;
        if(oldest==m_cache.end()) break;
        m_cache.erase(oldest);
    }
}
void CatalogRemote::load() {
    if(m_path.isEmpty()) return;
    QFile file(m_path); if(!file.open(QIODevice::ReadOnly) || file.size()>cacheCap) return;
    const auto obj=QJsonDocument::fromJson(file.readAll()).object();
    if(obj.value("apiVersion")!=QJsonValue(1) || obj.value("dataset")!=QJsonValue(m_dataset) || obj.value("endpoint")!=QJsonValue(m_endpoint.toString())) return;
    m_sourceRevision=obj.value("revision").toString();
    const auto publication=obj.value("publication").toString();if(validRevision(publication))m_publication=publication;
    if(!validRevision(m_sourceRevision)) { m_sourceRevision.clear(); return; }
    const auto entries=obj.value("entries").toObject(); if(entries.size()>entryCap) return;
    for(auto it=entries.begin();it!=entries.end();++it) {
        const auto e=it.value().toObject(); const auto date=QDateTime::fromString(e.value("fetchedAt").toString(),Qt::ISODate);
        if(it.key().size()!=64 || !date.isValid() || e.value("method").toString().isEmpty() || e.value("revision").toString()!=m_sourceRevision || !e.contains("result")) continue;
        if (m_validator && !m_validator(e.value("method").toString(), e.value("result"))) continue;
        const auto etag = e.value("etag").toString();
        m_cache.insert(it.key(),Entry{e.value("method").toString(),e.value("result"),date,e.value("revision").toString(),
                                    validEtag(etag) ? etag : QString()});
    }
}
void CatalogRemote::save() {
    if (m_path.isEmpty()) return;
    m_saveDirty = true;
    ++m_saveVersion;
    m_saveTimer.start();
}
void CatalogRemote::flushSave(bool wait) {
    if (wait) m_saveTimer.stop();
    if (m_saveTask.valid()) {
        if (!wait && m_saveTask.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
            m_saveTimer.start();
            return;
        }
        const auto saved = m_saveTask.get();
        // Only prune the exact snapshot that was saved, never newer entries.
        if (saved.version == m_saveVersion)
            for (const auto& id : saved.evicted) m_cache.remove(id);
        emit cachePersisted(saved.success);
    }
    if (!m_saveDirty || m_path.isEmpty()) return;
    m_saveDirty = false;
    // Implicitly shared value snapshot: serialization, cap enforcement and disk
    // I/O run off the GUI thread. The worker never captures this or a QObject.
    m_saveTask = std::async(std::launch::async,
        [cache = m_cache, path = m_path, dataset = m_dataset,
         endpoint = m_endpoint.toString(), revision = m_sourceRevision,
         publication = m_publication, version = m_saveVersion]() mutable {
        Saved saved; saved.version = version;
        QJsonObject entries;
        for (auto it = cache.cbegin(); it != cache.cend(); ++it)
            entries.insert(it.key(), QJsonObject{{"method", it->method}, {"result", it->result},
                {"fetchedAt", it->fetched.toString(Qt::ISODate)}, {"revision", it->revision}, {"etag", it->etag}});
        const auto serialize = [&] {
            return QJsonDocument(QJsonObject{{"apiVersion", 1}, {"dataset", dataset}, {"endpoint", endpoint},
                {"revision", revision}, {"publication", publication}, {"entries", entries}}).toJson(QJsonDocument::Compact);
        };
        auto bytes = serialize();
        while (bytes.size() > cacheCap && !cache.isEmpty()) {
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it)
                if (it->fetched < oldest->fetched) oldest = it;
            saved.evicted.append(oldest.key()); entries.remove(oldest.key()); cache.erase(oldest);
            bytes = serialize();
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return saved;
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            if (file.write(bytes) == bytes.size()) saved.success = file.commit();
            else file.cancelWriting();
        }
        return saved;
    });
    if (wait) flushSave(true);
    else m_saveTimer.start();
}
