#include "server1/local/LocalMetadata.h"
#include <QJsonDocument>
#include <QFileInfo>
#include <QDateTime>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
namespace server1::local {
namespace {
QString image(const QString &kind,const QString &id){return "https://images.metahub.space/"+kind+"/medium/"+id+"/img";}
bool truthy(QJsonValue v){return !v.isUndefined()&&!v.isNull()&&(!v.isBool()||v.toBool())&&(!v.isDouble()||v.toDouble()!=0)&&(!v.isString()||!v.toString().isEmpty());}
QString stringValue(QJsonValue v){if(v.isDouble())return QString::number(v.toDouble(),'g',16);if(v.isBool())return v.toBool()?"true":"false";if(v.isNull())return "null";if(v.isUndefined())return "undefined";return v.toString();}
QJsonValue parsedYear(QJsonValue v){auto match=QRegularExpression("^\\s*([+-]?[0-9]+)").match(stringValue(v));return match.hasMatch()?QJsonValue(match.captured(1).toDouble()):QJsonValue(QJsonValue::Null);}
}
LocalMetadata::Config LocalMetadata::Config::sourceEndpoints(){Config c;c.movieNames=QUrl("https://cinemeta.strem.io/stremioget/stremio/v1/q.json?b=eyJwYXJhbXMiOltudWxsLHt9XSwibWV0aG9kIjoibmFtZXMubW92aWUiLCJpZCI6MSwianNvbnJwYyI6IjIuMCJ9");c.seriesNames=QUrl("https://cinemeta.strem.io/stremioget/stremio/v1/q.json?b=eyJwYXJhbXMiOltudWxsLHt9XSwibWV0aG9kIjoibmFtZXMuc2VyaWVzIiwiaWQiOjEsImpzb25ycGMiOiIyLjAifQ==");c.metaBase=QUrl("https://v3-cinemeta.strem.io");return c;}
LocalMetadata::LocalMetadata(Config config,QObject *parent):QObject(parent),config_(std::move(config)),network_(this){}
LocalMetadata::~LocalMetadata(){cancel();}
void LocalMetadata::deliver(Callback callback,QJsonObject result){auto generation=generation_;QTimer::singleShot(0,this,[this,generation,callback,result]{if(generation==generation_)callback(result);});}
void LocalMetadata::cancel(){++generation_;auto pending=std::move(pending_);pending_.clear();for(auto i=pending.begin();i!=pending.end();++i){i.key()->disconnect(this);i.key()->abort();i.key()->deleteLater();}/* Cancelled operations deliberately never invoke user callbacks. */}
void LocalMetadata::get(QUrl url,std::function<void(QJsonObject)> callback){
    if(!config_.enabled||!url.isValid()||(url.scheme()!="https"&&url.scheme()!="http")){deliver(callback,{});return;}
    QNetworkRequest request(url);request.setTransferTimeout(config_.timeoutMs);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply=network_.get(request);pending_[reply]=std::move(callback);
    connect(reply,&QNetworkReply::readyRead,this,[this,reply]{if(reply->bytesAvailable()>config_.maxResponseBytes)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply]{auto callback=pending_.take(reply);QJsonObject result;if(reply->error()==QNetworkReply::NoError&&reply->bytesAvailable()<=config_.maxResponseBytes)result=QJsonDocument::fromJson(reply->readAll()).object();reply->deleteLater();if(callback)callback(result);});
}
QString LocalMetadata::simplifyName(QJsonObject query){auto name=query.value("name").toString().toLower().trimmed();name.remove(QRegularExpression("\\([^\\(]+\\)$"));name.replace('&',"and");name.replace(QRegularExpression("[^0-9a-z ]+")," ");name=name.split(' ',Qt::SkipEmptyParts).join(' ');auto y=stringValue(query.value("year"));if(truthy(query.value("year"))&&name.endsWith(" "+y))name.chop(y.size()+1);return name;}
QJsonObject LocalMetadata::matchMetadata(QJsonObject query,QJsonArray records){
    const auto name=simplifyName(query);if(name.isEmpty())return {};
    for(auto v:records){auto record=v.toObject();if(truthy(record.value("year")))record["year"]=parsedYear(stringValue(record.value("year")).section('-',0,0));
        if(simplifyName(record)!=name||record.value("type")!=query.value("type"))continue;
        if(query.value("type")=="movie"&&query.contains("year")&&truthy(query.value("year"))&&truthy(record.value("year"))){auto y=parsedYear(query.value("year")),ry=parsedYear(record.value("year"));if(y.isNull()||ry.isNull()||qAbs(y.toDouble()-ry.toDouble())>1)continue;}
        QJsonObject out{{"id",record.value("imdb_id")},{"name",record.value("name")},{"type",record.value("type")}};
        if(record.contains("year"))out["year"]=record.value("year");
        return out;
    }return {};
}

void LocalMetadata::recognize(QJsonObject query,Callback callback){auto type=query.value("type").toString();if(!config_.enabled||(type!="movie"&&type!="series")){deliver(callback,{});return;}if(names_.contains(type)){auto result=matchMetadata(query,names_[type]);deliver(callback,result);return;}get(type=="movie"?config_.movieNames:config_.seriesNames,[this,query,type,callback](QJsonObject response){if(response.value("result").isArray())names_[type]=response.value("result").toArray();callback(matchMetadata(query,names_.value(type)));});}
QJsonObject LocalMetadata::genericMeta(QJsonObject entry){QJsonObject largest,imdb;for(auto v:entry.value("files").toArray()){auto f=v.toObject();if(imdb.isEmpty()&&!f.value("imdb_id").toString().isEmpty())imdb=f;if(!f.value("parsedName").toString().isEmpty()&&(largest.isEmpty()||f.value("length").toDouble()>largest.value("length").toDouble()))largest=f;}QJsonObject result{{"id",entry.value("itemId")},{"type","other"},{"name",largest.isEmpty()?entry.value("name"):largest.value("parsedName")},{"showAsVideos",true}};if(!imdb.isEmpty())for(auto kind:{"poster","background","logo"})result[kind]=image(kind,imdb.value("imdb_id").toString());return result;}
void LocalMetadata::enrich(QJsonObject entry,Callback callback){auto fallback=genericMeta(entry);QJsonObject imdb;for(auto v:entry.value("files").toArray())if(!v.toObject()["imdb_id"].toString().isEmpty()){imdb=v.toObject();break;}if(imdb.isEmpty()||!config_.enabled){deliver(callback,fallback);return;}auto url=config_.metaBase;url.setPath(url.path()+"/meta/"+imdb.value("type").toString()+"/"+imdb.value("imdb_id").toString()+".json");get(url,[callback,fallback](QJsonObject response){if(!response.value("meta").isObject()){callback(fallback);return;}auto meta=response.value("meta").toObject();const QStringList fields{"imdb_id","name","genre","director","cast","poster","description","trailers","background","logo","imdbRating","runtime","genres","releaseInfo"};for(auto key:meta.keys())if(!fields.contains(key))meta.remove(key);meta["id"]=fallback.value("id");meta["type"]=fallback.value("type");callback(meta);});}
void LocalMetadata::indexParsedFile(QString path,QJsonObject parsed,Callback callback){
    const QFileInfo info(path);
    if(!info.isFile()||!info.isReadable()||!QRegularExpression(".mkv$|.avi$|.mp4$|.wmv$|.vp8$|.mov$|.mpg$|.mp3$|.flac$",QRegularExpression::CaseInsensitiveOption).match(path).hasMatch()) {deliver(callback,{{"files",QJsonArray{}}});return;}
    recognize(parsed,[path,parsed,info,callback](QJsonObject match){
        if(match.value("id").toString().isEmpty()){callback({{"files",QJsonArray{}}});return;}
        QJsonObject file{{"path",path},{"name",info.fileName()},{"length",info.size()},{"parsedName",parsed.value("name")},{"type",parsed.value("type")},{"imdb_id",match.value("id")}};
        if(parsed.value("season").toInt()){file["season"]=parsed.value("season");auto episode=parsed.value("episode");if(episode.isArray())episode=episode.toArray().isEmpty()?QJsonValue(QJsonValue::Undefined):episode.toArray().first();if(!episode.isUndefined())file["episode"]=episode;}
        callback({{"dateModified",info.lastModified().toUTC().toString(Qt::ISODateWithMs)},{"itemId","local:"+match.value("id").toString()},{"name",info.fileName()},{"files",QJsonArray{file}}});
    });
}

}
