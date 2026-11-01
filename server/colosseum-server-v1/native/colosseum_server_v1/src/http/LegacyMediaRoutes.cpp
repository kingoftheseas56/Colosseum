#include "server1/http/LegacyMediaRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/media/LegacyHlsService.h"
#include <QJsonArray>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <limits>
namespace server1::http {
namespace {
struct Config{QPointer<media::LegacyHlsService> service;std::function<QString()> base;QString action;};
struct Pending{void* deferred=nullptr;void* wake=nullptr;std::shared_ptr<media::LegacyOperation> operation;~Pending(){if(operation)operation->cancel();server1_http_deferred_destroy(deferred);server1_http_stream_wake_destroy(wake);}};
QString param(const void* req,const char* key){return QString::fromUtf8(server1_http_request_param(req,key));}
int route(const void* req,void* res,server1_http_next_fn next,void* nextContext,void* context){
 auto& c=*static_cast<Config*>(context);if(!c.service)return next(nextContext);
 QString target=QString::fromUtf8(server1_http_request_target(req));QUrlQuery query(QUrl(target.replace('+',' ')));QJsonObject q;for(auto pair:query.queryItems(QUrl::FullyDecoded)){if(q.contains(pair.first)){auto v=q[pair.first];auto a=v.isArray()?v.toArray():QJsonArray{v};a.append(pair.second);q[pair.first]=a;}else q[pair.first]=pair.second;}
 const QString base=c.base?c.base():QString();
 QString first=param(req,"first"),second=param(req,"second"),from=q["from"].toString();if(!from.isEmpty())from=QUrl::fromPercentEncoding(from.toUtf8());else if(first.size()==40)from=base+"/"+first+"/"+second;else if(first=="file"||first=="url")from=second;
 if(c.action=="probe"){from=q["url"].toString();if(!from.contains("://"))from=base+from;}
 QJsonObject params{{"from",from}};for(auto key:{"quality","stream","seg","lang"}){auto s=param(req,key);if(!s.isEmpty())params[key]=s;}
 QString rawTarget=QString::fromUtf8(server1_http_request_target(req));QJsonObject input{{"url",rawTarget},{"search",rawTarget.contains('?')?rawTarget.mid(rawTarget.indexOf('?')):QString()},{"params",params},{"query",q},{"headers",QJsonObject{{"host",QString::fromUtf8(server1_http_request_header_value_at(req,"host",0))}}}};
 auto pending=std::make_shared<Pending>();pending->deferred=server1_http_response_defer(res);if(!pending->deferred)return 0;
 auto* holder=new std::shared_ptr<Pending>(pending);if(!server1_http_response_set_lifetime(res,[](void* p){delete static_cast<std::shared_ptr<Pending>*>(p);},holder)){delete holder;return 0;}
 std::weak_ptr<Pending> weak=pending;
 pending->operation=c.service->request(c.action,input,[weak](media::LegacyResponse response){auto p=weak.lock();if(!p)return;auto res=server1_http_deferred_response(p->deferred);if(!res)return;server1_http_response_set_status(res,response.status);for(auto i=response.headers.begin();i!=response.headers.end();i++){auto value=i.value().isString()?i.value().toString():QString::number(i.value().toDouble(),'g',16);server1_http_response_set_header(res,i.key().toUtf8().constData(),value.toUtf8().constData());}
  if(response.program.isEmpty()){server1_http_response_set_body(res,response.body.constData(),response.body.size());}
  else {auto* stream=new std::shared_ptr<Pending>(p);if(!server1_http_response_set_async_stream(res,[](void* v,void* b,size_t n)->std::ptrdiff_t{auto p=*static_cast<std::shared_ptr<Pending>*>(v);return p->operation?p->operation->read(static_cast<char*>(b),qint64(n)):SERVER1_HTTP_WOULD_BLOCK;},[](void*v){delete static_cast<std::shared_ptr<Pending>*>(v);},stream,std::numeric_limits<size_t>::max())){delete stream;server1_http_response_set_status(res,500);}else{p->wake=server1_http_response_stream_wake_handle(res);if(p->operation)p->operation->wake=[weak]{if(auto p=weak.lock())server1_http_stream_wake_notify(p->wake);};}}
  server1_http_deferred_complete(p->deferred);
 });return 1;
}
}
bool installLegacyMediaRoutes(void* router,media::LegacyHlsService* service,QString base){
 return installLegacyMediaRoutes(router,service,[base=std::move(base)]{return base;});
}
bool installLegacyMediaRoutes(void* router,media::LegacyHlsService* service,std::function<QString()> base){
 const QList<QPair<QString,QString>> routes={{"/:first/:second/hls.m3u8","master"},{"/:first/:second/master.m3u8","multi"},{"/:first/:second/stream.m3u8","playlist"},{"/:first/:second/stream-q-:quality.m3u8","playlist"},{"/:first/:second/stream-:stream.m3u8","playlist"},{"/:first/:second/stream-q-:quality/:seg.ts","segment"},{"/:first/:second/stream-:stream/:seg.ts","segment"},{"/:first/:second/dlna","dlna"},{"/:first/:second/subs-:lang.m3u8","subtitle"},{"/:first/:second/thumb.jpg","thumb"},{"/thumb.jpg","thumb"},{"/probe","probe"}};
 for(auto row:routes){auto* c=new Config{service,base,row.second};if(!server1_http_router_add_handler(router,0,0,"GET",row.first.toUtf8().constData(),route,c,[](void*p){delete static_cast<Config*>(p);}))return false;}return true;
}
}
