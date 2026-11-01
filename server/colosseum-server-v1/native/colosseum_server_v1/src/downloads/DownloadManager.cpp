#include "server1/downloads/DownloadManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>
#include <QElapsedTimer>
#include <map>
#include <algorithm>
namespace server1::downloads {
struct DownloadManager::Impl {
 struct Handler {
  QString id,url,currentUrl,name,path; QPointer<QNetworkReply> reply; QFile file;
  qint64 downloaded=0,total=0,speed=0,previous=0; bool knownTotal=true,resumable=false,resumed=false,initialized=false,active=true;
  int incompleteRetries=0; QElapsedTimer speedTime; QTimer timer;
 };
 DownloadManager* owner; QString directory,statsPath; QNetworkAccessManager network; QJsonObject records; QStringList order;
 std::map<QString,std::shared_ptr<Handler>> handlers;
 static QStringList keyOrder(const QByteArray& raw){QStringList keys;int depth=0;for(qsizetype i=0;i<raw.size();++i){char c=raw[i];if(c=='"'){auto start=i;while(++i<raw.size()){if(raw[i]=='\\'){++i;continue;}if(raw[i]=='"')break;}auto next=i+1;while(next<raw.size()&&isspace(static_cast<unsigned char>(raw[next])))++next;if(depth==1&&next<raw.size()&&raw[next]==':'){auto a=QJsonDocument::fromJson("["+raw.mid(start,i-start+1)+"]").array();if(!a.isEmpty()){auto key=a.first().toString();if(!keys.contains(key))keys.append(key);}}}else if(c=='{'||c=='[')++depth;else if(c=='}'||c==']')--depth;}
  auto index=[](const QString& k)->quint64{bool ok=false;auto n=k.toULongLong(&ok);return ok&&n<4294967295ULL&&QString::number(n)==k?n:4294967295ULL;};std::stable_sort(keys.begin(),keys.end(),[&](const QString&a,const QString&b){return index(a)<index(b);});return keys;}
 QByteArray persisted()const{QByteArray bytes="{";bool first=true;for(auto& key:order){if(!records.contains(key))continue;auto value=QJsonDocument(QJsonObject{{key,records[key]}}).toJson(QJsonDocument::Compact);if(!first)bytes+=',';first=false;bytes+=value.mid(1,value.size()-2);}return bytes+'}';}
 Impl(DownloadManager* o,const QString& app):owner(o),directory(QDir(app).filePath("downloads")),statsPath(QDir(app).filePath("downManager.json")){
  QDir().mkdir(directory); QFile f(statsPath); if(f.open(QIODevice::ReadOnly)){auto raw=f.readAll();records=QJsonDocument::fromJson(raw).object();order=keyOrder(raw);}
   auto original=records;
  for(auto it=original.begin();it!=original.end();++it){auto r=it.value().toObject(); const auto n=r["fileName"].toString();
   if(!n.isEmpty()&&QFile::exists(QDir(directory).filePath(n))){
    if(!r["status"].toString().isEmpty()&&r["status"]!="paused"&&r["progress"].toDouble()!=0&&r["progress"].toDouble()<100) start(r["url"].toString(),r["id"].toString(),r);
   }else if(r["downloaded"].toDouble()!=0&&!r["id"].toString().isEmpty()){records.remove(r["id"].toString());order.removeAll(r["id"].toString());}
  }
 }
 ~Impl(){for(auto& p:handlers) cancel(p.second);}
 static void cancel(const std::shared_ptr<Handler>& h){h->active=false;h->timer.stop();h->timer.disconnect();if(h->reply){h->reply->disconnect();h->reply->abort();h->reply->deleteLater();h->reply=nullptr;}h->file.close();}
 QJsonObject stats(const std::shared_ptr<Handler>& h){return {{"total",h->knownTotal?QJsonValue(h->total):QJsonValue(QJsonValue::Null)},{"name",h->name},{"downloaded",h->downloaded},{"progress",h->total?100.0*h->downloaded/h->total:0.0},{"speed",h->speed}};}
 void update(const std::shared_ptr<Handler>& h){if(!records.contains(h->id))return;auto r=records[h->id].toObject();auto s=stats(h);for(auto it=s.begin();it!=s.end();++it)r[it.key()]=it.value();if(r["fileName"].toString().isEmpty()&&!h->name.isEmpty())r["fileName"]=h->name;records[h->id]=r;QFile f(statsPath);if(f.open(QIODevice::WriteOnly|QIODevice::Truncate))f.write(persisted());}
 void status(const std::shared_ptr<Handler>& h,const QString&s){auto r=records[h->id].toObject();r["status"]=s;records[h->id]=r;}
 void error(const std::shared_ptr<Handler>& h,const QString& message,bool removeFile=true){if(!h->active)return;h->timer.stop();h->file.close();if(removeFile&&!h->path.isEmpty())QFile::remove(h->path);auto r=records[h->id].toObject();r["status"]="error";r["error"]=message;records[h->id]=r;update(h);}
 QString unique(QString p){while(QFile::exists(p)){auto m=QRegularExpression("(.*)(\\([0-9]+\\))(\\..*)$").match(p);QString base=m.hasMatch()?m.captured(1).trimmed():p;int n=m.hasMatch()?m.captured(2).mid(1,m.captured(2).size()-2).toInt():0;auto ext=p.section('.',-1);if(ext!=p&&!ext.isEmpty()){ext='.'+ext;base.replace(ext,"");}else ext.clear();p=base+" ("+QString::number(n+1)+")"+ext;}return p;}
 QString filename(QNetworkReply* reply){QString cd=QString::fromLatin1(reply->rawHeader("content-disposition")),name;
  for(auto pattern:{QString(".*filename\\*=.*?'.*?'([^\"].+?[^\"])(?:(?:;)|$)"),QString(".*filename=\"(.*?)\";?"),QString(".*filename=([^\"].+?[^\"])(?:(?:;)|$)")}){auto m=QRegularExpression(pattern,QRegularExpression::CaseInsensitiveOption).match(cd);if(m.hasMatch()){name=m.captured(1);name.remove('/');name.remove('\\');break;}}
  if(name.isEmpty()){name=QFileInfo(reply->url().path(QUrl::FullyEncoded)).fileName();if(name.isEmpty())name=reply->url().host()+".html";}
  name.remove(QRegularExpression("\\.*$"));return name;
 }
 void begin(const std::shared_ptr<Handler>&h){h->initialized=false;QNetworkRequest req(QUrl(h->currentUrl.isEmpty()?h->url.trimmed():h->currentUrl));req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);if(h->resumed)req.setRawHeader("range","bytes="+QByteArray::number(h->downloaded)+"-");
  auto reply=network.get(req);h->reply=reply;
  QObject::connect(reply,&QNetworkReply::readyRead,owner,[this,h,reply]{if(h->active&&h->reply==reply)consume(h,reply);});
  QObject::connect(reply,&QNetworkReply::finished,owner,[this,h,reply]{if(!h->active||h->reply!=reply){reply->deleteLater();return;}consume(h,reply);if(h->reply!=reply){reply->deleteLater();return;}h->reply=nullptr;
   const int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
   if(!h->resumed&&(code<200||code>=300)){bool ok=false;h->total=reply->rawHeader("content-length").toLongLong(&ok);h->knownTotal=ok&&h->total!=0;h->downloaded=0;h->speed=0;h->previous=0;}
   if(code>300&&code<400&&reply->hasRawHeader("location")&&!reply->rawHeader("location").isEmpty()){h->currentUrl=QUrl(h->url).resolved(QUrl(QString::fromUtf8(reply->rawHeader("location")))).toString();begin(h);}
   else if(code<200||code>=400){error(h,code?QString("Response status was %1").arg(code):reply->errorString(),code>=500||code==0);}
   else if(reply->error()!=QNetworkReply::NoError){error(h,reply->errorString());}
   else {h->file.close();if(h->total&&h->downloaded!=h->total&&h->resumable&&h->incompleteRetries<=5){++h->incompleteRetries;h->resumed=true;begin(h);}else{h->timer.stop();status(h,"finished");update(h);}}
   reply->deleteLater();
  });
 }
 void consume(const std::shared_ptr<Handler>&h,QNetworkReply*reply){int code=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();if(code<200||code>=400||(code>300&&code<400&&reply->hasRawHeader("location")))return;
  if(!h->initialized){h->initialized=true;if(!h->resumed){bool ok=false;h->total=reply->rawHeader("content-length").toLongLong(&ok);h->knownTotal=ok&&h->total!=0;h->downloaded=0;h->speed=0;h->previous=0;h->path=unique(QDir(directory).filePath(filename(reply)));h->name=QFileInfo(h->path).fileName();}
   if(reply->hasRawHeader("accept-ranges")&&reply->rawHeader("accept-ranges")!="none")h->resumable=true;
   h->file.setFileName(h->path);if(!h->file.open(QIODevice::WriteOnly|(h->resumed?QIODevice::Append:QIODevice::Truncate))){error(h,h->file.errorString());reply->disconnect();reply->abort();h->reply=nullptr;reply->deleteLater();return;}h->resumed=false;h->speedTime.start();}
  auto bytes=reply->readAll();if(h->file.write(bytes)!=bytes.size()){error(h,h->file.errorString());reply->disconnect();reply->abort();h->reply=nullptr;reply->deleteLater();return;}h->downloaded+=bytes.size();if(!bytes.isEmpty()&&(h->downloaded==h->total||h->speedTime.elapsed()>1000)){h->speed=h->downloaded-h->previous;h->previous=h->downloaded;h->speedTime.restart();}
 }
 QJsonObject start(const QString&url,QString id={},QJsonObject prev={}){if(id.isEmpty()){do{id=QString::number(QRandomGenerator::global()->generate64(),36);}while(records.contains(id));}if(handlers.count(id))cancel(handlers[id]);auto h=std::make_shared<Handler>();h->id=id;h->url=url;
  auto initial=stats(h);initial["id"]=id;initial["url"]=url;initial["status"]="running";if(!records.contains(id))order.append(id);records[id]=initial;handlers[id]=h;
  QObject::connect(&h->timer,&QTimer::timeout,owner,[this,h]{update(h);});h->timer.start(2000);
  QTimer::singleShot(0,owner,[this,h,prev]{if(!h->active)return;auto r=records[h->id].toObject();r["fileName"]=h->name;records[h->id]=r;if(r["status"]=="paused"||h->reply||h->initialized)return;if(!prev.isEmpty()){h->name=prev["fileName"].toString();h->path=QDir(directory).filePath(h->name);h->total=prev["total"].toInteger();h->downloaded=prev["downloaded"].toInteger();if(!h->downloaded)h->downloaded=QFileInfo(h->path).size();h->resumed=true;h->resumable=true;}begin(h);});return initial;
 }
};
DownloadManager::DownloadManager(const QString&p,QObject* parent):QObject(parent),d(std::make_unique<Impl>(this,p)){}
DownloadManager::~DownloadManager()=default;
QJsonArray DownloadManager::all()const{QJsonArray a;for(auto&id:d->order)if(d->records.contains(id))a.append(d->records[id]);return a;}
QJsonObject DownloadManager::get(const QString&id)const{return d->records[id].toObject();}
QJsonObject DownloadManager::add(const QString&url){return d->start(url);}
QString DownloadManager::filePath(const QString&id)const{return QDir(d->directory).filePath(get(id)["fileName"].toString());}
QString DownloadManager::pause(const QString&id){if(id.isEmpty())return "missing id";if(!d->records.contains(id)||!d->handlers.count(id))return "there is no download handler with this id";auto h=d->handlers[id];if(get(id)["status"]!="running")return "download is not running";if(h->reply){h->reply->disconnect();h->reply->abort();h->reply->deleteLater();h->reply=nullptr;}h->file.close();d->status(h,"paused");return {};}
QString DownloadManager::resume(const QString&id){if(id.isEmpty())return "missing id";auto r=get(id);if(r.isEmpty())return "there is no download with this id";bool handler=d->handlers.count(id);if(handler&&r["status"]=="paused"){auto h=d->handlers[id];h->resumed=h->resumable;d->status(h,"running");d->begin(h);return {};}
 if(r["status"]=="error"||(!handler&&r["status"]=="paused")){if(!r["fileName"].toString().isEmpty())QFile::remove(filePath(id));if(r["url"].toString().isEmpty())return "url missing";d->start(r["url"].toString(),id);return {};}
 return handler?"download is not pause or errored":"Download status is not paused or errored";}
QString DownloadManager::remove(const QString&id){if(id.isEmpty())return "missing id";auto r=get(id);if(r.isEmpty())return "there is no download handler with this id";if(d->handlers.count(id)){auto h=d->handlers[id];Impl::cancel(h);if(!h->path.isEmpty())QFile::remove(h->path);d->handlers.erase(id);}if(r["status"]=="finished"&&!r["fileName"].toString().isEmpty())QFile::remove(filePath(id));d->records.remove(id);d->order.removeAll(id);return {};}
}
