#include "CatalogRemote.h"
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
}
CatalogRemote::CatalogRemote(QString dataset, QObject* parent) : QObject(parent), m_dataset(std::move(dataset)) {}
CatalogRemote::~CatalogRemote() { ++m_generation; for(const auto& reply : std::as_const(m_replies)) if(reply) reply->abort(); }
void CatalogRemote::setResultValidator(std::function<bool(const QString&, const QJsonValue&)> validator) { m_validator = std::move(validator); }
bool CatalogRemote::enabled() const { return m_nam && !m_endpoint.isEmpty(); }
QString CatalogRemote::key(const QString& method, const QVariantList& args) const {
    const QByteArray bytes = QJsonDocument(QJsonObject{{"method",method},{"args",QJsonArray::fromVariantList(args)}}).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
void CatalogRemote::configure(QNetworkAccessManager* nam, const QUrl& endpoint, const QString& path) {
    ++m_generation;
    for (const auto& reply : std::as_const(m_replies)) if (reply) reply->abort();
    m_replies.clear(); m_pending.clear(); m_failures.clear(); m_token.clear(); m_leasing=false;
    m_nam=nam; m_path=path; m_endpoint=endpoint; m_cache.clear(); m_sourceRevision.clear(); ++m_sourceGeneration;
    if (!endpoint.isEmpty() && (!endpoint.isValid() || !endpoint.userInfo().isEmpty()
        || !endpoint.query().isEmpty() || !endpoint.fragment().isEmpty()
        || (endpoint.scheme()!="https" && !(endpoint.scheme()=="http" && (endpoint.host()=="127.0.0.1" || endpoint.host()=="localhost"))))) m_endpoint=QUrl();
    if (!m_endpoint.isEmpty()) load();
    ++m_revision; emit changed();
}
QVariant CatalogRemote::read(const QString& method, const QVariantList& args, const QVariant& empty, bool force) {
    const auto id=key(method,args);
    const auto cached=m_cache.constFind(id);
    const auto now=QDateTime::currentDateTimeUtc();
    const bool valid=cached!=m_cache.constEnd() && shape(cached->result,empty);
    const auto age=valid ? cached->fetched.secsTo(now) : -1;
    const auto failure=m_failures.constFind(id);
    if (enabled() && (force || !valid || age<0 || age>=86400) && !m_pending.contains(id)
        && (failure==m_failures.constEnd() || failure->secsTo(now)>=30) && m_pending.size()<128) {
        m_pending.insert(id,Query{method,args,empty,false,false});
        const auto generation=m_generation;
        QTimer::singleShot(0,this,[this,id,generation]{ if(generation==m_generation && m_pending.contains(id)) { if(m_token.isEmpty()) lease(); else dispatch(id); } });
    }
    return valid ? cached->result.toVariant() : empty;
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
void CatalogRemote::dispatch(const QString& id) {
    if(!enabled() || !m_pending.contains(id) || m_pending[id].dispatched) return;
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
    const auto query=m_pending.take(id); m_failures.insert(id,QDateTime::currentDateTimeUtc());
    while (m_failures.size() > 512) {
        auto oldest = m_failures.begin();
        for (auto it = m_failures.begin(); it != m_failures.end(); ++it) if (it.value() < oldest.value()) oldest = it;
        m_failures.erase(oldest);
    }
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
        bytes->append(reply->readAll());
        const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto fail=[&](const QString& reason){
            if(session) { m_leasing=false; const auto keys=m_pending.keys(); for(const auto& k:keys) if(!m_pending[k].dispatched) finishFailure(k,reason); }
            else finishFailure(id,reason);
        };
        if(!session && m_pending.contains(id)) m_pending[id].dispatched=false;
        if(!session && sourceGeneration!=m_sourceGeneration && m_pending.contains(id)) { dispatch(id); return; }
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
            m_pending.remove(id); m_failures.remove(id); save(); ++m_revision; emit changed(); return;
        }
        if(reply->error()!=QNetworkReply::NoError || status!=200 || bytes->size()>cap) { fail(bytes->size()>cap ? "RESPONSE_TOO_LARGE" : "HTTP_"+QString::number(status)); return; }
        QJsonParseError error; const auto doc=QJsonDocument::fromJson(*bytes,&error); const auto obj=doc.object();
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) { fail("INVALID_RESPONSE"); return; }
        if(session) {
            m_leasing=false;
            const auto token=obj.value("token").toString().toLatin1();
            const auto expiry=QDateTime::fromString(obj.value("expiresAt").toString(),Qt::ISODate);
            if(token.isEmpty() || token.size()>2048 || !expiry.isValid() || expiry<=QDateTime::currentDateTimeUtc()) { fail("INVALID_LEASE"); return; }
            m_token=token; const auto keys=m_pending.keys(); for(const auto& k:keys) dispatch(k); return;
        }
        if(!m_pending.contains(id)) return;
        const auto query=m_pending.value(id); const auto revision=obj.value("revision").toString();
        if(obj.value("apiVersion")!=QJsonValue(1) || obj.value("method")!=QJsonValue(query.method)
            || !validRevision(revision) || !shape(obj.value("result"),query.empty)) { fail("INCOMPATIBLE_RESPONSE"); return; }
        if (m_validator && !m_validator(query.method, obj.value("result"))) { fail("INVALID_RESULT"); return; }
        if (!m_sourceRevision.isEmpty() && m_sourceRevision != revision) {
            m_cache.clear(); ++m_sourceGeneration;
        }
        m_sourceRevision = revision;
        m_pending.remove(id); m_failures.remove(id);
        const auto responseEtag = QString::fromLatin1(reply->rawHeader("ETag"));
        m_cache.insert(id,Entry{query.method,obj.value("result"),QDateTime::currentDateTimeUtc(),revision,
                                validEtag(responseEtag) ? responseEtag : QString()});
        bound(); save(); ++m_revision; emit changed();
    });
}
void CatalogRemote::bound() {
    while(m_cache.size()>entryCap) {
        auto oldest=m_cache.begin(); for(auto it=m_cache.begin();it!=m_cache.end();++it) if(it->fetched<oldest->fetched) oldest=it;
        m_cache.erase(oldest);
    }
}
void CatalogRemote::load() {
    if(m_path.isEmpty()) return;
    QFile file(m_path); if(!file.open(QIODevice::ReadOnly) || file.size()>cacheCap) return;
    const auto obj=QJsonDocument::fromJson(file.readAll()).object();
    if(obj.value("apiVersion")!=QJsonValue(1) || obj.value("dataset")!=QJsonValue(m_dataset) || obj.value("endpoint")!=QJsonValue(m_endpoint.toString())) return;
    m_sourceRevision=obj.value("revision").toString();
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
    if(m_path.isEmpty()) return;
    QJsonObject entries;
    for(auto it=m_cache.cbegin();it!=m_cache.cend();++it) entries.insert(it.key(),QJsonObject{{"method",it->method},{"result",it->result},{"fetchedAt",it->fetched.toString(Qt::ISODate)},{"revision",it->revision},{"etag",it->etag}});
    auto bytes=QJsonDocument(QJsonObject{{"apiVersion",1},{"dataset",m_dataset},{"endpoint",m_endpoint.toString()},{"revision",m_sourceRevision},{"entries",entries}}).toJson(QJsonDocument::Compact);
    while(bytes.size()>cacheCap && !m_cache.isEmpty()) { auto oldest=m_cache.begin(); for(auto it=m_cache.begin();it!=m_cache.end();++it) if(it->fetched<oldest->fetched) oldest=it; entries.remove(oldest.key()); m_cache.erase(oldest); bytes=QJsonDocument(QJsonObject{{"apiVersion",1},{"dataset",m_dataset},{"endpoint",m_endpoint.toString()},{"revision",m_sourceRevision},{"entries",entries}}).toJson(QJsonDocument::Compact); }
    if(!QDir().mkpath(QFileInfo(m_path).absolutePath())) return;
    QSaveFile file(m_path); if(file.open(QIODevice::WriteOnly)) { if(file.write(bytes)==bytes.size()) file.commit(); else file.cancelWriting(); }
}
