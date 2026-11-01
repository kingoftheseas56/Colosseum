#include "server1/http/DownloadRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/downloads/DownloadManager.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <map>
#include <optional>
namespace server1::http {
namespace {
struct Config { QPointer<downloads::DownloadManager> manager; QString action; };
void json(void* res,const QJsonDocument& doc,int code=200){auto bytes=doc.toJson(QJsonDocument::Compact);server1_http_response_set_status(res,code);server1_http_response_set_header(res,"content-type","application/json");server1_http_response_set_body(res,bytes.constData(),bytes.size());}
void error(void*res,const QString& text,int code=500,bool missingUrl=false){QJsonObject e{{"message",text}};if(missingUrl)e["code"]=1;json(res,QJsonDocument(QJsonObject{{"error",e}}),code);}
std::optional<qint64> number(QString text){auto match=QRegularExpression("^\\s*([+-]?[0-9]+)").match(text);if(!match.hasMatch())return {};bool ok;auto n=match.captured(1).toLongLong(&ok);return ok?std::optional<qint64>(n):std::nullopt;}
struct Reader {QFile file;qint64 left;Reader(QString p,qint64 n):file(p),left(n){} };
QByteArray mime(const QString&path){static const std::map<std::string,std::string> table={
#include "TorrentMime.inc"
};auto ext=QFileInfo(path).suffix().toLower().toStdString();auto it=table.find(ext);return it==table.end()?QByteArray("application/octet-stream"):QByteArray::fromStdString(it->second);}
int handle(const void*req,void*res,server1_http_next_fn,void*,void*context){auto c=static_cast<Config*>(context);auto m=c->manager;if(!m){error(res,"download manager unavailable");return 1;}auto action=c->action;QString id=QString::fromUtf8(server1_http_request_query_value(req,"id"));
 if(action=="getAll"){json(res,QJsonDocument(m->all()));return 1;}
 if(action=="add"){QString url=QString::fromUtf8(server1_http_request_query_value(req,"url"));if(url.isEmpty())error(res,"missing url",500,true);else json(res,QJsonDocument(m->add(url)));return 1;}
 if(id.isEmpty()){error(res,"missing id");return 1;}
 if(action=="get"){auto r=m->get(id);if(r.isEmpty())error(res,"there is no download with this id");else json(res,QJsonDocument(r));return 1;}
 if(action=="pause"||action=="resume"||action=="remove"){auto e=action=="pause"?m->pause(id):action=="resume"?m->resume(id):m->remove(id);if(e.isEmpty())json(res,QJsonDocument(QJsonObject{{"success",true}}));else error(res,e);return 1;}
 auto r=m->get(id);if(r.isEmpty()){error(res,"there is no download handler with this id");return 1;}if(r["status"]!="finished"){error(res,"stream for this id not finished");return 1;}
 auto path=m->filePath(id);if(!QFile::exists(path)){error(res,"file not found",404);return 1;}
 qint64 size=QFileInfo(path).size(),start=0,end=size-1;QString range=QString::fromUtf8(server1_http_request_header_value_at(req,"range",0));int code=200;
 if(!range.isEmpty()){range.replace(QRegularExpression("bytes="),"");auto parts=range.split('-');auto s=number(parts.value(0));auto e=parts.value(1).isEmpty()?std::optional<qint64>(size-1):number(parts.value(1));if(s&&!e)e=size-1;if(!s&&e){s=size-*e;e=size-1;}
  if((s&&*s>=size)||(e&&*e>=size)){server1_http_response_set_status(res,416);server1_http_response_set_body(res,"",0);server1_http_response_set_header(res,"content-range",("bytes */"+QByteArray::number(size)).constData());return 1;}
  // Node's createReadStream rejects NaN, negative and inverted ranges. Do not turn them into a full-file response.
  if(!s||!e||*s<0||*e<*s){error(res,"invalid range");return 1;}start=*s;end=*e;code=206;
  server1_http_response_set_header(res,"content-range",QString("bytes %1-%2/%3").arg(start).arg(end).arg(size).toUtf8().constData());server1_http_response_set_header(res,"accept-ranges","bytes");
 }
 auto reader=new Reader(path,end-start+1);if(!reader->file.open(QIODevice::ReadOnly)||!reader->file.seek(start)){delete reader;error(res,"file not found",404);return 1;}
 server1_http_response_set_status(res,code);server1_http_response_set_header(res,"content-type",mime(path).constData());server1_http_response_set_header(res,"content-length",QByteArray::number(reader->left).constData());
 server1_http_response_set_stream(res,[](void*ctx,void*buf,std::size_t cap)->std::ptrdiff_t{auto r=static_cast<Reader*>(ctx);auto n=r->file.read(static_cast<char*>(buf),qMin<qint64>(r->left,cap));if(n>0)r->left-=n;return n;},[](void*ctx){delete static_cast<Reader*>(ctx);},reader,reader->left);return 1;
}
}
void mountDownloadRoutes(void*router,downloads::DownloadManager*m,const QString&key){if(key.isEmpty())return;for(auto action:{"getAll","get","add","pause","resume","remove","stream"}){auto path=QString("/%1/downloader/%2").arg(key,action).toUtf8();server1_http_router_add_handler(router,0,0,"GET",path.constData(),handle,new Config{m,action},[](void*p){delete static_cast<Config*>(p);});}}
}
