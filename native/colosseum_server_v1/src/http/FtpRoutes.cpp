#include "server1/http/FtpRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/archive/FtpSource.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QFileInfo>
#include <QUuid>
#include <map>
#include <cstring>
namespace server1::http {
namespace {
using namespace server1::archive;
struct Entry {QUrl url;FtpMetadata meta;QString location;};
struct Request;
struct Registry {QHash<QString,Entry> entries;QHash<QString,QList<std::weak_ptr<Request>>> waiting;};
void body(void*r,int status,QByteArray bytes={}){server1_http_response_set_status(r,status);server1_http_response_set_body(r,bytes.constData(),size_t(bytes.size()));}
QByteArray mime(QString path){static const std::map<std::string,std::string> types={
#include "TorrentMime.inc"
};auto i=types.find(QFileInfo(path).suffix().toLower().toStdString());return i==types.end()?QByteArray("application/octet-stream"):QByteArray::fromStdString(i->second);}
struct Request:std::enable_shared_from_this<Request>{
 std::shared_ptr<Registry> registry;QString key,method,range,origin;void*deferred=nullptr;void*wake=nullptr;
 std::unique_ptr<FtpSource> source;qint64 offset=0,remaining=0;QByteArray buffer;qsizetype position=0;bool pending=false,failed=false,seek=true;
 ~Request(){source.reset();server1_http_stream_wake_destroy(wake);server1_http_deferred_destroy(deferred);}
 void finish(int status,QByteArray bytes={}){if(auto r=server1_http_deferred_response(deferred)){body(r,status,bytes);server1_http_deferred_complete(deferred);}}
 void redirect(){auto i=registry->entries.constFind(key);if(i==registry->entries.cend())return;if(auto r=server1_http_deferred_response(deferred)){server1_http_response_set_header(r,"location",i->location.toUtf8().constData());finish(302);}}
 std::ptrdiff_t pull(void*dest,size_t maximum){if(!maximum)return 0;if(position<buffer.size()){auto n=qMin<qsizetype>(buffer.size()-position,qsizetype(qMin<size_t>(maximum,65536)));memcpy(dest,buffer.constData()+position,size_t(n));position+=n;if(position==buffer.size()){buffer.clear();position=0;}return n;}if(failed)return -1;if(!remaining)return 0;if(!pending){pending=true;auto n=qMin<qint64>(remaining,65536);std::weak_ptr<Request>w=shared_from_this();source->read(offset,n,[w,n](ReadResult result){if(auto self=w.lock()){self->pending=false;self->failed=!result.error.isEmpty();self->buffer=std::move(result.bytes);self->offset+=self->buffer.size();self->remaining-=self->buffer.size();if(self->buffer.size()!=n)self->failed=true;server1_http_stream_wake_notify(self->wake);}},seek);}return SERVER1_HTTP_WOULD_BLOCK;}
 void stream(const Entry&e){auto r=server1_http_deferred_response(deferred);if(!r)return;auto plan=ftpRangePlan(e.meta.size,e.meta.supportsSeek,range);if(plan.status>=400){finish(plan.status);return;}server1_http_response_set_status(r,plan.status);server1_http_response_set_header(r,"content-type",mime(e.meta.path).constData());server1_http_response_set_header(r,"content-length",QByteArray::number(plan.length).constData());server1_http_response_set_header(r,"accept-ranges",plan.acceptRanges?"bytes":"none");server1_http_response_set_header(r,"access-control-allow-origin",(origin.isEmpty()?QByteArray("*"):origin.toUtf8()).constData());server1_http_response_set_header(r,"transferMode.dlna.org","Streaming");server1_http_response_set_header(r,"contentFeatures.dlna.org","DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000");if(!plan.contentRange.isEmpty())server1_http_response_set_header(r,"content-range",plan.contentRange.toUtf8().constData());if(method=="HEAD"){server1_http_deferred_complete(deferred);return;}offset=plan.start;remaining=plan.length;seek=e.meta.supportsSeek;source=std::make_unique<FtpSource>(e.url);auto lifetime=new std::shared_ptr<Request>(shared_from_this());if(!server1_http_response_set_async_stream(r,[](void*p,void*d,size_t n){return (*static_cast<std::shared_ptr<Request>*>(p))->pull(d,n);},[](void*p){delete static_cast<std::shared_ptr<Request>*>(p);},lifetime,size_t(plan.length))){delete lifetime;finish(500);return;}wake=server1_http_response_stream_wake_handle(r);server1_http_deferred_complete(deferred);}
};
struct Context{std::shared_ptr<Registry> registry;int action;};
int handle(const void*req,void*res,server1_http_next_fn,void*,void*opaque){
 auto c=static_cast<Context*>(opaque);auto query=[&](const char*n){return QString::fromUtf8(server1_http_request_query_value(req,n));};
 auto request=std::make_shared<Request>();request->registry=c->registry;request->method=QString::fromUtf8(server1_http_request_method(req));
 QUrl url;bool post=request->method=="POST";
 if(c->action==0){QJsonObject data;auto lz=query("lz");if(post){data=QJsonDocument::fromJson(QByteArray(server1_http_request_body(req),qsizetype(server1_http_request_body_size(req)))).object();if(data["ftpUrl"].toString().isEmpty()){body(res,500,"Cannot parse JSON data, err 1");return 1;}}else{if(lz.isEmpty()){body(res,500,"Cannot parse JSON data, err 2");return 1;}QString error;data=QJsonDocument::fromJson(decompressUriQuery(lz,&error).toUtf8()).object();if(!error.isEmpty()||data.isEmpty()){body(res,500,"Cannot parse JSON data, err 3");return 1;}}
  url=QUrl(data["ftpUrl"].toString());request->key=post?QString::fromUtf8(server1_http_request_param(req,"createKey")):QString::fromLatin1(QCryptographicHash::hash(lz.toUtf8(),QCryptographicHash::Sha256).toHex());if(request->key.isEmpty())request->key=QUuid::createUuid().toString(QUuid::Id128);
 }else request->key=c->action==1?query("key"):QString::fromUtf8(server1_http_request_param(req,"key"));
 request->deferred=server1_http_response_defer(res);if(!request->deferred)return 0;auto lifetime=new std::shared_ptr<Request>(request);if(!server1_http_response_set_lifetime(res,[](void*p){delete static_cast<std::shared_ptr<Request>*>(p);},lifetime)){delete lifetime;return 0;}
 if(c->action==0){request->source=std::make_unique<FtpSource>(url);std::weak_ptr<Request>w=request;request->source->inspect([w,url,post](FtpMetadata meta){auto self=w.lock();if(!self)return;if(!meta.error.isEmpty()){self->finish(500);return;}Entry e{url,meta,"/ftp/stream/"+self->key+"/"+QFileInfo(meta.path).fileName()};self->registry->entries[self->key]=e;self->source.reset();if(post){auto r=server1_http_deferred_response(self->deferred);if(r)server1_http_response_set_header(r,"content-type","application/json");self->finish(200,QJsonDocument(QJsonObject{{"key",self->key}}).toJson(QJsonDocument::Compact));}else self->redirect();for(auto waiter:self->registry->waiting.take(self->key))if(auto pending=waiter.lock())pending->redirect();});}
 else if(c->action==1){if(request->registry->entries.contains(request->key))request->redirect();else{auto&waiting=request->registry->waiting[request->key];for(auto i=waiting.begin();i!=waiting.end();)if(i->expired())i=waiting.erase(i);else ++i;waiting.append(request);}}
 else{auto i=request->registry->entries.constFind(request->key);if(i==request->registry->entries.cend()){request->finish(500);return 1;}request->range=QString::fromUtf8(server1_http_request_header_value_at(req,"range",0));request->origin=QString::fromUtf8(server1_http_request_header_value_at(req,"origin",0));request->stream(*i);}
 return 1;
}
}
void mountFtpRoutes(void*router){auto registry=std::make_shared<Registry>();struct Route{const char*method;const char*path;int action;};for(auto r:{Route{"POST","/ftp/create/:createKey",0},{"ALL","/ftp/create",0},{"GET","/ftp/stream/:key/:fileName",2},{"GET","/ftp/stream",1},{"ALL","/ftp/:fileName",0}})server1_http_router_add_handler(router,0,0,r.method,r.path,handle,new Context{registry,r.action},[](void*p){delete static_cast<Context*>(p);});}
}
