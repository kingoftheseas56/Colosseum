#include "server1/http/NzbRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/nzb/NzbSource.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <cstring>
#include <algorithm>
#include <map>
namespace server1::http {
namespace {
using namespace server1::nzb;
struct Request;
struct Session {QHash<QString,std::shared_ptr<NzbSource>> files;QString location;};
struct Registry {void *router=nullptr;int mounts=0;std::function<quint16()> localPort;std::shared_ptr<ArticleClient> client=std::make_shared<ArticleClient>();QHash<QString,std::shared_ptr<Session>> sessions;QSet<QString> failedKeys;QHash<QString,QList<std::weak_ptr<Request>>> waiting;};
void body(void *response,int status,const QByteArray &bytes){server1_http_response_set_status(response,status);if(status>=400&&!bytes.isEmpty()){server1_http_response_set_header(response,"content-type","text/html; charset=utf-8");server1_http_response_set_header(response,"content-length",QByteArray::number(bytes.size()).constData());}server1_http_response_set_body(response,bytes.constData(),size_t(bytes.size()));}
void redirect(void *response,const QString &location){server1_http_response_set_header(response,"location",location.toUtf8().constData());const auto bytes="Found. Redirecting to "+location.toUtf8();server1_http_response_set_header(response,"content-type","text/plain; charset=utf-8");server1_http_response_set_header(response,"content-length",QByteArray::number(bytes.size()).constData());body(response,302,bytes);}
QString videoName(const QString &subject){static const QRegularExpression re(QStringLiteral(R"REGEX(^((.*)(\.(MKV|MP4|AVI|TS)))$|"((.*)(\.(MKV|MP4|AVI|TS)))")REGEX"),QRegularExpression::CaseInsensitiveOption);auto m=re.match(subject);return !m.hasMatch()?QString():m.captured(1).isEmpty()?m.captured(5):m.captured(1);}
QString archiveName(const QString &subject){static const QRegularExpression re(QStringLiteral(R"REGEX((?:^|")([^"\r\n]+\.(?:rar|r\d{1,5}|7z(?:\.\d{1,5})?|zip|z\d{1,5}|tar(?:\.\d{1,5})?|gz(?:\.\d{1,5})?|tgz(?:\.\d{1,5})?))(?:$|"))REGEX"),QRegularExpression::CaseInsensitiveOption);auto m=re.match(subject);return m.hasMatch()?m.captured(1):QString();}
QString familyFor(const QString &name){auto n=name.toLower();if(QRegularExpression("[.]7z([.][0-9]+)?$").match(n).hasMatch())return "7zip";if(QRegularExpression("[.](zip|z[0-9]+)$").match(n).hasMatch())return "zip";if(QRegularExpression("[.](rar|r[0-9]+)$").match(n).hasMatch())return "rar";if(QRegularExpression("[.]tar([.][0-9]+)?$").match(n).hasMatch())return "tar";return "tgz";}
int archiveOrder(const QString &family,const QString &name){auto n=name.toLower();if(family=="rar"&&n.endsWith(".rar")&&!n.contains(".part"))return -1;QRegularExpression re(family=="rar"?"(?:[.]part|[.]r)([0-9]+)(?:[.]rar)?$":family=="zip"?"[.]z([0-9]+)$":"[.]([0-9]+)$");auto m=re.match(n);return m.hasMatch()?m.captured(1).toInt():family=="zip"?99999:0;}
struct Request:std::enable_shared_from_this<Request>{
    std::shared_ptr<Registry> registry;std::shared_ptr<Session> session;QString key,method,filename,range,origin,archiveFamily;bool creating=false,lz=false,trial=false,raceWon=false;int pendingTrials=0;QList<std::shared_ptr<Request>> trials;std::function<void(std::shared_ptr<Request>,bool)> trialDone;
    void *deferred=nullptr,*wake=nullptr;QNetworkAccessManager network;QPointer<QNetworkReply> reply;QByteArray xml,buffer;qsizetype bufferAt=0;
    QList<Server> servers;QStringList urls;int urlIndex=0,fileIndex=0;QList<File> candidates;QStringList names;
    std::shared_ptr<NzbSource> source;quint64 token=0;qint64 offset=0,remaining=0;bool pending=false,failed=false;
    ~Request(){if(reply){QObject::disconnect(reply,nullptr,nullptr,nullptr);reply->abort();delete reply;}if(source&&token)source->cancel(token);server1_http_stream_wake_destroy(wake);server1_http_deferred_destroy(deferred);}
    void finish(int status,const QByteArray &bytes={}){auto response=server1_http_deferred_response(deferred);if(!response)return;body(response,status,bytes);server1_http_deferred_complete(deferred);}
    void failInitialization(const QByteArray &error){registry->sessions.remove(key);registry->failedKeys.insert(key);finish(500,error);auto waiting=registry->waiting.take(key);for(auto weak:waiting)if(auto request=weak.lock())request->resolve();}
    void publish(){
        if(trial){if(trialDone)trialDone(shared_from_this(),true);return;}
        if(!server1_http_deferred_response(deferred))return;
        session->location="/nzb/stream/"+QString::fromLatin1(QUrl::toPercentEncoding(key))+"/"+QString::fromLatin1(QUrl::toPercentEncoding(names.first()));
        registry->sessions.insert(key,session);
        if(!archiveFamily.isEmpty()){
            const auto port=registry->localPort?registry->localPort():0;if(!port||!registry->router){failInitialization("Archive initialization requires the bound local listener");return;}
            QJsonArray urls;for(const auto &name:names)urls.append(QJsonObject{{"url",QString("http://127.0.0.1:%1/nzb/stream/%2/%3").arg(port).arg(QString::fromLatin1(QUrl::toPercentEncoding(key)),QString::fromLatin1(QUrl::toPercentEncoding(name)))},{"bytes",session->files[name]->size()}});
            auto payload=QJsonDocument(urls).toJson(QJsonDocument::Compact);auto target="/"+archiveFamily.toUtf8()+"/create/"+QUrl::toPercentEncoding(key);auto raw="POST "+target+" HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload;auto parser=server1_http_parser_create(size_t(raw.size()+1));void *result=nullptr;if(server1_http_parser_feed(parser,raw.constData(),size_t(raw.size()))==1)result=server1_http_router_dispatch_parsed(registry->router,parser);server1_http_parser_destroy(parser);
            bool ok=result&&!server1_http_response_is_deferred(result)&&server1_http_response_status(result)==200; if(result)server1_http_response_destroy(result);if(!ok){failInitialization("Archive initialization failed");return;}
            session->location="/"+archiveFamily+"/stream?key="+QString::fromLatin1(QUrl::toPercentEncoding(key));
        }
        auto response=server1_http_deferred_response(deferred);
        if(lz){redirect(response,session->location);server1_http_deferred_complete(deferred);}else{auto bytes=QJsonDocument(QJsonObject{{"key",key}}).toJson(QJsonDocument::Compact);server1_http_response_set_header(response,"content-type","application/json");server1_http_response_set_header(response,"content-length",QByteArray::number(bytes.size()).constData());finish(200,bytes);}
        auto waiting=registry->waiting.take(key);for(auto weak:waiting)if(auto request=weak.lock())request->resolve();
    }
    void openNext(){
        if(fileIndex==candidates.size()){publish();return;}
        source=std::make_shared<NzbSource>(nullptr,registry->client);auto current=source;auto name=names[fileIndex];std::weak_ptr<Request> weak=shared_from_this();
        current->open(servers,candidates[fileIndex],[weak,weakSource=std::weak_ptr<NzbSource>(current),name](QString error){auto self=weak.lock();auto current=weakSource.lock();if(!self||!current)return;if(!error.isEmpty()||current->size()<=0){self->fetchUrl();return;}self->session->files.insert(name,current);++self->fileIndex;self->openNext();});
    }
    void select(){
        QString error;auto files=parse(xml,&error);if(!error.isEmpty()){fetchUrl();return;}
        candidates.clear();names.clear();fileIndex=0;session=std::make_shared<Session>();
        archiveFamily.clear();QList<QPair<QString,File>> archives;
        for(const auto &file:files)for(const auto &segment:file){auto name=archiveName(segment.subject);if(!name.isEmpty()){archives.append({name,file});break;}}
        if(!archives.isEmpty()){archiveFamily=familyFor(archives.first().first);std::stable_sort(archives.begin(),archives.end(),[this](const auto&a,const auto&b){return archiveOrder(archiveFamily,a.first)<archiveOrder(archiveFamily,b.first);});for(const auto &entry:archives)if(familyFor(entry.first)==archiveFamily){names.append(entry.first);candidates.append(entry.second);}}
        else for(const auto &file:files){QString name;for(const auto &segment:file){name=videoName(segment.subject);if(!name.isEmpty())break;}if(!name.isEmpty()){candidates.append(file);names.append(name);}}
        if(candidates.isEmpty()) {discoverNames(std::make_shared<QList<File>>(std::move(files)),0);return;}
        openNext();
    }
    void discoverNames(std::shared_ptr<QList<File>> files,int index){
        if(index==files->size()){if(candidates.isEmpty())fetchUrl();else openNext();return;}
        source=std::make_shared<NzbSource>(nullptr,registry->client);auto current=source;std::weak_ptr<Request> weak=shared_from_this();
        current->open(servers,files->at(index),[weak,files,index,weakSource=std::weak_ptr<NzbSource>(current)](QString error){auto self=weak.lock();auto current=weakSource.lock();if(!self||!current)return;if(!error.isEmpty()){self->fetchUrl();return;}auto archive=archiveName(current->name());auto name=archive.isEmpty()?videoName(current->name()):archive;if(!archive.isEmpty()&&self->archiveFamily.isEmpty()){self->archiveFamily=familyFor(archive);self->names.clear();self->candidates.clear();}if(!self->archiveFamily.isEmpty()&&(archive.isEmpty()||familyFor(archive)!=self->archiveFamily))name.clear();if(!name.isEmpty()){self->names.append(name);self->candidates.append(files->at(index));}self->discoverNames(files,index+1);});
    }
    void startBatch(){
        if(raceWon)return;
        if(urlIndex>=urls.size()){registry->failedKeys.insert(key);finish(500,"All nzb files failed checks, cannot play");auto waiting=registry->waiting.take(key);for(auto weak:waiting)if(auto request=weak.lock())request->resolve();return;}
        pendingTrials=qMin(5,urls.size()-urlIndex);trials.clear();std::weak_ptr<Request> weak=shared_from_this();
        for(int i=0;i<pendingTrials;++i){auto candidate=std::make_shared<Request>();candidate->registry=registry;candidate->key=key;candidate->servers=servers;candidate->urls={urls[urlIndex++]};candidate->trial=true;
            candidate->trialDone=[weak](std::shared_ptr<Request> winner,bool ok){auto self=weak.lock();if(!self||self->raceWon)return;if(ok){self->raceWon=true;self->session=winner->session;self->names=winner->names;self->archiveFamily=winner->archiveFamily;self->trials.clear();self->publish();}else if(!--self->pendingTrials){self->trials.clear();QTimer::singleShot(0,&self->network,[weak]{if(auto self=weak.lock())self->startBatch();});}};trials.append(candidate);}
        const auto launch=trials;for(auto candidate:launch){if(raceWon)break;candidate->fetchUrl();}
    }
    void fetchUrl(){
        if(!trial){startBatch();return;}
        source.reset();session.reset();
        if(urlIndex>=urls.size()){if(trialDone)trialDone(shared_from_this(),false);return;}
        auto url=QUrl(urls[urlIndex++]);if((url.scheme()!="http"&&url.scheme()!="https")||url.host().isEmpty()){fetchUrl();return;}
        xml.clear();QNetworkRequest request(url);request.setTransferTimeout(20000);request.setMaximumRedirectsAllowed(3);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        reply=network.get(request);reply->setReadBufferSize(1024*1024);auto active=reply;std::weak_ptr<Request> weak=shared_from_this();
        QObject::connect(reply,&QNetworkReply::readyRead,reply,[weak,active]{if(auto self=weak.lock()){self->xml+=active->readAll();if(self->xml.size()>16*1024*1024){active->abort();}}});
        QObject::connect(reply,&QNetworkReply::finished,reply,[weak,active]{auto self=weak.lock();if(!self)return;self->xml+=active->readAll();const bool ok=active->error()==QNetworkReply::NoError&&self->xml.size()<=16*1024*1024;self->reply=nullptr;active->deleteLater();if(ok)self->select();else self->fetchUrl();});
    }
    void resolve(){if(registry->failedKeys.contains(key)){finish(500,"Cannot stream this nzb file");return;}auto found=registry->sessions.constFind(key);if(found==registry->sessions.cend())return;session=*found;auto response=server1_http_deferred_response(deferred);if(!response)return;if(filename.isEmpty()){redirect(response,session->location);server1_http_deferred_complete(deferred);return;}source=session->files.value(filename);if(!source){finish(404);return;}serve();}
    void serve(){
        const auto size=source->size();qint64 start=0,end=size-1;bool partial=!range.isEmpty();
        if(partial){auto text=range;if(text.startsWith("bytes="))text.remove(0,6);auto parts=text.split('-');bool startOk=false,endOk=false;start=parts.value(0).toLongLong(&startOk);end=parts.value(1).isEmpty()?size-1:parts.value(1).toLongLong(&endOk);if(parts.value(1).isEmpty())endOk=true;if(startOk&&!endOk)end=size-1;else if(!startOk&&endOk){start=size-end;end=size-1;}else if(!startOk){finish(416);return;}
            if(start<0||end<start||start>=size||end>=size){auto response=server1_http_deferred_response(deferred);if(response)server1_http_response_set_header(response,"content-range",("bytes */"+QByteArray::number(size)).constData());finish(416);return;}}
        auto response=server1_http_deferred_response(deferred);if(!response)return;offset=start;remaining=end-start+1;
        server1_http_response_set_status(response,200);server1_http_response_set_body(response,nullptr,0);server1_http_response_set_header(response,"accept-ranges","bytes");server1_http_response_set_header(response,"content-length",QByteArray::number(remaining).constData());
        static const std::map<std::string,std::string> types={
#include "TorrentMime.inc"
        };auto found=types.find(filename.section('.',-1).toLower().toStdString());const char *mime=found==types.end()?"application/octet-stream":found->second.c_str();
        server1_http_response_set_header(response,"content-type",mime);server1_http_response_set_header(response,"access-control-allow-origin",origin.isEmpty()?"*":origin.toUtf8().constData());
        server1_http_response_set_header(response,"transferMode.dlna.org","Streaming");server1_http_response_set_header(response,"contentFeatures.dlna.org","DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000");
        if(partial)server1_http_response_set_header(response,"content-range",QString("bytes %1-%2/%3").arg(start).arg(end).arg(size).toUtf8().constData());
        if(method=="HEAD"){server1_http_deferred_complete(deferred);return;}
        auto context=new std::shared_ptr<Request>(shared_from_this());
        if(!server1_http_response_set_async_stream(response,[](void *context,void *out,size_t count){return (*static_cast<std::shared_ptr<Request>*>(context))->pull(out,count);},[](void *context){delete static_cast<std::shared_ptr<Request>*>(context);},context,size_t(remaining))){delete context;finish(500);return;}
        wake=server1_http_response_stream_wake_handle(response);server1_http_deferred_complete(deferred);
    }
    std::ptrdiff_t pull(void *out,size_t capacity){
        if(!capacity)return 0;
        if(bufferAt<buffer.size()){auto count=qMin<qsizetype>(buffer.size()-bufferAt,qsizetype(qMin<size_t>(capacity,65536)));std::memcpy(out,buffer.constData()+bufferAt,size_t(count));bufferAt+=count;if(bufferAt==buffer.size()){buffer.clear();bufferAt=0;}return count;}
        if(failed)return -1;if(!remaining)return 0;
        if(!pending){pending=true;std::weak_ptr<Request> weak=shared_from_this();const auto count=qMin<qint64>(65536,remaining);token=source->read(offset,count,[weak,count](archive::ReadResult result){auto self=weak.lock();if(!self)return;self->token=0;self->pending=false;self->failed=!result.error.isEmpty()||result.bytes.size()!=count;if(!self->failed){self->buffer=std::move(result.bytes);self->offset+=count;self->remaining-=count;}server1_http_stream_wake_notify(self->wake);});}
        return SERVER1_HTTP_WOULD_BLOCK;
    }
};
struct Context{std::shared_ptr<Registry> registry;bool create=false;};
int handle(const void *request,void *response,server1_http_next_fn,void *,void *context){
    auto ctx=static_cast<Context*>(context);auto registry=ctx->registry;auto method=QString::fromUtf8(server1_http_request_method(request));auto query=[request](const char *key){return QString::fromUtf8(server1_http_request_query_value(request,key));};
    auto pending=std::make_shared<Request>();pending->registry=registry;pending->method=method;
    if(ctx->create){
        QJsonObject data;QString lz=query("lz");
        if(method=="POST"){QJsonParseError error;auto doc=QJsonDocument::fromJson(QByteArray(server1_http_request_body(request),int(server1_http_request_body_size(request))),&error);if(error.error!=QJsonParseError::NoError){body(response,400,"Invalid JSON");return 1;}data=doc.object();if(!data["servers"].isArray()||data["servers"].toArray().isEmpty()||(!data.contains("nzbUrl")&&!data.contains("nzbUrls"))){body(response,500,"Cannot parse JSON data, err 1");return 1;}}
        else {if(lz.isEmpty()){body(response,500,"Cannot parse JSON data, err 2");return 1;}QString error;auto text=archive::decompressUriQuery(lz,&error);data=QJsonDocument::fromJson(text.toUtf8()).object();if(data.isEmpty()){body(response,500,"Cannot parse JSON data, err 3");return 1;}pending->lz=true;}
        if(!data.contains("servers")||(!data.contains("nzbUrl")&&!data.contains("nzbUrls"))){body(response,500,"Cannot parse JSON data, err 3");return 1;}
        auto urls=data["nzbUrls"];if(!data.contains("nzbUrls")&&data.contains("nzbUrl"))urls=QJsonArray{data["nzbUrl"]};if(urls.isArray())for(auto url:urls.toArray())if(url.isString())pending->urls.append(url.toString());
        if(pending->urls.isEmpty()){body(response,500,"Cannot parse JSON data, err 4");return 1;}
        for(auto value:data["servers"].toArray()){auto server=parseServer(value.toString());if(!server){body(response,500,"All nzb files failed checks, cannot play");return 1;}pending->servers.append(*server);}
        if(pending->servers.isEmpty()){body(response,500,"All nzb files failed checks, cannot play");return 1;}
        pending->key=pending->lz?QString::fromLatin1(QCryptographicHash::hash(lz.toUtf8(),QCryptographicHash::Sha256).toHex()):QString::fromUtf8(server1_http_request_param(request,"createKey"));if(pending->key.isEmpty())pending->key=QUuid::createUuid().toString(QUuid::Id128);
        if(registry->sessions.contains(pending->key)){redirect(response,registry->sessions[pending->key]->location);return 1;}
        registry->failedKeys.remove(pending->key);pending->creating=true;
    }else{pending->key=QString::fromUtf8(server1_http_request_param(request,"key"));pending->filename=QString::fromUtf8(server1_http_request_param(request,"fileName"));if(pending->key.isEmpty())pending->key=query("key");if(pending->key.isEmpty()){body(response,500,{});return 1;}pending->range=QString::fromUtf8(server1_http_request_header_value_at(request,"range",0));pending->origin=QString::fromUtf8(server1_http_request_header_value_at(request,"origin",0));}
    pending->deferred=server1_http_response_defer(response);if(!pending->deferred)return 0;auto lifetime=new std::shared_ptr<Request>(pending);if(!server1_http_response_set_lifetime(response,[](void *value){delete static_cast<std::shared_ptr<Request>*>(value);},lifetime)){delete lifetime;return 0;}
    if(pending->creating)pending->fetchUrl();else if(registry->sessions.contains(pending->key)||registry->failedKeys.contains(pending->key))pending->resolve();else{auto &waiting=registry->waiting[pending->key];for(auto i=waiting.begin();i!=waiting.end();)if(i->expired())i=waiting.erase(i);else ++i;waiting.append(pending);}
    return 1;
}
}
void mountNzbRoutes(void *router,std::function<quint16()> localPort){auto registry=std::make_shared<Registry>();registry->router=router;registry->localPort=std::move(localPort);registry->mounts=4;for(auto route:{std::pair<const char*,const char*>{"POST","/nzb/create/:createKey"},{"ALL","/nzb/create"},{"GET","/nzb/stream/:key/:fileName"},{"GET","/nzb/stream"}}){server1_http_router_add_handler(router,0,0,route.first,route.second,handle,new Context{registry,QString::fromLatin1(route.second).contains("/create")},[](void *context){auto ctx=static_cast<Context*>(context);if(!--ctx->registry->mounts)ctx->registry->router=nullptr;delete ctx;});}}
}
