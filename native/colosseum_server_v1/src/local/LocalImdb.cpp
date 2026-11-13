#include "server1/local/LocalImdb.h"
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QPointer>
#include <algorithm>
#include <vector>
namespace server1::local {
namespace {
QString sanitize(QString s){s.remove(QRegularExpression("[^a-zA-Z0-9 ]"));s=s.toLower();auto pos=s.indexOf("the ");if(pos>=0)s.remove(pos,4);s.replace(QRegularExpression("\\s+")," ");return s.trimmed();}
double similar(QString a,QString b){if(a.isEmpty()||b.isEmpty())return 0;a=sanitize(a);b=sanitize(b);if(a==b)return 1;auto longest=std::max(a.size(),b.size());std::vector<int> costs(size_t(b.size()+1));for(int j=0;j<=b.size();++j)costs[size_t(j)]=j;for(int i=1;i<=a.size();++i){int previous=costs[0];costs[0]=i;for(int j=1;j<=b.size();++j){int old=costs[size_t(j)];costs[size_t(j)]=std::min({costs[size_t(j)]+1,costs[size_t(j-1)]+1,previous+(a[i-1]!=b[j-1])});previous=old;}}return double(longest-costs.back())/double(longest);}
bool almost(QString a,QString b){if(a.isEmpty()||b.isEmpty())return false;a=sanitize(a);b=sanitize(b);return a.startsWith(b)||a.endsWith(b)||b.startsWith(a)||b.endsWith(a);}
int year(QJsonValue v){if(v.isDouble())return v.toInt();auto m=QRegularExpression("^\\s*([+-]?[0-9]+)").match(v.toString());return m.captured(1).toInt();}
}
QString LocalImdb::parseSearchTerm(QString term){
    term.remove(QRegularExpression("[^\\x{0000}-\\x{036F}][ \\t]*"));
    QString output;
    for(auto character:term){switch(character.unicode()){
#include "LocalSearchTerm.inc"
    default:output+=character;break;
    }}return output;
}

QJsonObject LocalImdb::match(QJsonObject query,QJsonArray suggestions){QJsonObject pick,second,first;auto taskName=query.value("name").toString();for(auto v:suggestions){auto result=v.toObject();if(!QRegularExpression("tt[0-9]{7,8}").match(result.value("id").toString()).hasMatch())continue;auto type=result.value("q").toString(),wanted=query.value("type").toString();bool movie=wanted=="movie"&&(type=="feature"||type=="TV special"),series=wanted=="series"&&(type=="TV series"||type=="TV mini-series");auto y=year(query.value("year")),other=year(result.value("y"));if((!wanted.isEmpty()&&!movie&&!series)||(y&&other&&qAbs(y-other)>1))continue;
    QJsonObject res;for(auto pair:{std::pair<const char*,const char*>{"id","id"},{"name","l"},{"year","y"},{"type","q"},{"yearRange","yr"},{"starring","s"}})if(result.contains(pair.second))res[pair.first]=result[pair.second];if(result.value("i").isArray()){auto a=result.value("i").toArray();QJsonObject image;for(int i=0;i<std::min(3,int(a.size()));++i)image[QStringList{"src","width","height"}[i]]=a[i];res["image"]=image;}
    auto score=similar(taskName,res.value("name").toString());if(score>.6&&(pick.isEmpty()||score>pick["similarity"].toDouble())){res["similarity"]=score;pick=res;}
    if(second.isEmpty()&&almost(taskName,res.value("name").toString()))second=res;
    if(first.isEmpty()&&!query.value("strict").toBool())first=res;
 }if(!second.isEmpty()&&!pick.isEmpty()&&!almost(taskName,pick["name"].toString()))pick=second;return !pick.isEmpty()?pick:!second.isEmpty()?second:first;}
LocalImdb::LocalImdb(Config config,QObject *parent):QObject(parent),config_(std::move(config)),network_(this){}
LocalImdb::~LocalImdb(){cancel();}
void LocalImdb::cancel(){++generation_;auto pending=pending_;pending_.clear();for(auto *reply:pending.keys()){reply->disconnect(this);reply->abort();reply->deleteLater();}}
void LocalImdb::recognize(QJsonObject query,Callback callback){auto generation=generation_;if(!config_.enabled||query.value("name").toString().isEmpty()){QTimer::singleShot(0,this,[this,generation,callback]{if(generation==generation_)callback({});});return;}attempt(query,std::move(callback),false,generation);}
void LocalImdb::attempt(QJsonObject query,Callback callback,bool loose,quint64 generation){auto term=query.value("name").toString();bool retry=!loose&&year(query.value("year"));if(retry)term+=" "+QString::number(year(query.value("year")));auto url=config_.base;auto base=url.toString(QUrl::FullyEncoded);if(!base.endsWith('/'))base+='/';auto encoded=QUrl::toPercentEncoding(term,QByteArray("-_.!~*'()"));url=QUrl::fromEncoded(base.toUtf8()+QUrl::toPercentEncoding(term.left(1).toLower())+'/'+encoded+".json");
    if(url.scheme()!="http"&&url.scheme()!="https"){QTimer::singleShot(0,this,[this,generation,callback]{if(generation==generation_)callback({});});return;}
    QNetworkRequest request(url);request.setTransferTimeout(config_.timeoutMs);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);auto *reply=network_.get(request);pending_[reply]=true;
    connect(reply,&QNetworkReply::readyRead,this,[this,reply]{if(reply->bytesAvailable()>config_.maxBytes)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,query,callback,retry,generation]{pending_.remove(reply);QJsonArray suggestions;if(reply->error()==QNetworkReply::NoError&&reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200&&reply->bytesAvailable()<=config_.maxBytes){auto bytes=reply->readAll();auto start=bytes.indexOf('{'),end=bytes.lastIndexOf('}');if(start>=0&&end>=start)suggestions=QJsonDocument::fromJson(bytes.mid(start,end-start+1)).object()["d"].toArray();}reply->deleteLater();if(generation!=generation_)return;auto result=match(query,suggestions);if(result.isEmpty()&&retry)attempt(query,callback,true,generation);else callback(result);});
}
}
