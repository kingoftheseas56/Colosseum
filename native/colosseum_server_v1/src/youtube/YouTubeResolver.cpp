#include "server1/youtube/YouTubeResolver.h"
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
namespace server1::youtube {
namespace {
#include "YouTubePatterns.inc"
QString match(const QString &text,const QString &pattern,int capture=1) {return QRegularExpression(pattern).match(text).captured(capture);}
bool truth(const QJsonValue &v) {return v.isBool()?v.toBool():v.isDouble()?v.toDouble()!=0:v.isString()?!v.toString().isEmpty():v.isObject()||v.isArray();}
QString string(const QJsonValue &v) {return v.isDouble()?QString::number(v.toDouble(),'g',16):v.toString();}
double number(const QJsonValue &v) {return v.isDouble()?v.toDouble():v.toString().toDouble();}
QString between(const QString &s,const QString &left,const QString &right) {auto a=s.indexOf(left);if(a<0)return {};a+=left.size();auto b=s.indexOf(right,a);return b<0?QString{}:s.mid(a,b-a);}
QJsonObject parseObject(const QByteArray &bytes) {return QJsonDocument::fromJson(bytes).object();}
QJsonObject embedded(const QString &body,const QString &pattern) {
    const auto m=QRegularExpression(pattern).match(body);if(!m.hasMatch())return {};
    const auto start=m.capturedEnd()-1;int depth=0;bool quoted=false,escape=false;
    for(qsizetype i=start;i<body.size();++i){auto c=body[i];if(quoted){if(escape)escape=false;else if(c=='\\')escape=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c=='{')++depth;else if(c=='}'&&!--depth)return parseObject(body.mid(start,i-start+1).toUtf8());}
    return {};
}
QJsonObject playerFrom(const QJsonObject &info) {
    QJsonValue value=info.value("args").toObject().value("player_response");
    for(auto key:{"player_response","playerResponse","embedded_player_response"})if(!truth(value))value=info.value(key);
    return value.isString()?parseObject(value.toString().toUtf8()):value.toObject();
}
QString playerScript(const QString &body) {
    const auto m=QRegularExpression(R"YT(<script\s+src="([^"]+)"(?:\s+type="text/javascript")?\s+name="player_ias/base"\s*>|"jsUrl":"([^"]+)")YT").match(body);
    return m.captured(1).isEmpty()?m.captured(2):m.captured(1);
}
QJsonObject queryObject(const QString &query) {
    QJsonObject out;
    // querystring.parse decodes '+' as space and preserves duplicate fields as arrays.
    const auto items=QUrlQuery(QString(query).replace('+',"%20")).queryItems(QUrl::FullyDecoded);
    for(const auto &item:items) {if(!out.contains(item.first))out[item.first]=item.second;else{auto v=out[item.first];auto a=v.isArray()?v.toArray():QJsonArray{v};a.append(item.second);out[item.first]=a;}}
    return out;
}
QString formEncode(const QString &s) {return QString::fromLatin1(QUrl::toPercentEncoding(s,"*-._")).replace("%20","+").replace('~',"%7E");}
QString downloadUrl(const QString &url,const QString &signature,const QString &sp) {
    // decodeURIComponent is deliberately applied once, as in pinned setDownloadURL.
    for(qsizetype i=0;i<url.size();++i)if(url[i]=='%'&&(i+2>=url.size()||!QRegularExpression("^[0-9a-fA-F]{2}$").match(url.mid(i+1,2)).hasMatch()))return url;
    const auto decoded=QUrl::fromPercentEncoding(url.toUtf8());QUrl parsed(decoded,QUrl::StrictMode);
    if(!parsed.isValid()||parsed.scheme().isEmpty())return {};
    auto items=QUrlQuery(QString(parsed.query(QUrl::FullyEncoded)).replace('+',"%20")).queryItems(QUrl::FullyDecoded);
    auto set=[&](const QString &key,const QString &value){bool found=false;for(auto i=items.begin();i!=items.end();){if(i->first==key){if(!found){i->second=value;found=true;++i;}else i=items.erase(i);}else ++i;}if(!found)items.append({key,value});};
    set("ratebypass","yes");if(!signature.isEmpty())set(sp.isEmpty()?"signature":sp,signature);
    QStringList encoded;for(const auto &item:items)encoded.append(formEncode(item.first)+"="+formEncode(item.second));
    return parsed.adjusted(QUrl::RemoveQuery|QUrl::RemoveFragment).toString(QUrl::FullyEncoded)+"?"+encoded.join('&')+(parsed.hasFragment()?"#"+parsed.fragment(QUrl::FullyEncoded):QString{});
}
QString playError(const QJsonObject &player) {
    const auto p=player.value("playabilityStatus").toObject();const auto status=p.value("status").toString();
    // Never attempt alternate endpoints to evade login, consent, age, geo, or availability restrictions.
    if(!status.isEmpty()&&status!="OK") {auto reason=p.value("reason").toString();if(reason.isEmpty())reason=p.value("messages").toArray().first().toString();return reason.isEmpty()?"YouTube playback is restricted ("+status+")":reason;}
    return {};
}
}
QStringList YouTubeResolver::extractActions(const QString &script) {
    auto obj=actionsObjRegexp.match(script),func=actionsFuncRegexp.match(script);if(!obj.hasMatch()||!func.hasMatch())return {};
    const auto body=obj.captured(2);QStringList keys;for(const auto *re:{&reverseRegexp,&sliceRegexp,&spliceRegexp,&swapRegexp}){QString k=re->match(body).captured(1);if(k.startsWith('"')||k.startsWith('\''))k=k.mid(1,k.size()-2);keys.append(k);}
    const QString prop="("+keys.join('|')+")";
    const QRegularExpression calls("(?:a=)?"+QRegularExpression::escape(obj.captured(1))+"(?:\\."+prop+"|\\['"+prop+"'\\]|\\[\""+prop+"\"\\])\\(a,(\\d+)\\)");
    QStringList actions;auto it=calls.globalMatch(func.captured(1));while(it.hasNext()){const auto m=it.next();auto key=m.captured(1);if(key.isEmpty())key=m.captured(2);if(key.isEmpty())key=m.captured(3);if(key==keys[3])actions.append("w"+m.captured(4));else if(key==keys[0])actions.append("r");else if(key==keys[1])actions.append("s"+m.captured(4));else if(key==keys[2])actions.append("p"+m.captured(4));}return actions;
}
QString YouTubeResolver::decipher(const QStringList &actions,const QString &signature) {
    // QString retains JavaScript UTF-16 split/join behavior, including sparse swaps.
    QVector<QString> chars;for(const auto c:signature)chars.append(QString(c));
    for(const auto &action:actions){auto n=action.mid(1).toUInt();if(action.startsWith('r'))std::reverse(chars.begin(),chars.end());else if(action.startsWith('s')||action.startsWith('p'))chars.erase(chars.begin(),chars.begin()+std::min<qsizetype>(n,chars.size()));else if(action.startsWith('w')&&!chars.isEmpty()){auto first=chars[0];chars[0]=chars[n%chars.size()];if(n<1000000){if(n>=quint32(chars.size()))chars.resize(n+1);chars[n]=first;}}}
    QString result;for(const auto &c:chars)result+=c;return result;
}
QJsonObject YouTubeResolver::addFormatMeta(QJsonObject f) {
    static const auto defaults=parseObject(
#include "YouTubeFormats.inc"
    );
    auto result=defaults.value(string(f.value("itag"))).toObject();for(auto i=f.begin();i!=f.end();++i)result[i.key()]=i.value();f=result;
    const bool video=truth(f.value("qualityLabel")),audio=truth(f.value("audioBitrate"));const auto mime=f.value("mimeType").toString();
    f["hasVideo"]=video;f["hasAudio"]=audio;
    f["container"]=mime.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(mime.section(';',0,0).section('/',1,1));
    const auto codecs=between(mime,"codecs=\"","\"");f["codecs"]=mime.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(codecs);
    f["videoCodec"]=video&&!codecs.isEmpty()?QJsonValue(codecs.split(", ").first()):QJsonValue(QJsonValue::Null);
    f["audioCodec"]=audio&&!codecs.isEmpty()?QJsonValue(codecs.split(", ").last()):QJsonValue(QJsonValue::Null);
    const auto url=f.value("url").toString();f["isLive"]=QRegularExpression("\\bsource[/=]yt_live_broadcast\\b").match(url).hasMatch();f["isHLS"]=QRegularExpression("/manifest/hls_(variant|playlist)/").match(url).hasMatch();f["isDashMPD"]=url.contains("/manifest/dash/");return f;
}
Result YouTubeResolver::chooseFormat(QJsonArray input) {
    QList<QJsonObject> formats;for(const auto &v:input){auto f=addFormatMeta(v.toObject());if(truth(f.value("url"))&&truth(f.value("hasAudio"))&&truth(f.value("hasVideo")))formats.append(f);}
    const bool hls=std::any_of(formats.begin(),formats.end(),[](const auto &f){return f.value("isHLS").toBool();});
    if(hls)formats.erase(std::remove_if(formats.begin(),formats.end(),[](const auto &f){return !f.value("isHLS").toBool()&&f.value("isLive").toBool();}),formats.end());
    auto rank=[](const QString &s,const QStringList &list){for(int i=0;i<list.size();++i)if(s.contains(list[i]))return i;return -1;};
    auto score=[&](const QJsonObject &f){return QList<double>{double(f.value("isHLS").toBool()),double(f.value("isDashMPD").toBool()),double(number(f.value("contentLength"))>0),double(f.value("hasVideo").toBool()&&f.value("hasAudio").toBool()),double(f.value("hasVideo").toBool()),match(f.value("qualityLabel").toString(),"^(\\d+)").toDouble(),number(f.value("bitrate")),number(f.value("audioBitrate")),double(rank(f.value("codecs").toString(),{"mp4v","avc1","Sorenson H.283","MPEG-4 Visual","VP8","VP9","H.264"})),double(rank(f.value("codecs").toString(),{"mp4a","mp3","vorbis","aac","opus","flac"}))};};
    std::stable_sort(formats.begin(),formats.end(),[&](const auto &a,const auto &b){return score(a)>score(b);});
    if(formats.isEmpty())return {{},"No such format found: highest"};
    return {formats.first(),{}};
}
struct YouTubeResolver::State {
    Fetch fetch;QPointer<QObject> owner;
    struct Cached {Result result;qint64 expires;};
    QHash<QString,Cached> cache;QHash<QString,QList<Callback>> pending;
};
namespace {
struct Job:std::enable_shared_from_this<Job> {
    YouTubeResolver::Fetch fetch;YouTubeResolver::Callback done;QString id,html,script,cver="2.20210622.10.00";QJsonObject player;QJsonArray formats;QStringList actions;int endpoint=0;bool finished=false;
    void finish(Result result){if(finished)return;finished=true;done(std::move(result));}
    void get(const QUrl &url,std::function<void(YouTubeResolver::Page)> cb){fetch(url,std::move(cb));}
    void next(){if(endpoint>=3){finish({{},"Could not retrieve YouTube player response"});return;}const int stage=endpoint++;
        QUrl url("https://www.youtube.com/watch");QUrlQuery query;query.addQueryItem("v",id);query.addQueryItem("hl","en");if(stage==1)query.addQueryItem("pbj","1");if(stage==2){url=QUrl("https://www.youtube.com/get_video_info");query.clear();query.addQueryItem("video_id",id);query.addQueryItem("c","TVHTML5");query.addQueryItem("cver","7"+cver.mid(1));query.addQueryItem("eurl","https://youtube.googleapis.com/v/"+id);query.addQueryItem("ps","default");query.addQueryItem("gl","US");query.addQueryItem("hl","en");query.addQueryItem("html5","1");}url.setQuery(query);
        get(url,[self=shared_from_this(),stage](YouTubeResolver::Page page){if(!page.error.isEmpty()){if(stage==2)self->finish({{},page.error});else self->next();return;}QJsonObject p;
            if(stage==0){self->html=QString::fromUtf8(page.body);if(QRegularExpression("<title>\\s*(Before you continue|Sign in)|<form[^>]+action=[\"\']https://consent\\.",QRegularExpression::CaseInsensitiveOption).match(self->html).hasMatch()){self->finish({{},"YouTube consent or sign-in is required"});return;}auto cv=between(self->html,"{\"key\":\"cver\",\"value\":\"","\"}");self->cver=cv;p=embedded(self->html,"\\bytInitialPlayerResponse\\s*=\\s*\\{");if(p.isEmpty())p=playerFrom(embedded(self->html,"\\bytplayer\\.config\\s*=\\s*\\{"));self->script=playerScript(self->html);
                // The old extractor also requires ytInitialData, even when unused by these routes.
                if(embedded(self->html,"\\bytInitialData(\"\\])?\\s*=\\s*\\{").isEmpty()&&!QRegularExpression("\\bytInitialData(\"\\])?\\s*=\\s*\\{\\s*\\}").match(self->html).hasMatch())p={};
            }else if(stage==1){QByteArray body=page.body;while(!body.isEmpty()&&QByteArray(")]}' \r\n\t").contains(body[0]))body.remove(0,1);auto doc=QJsonDocument::fromJson(body);QJsonObject info;for(const auto &v:doc.array()){auto o=v.toObject();for(auto i=o.begin();i!=o.end();++i)if(!info.contains(i.key()))info[i.key()]=i.value();}p=playerFrom(info);auto js=info.value("player").toObject().value("assets").toObject().value("js").toString();if(!js.isEmpty())self->script=js;
            }else p=playerFrom(queryObject(QString::fromUtf8(page.body)));
            const auto restricted=playError(p);if(!restricted.isEmpty()){self->finish({{},restricted});return;}
            for(auto i=p.begin();i!=p.end();++i)if(!i.value().isNull())self->player[i.key()]=i.value();
            if(!self->player.contains("streamingData")){if(stage==2)self->prepare();else self->next();return;}self->prepare();
        });
    }
    void prepare(){auto streaming=player.value("streamingData").toObject();formats=streaming.value("formats").toArray();for(const auto &f:streaming.value("adaptiveFormats").toArray())formats.append(f);
        if(formats.isEmpty()){manifests();return;}if(script.isEmpty()){get(QUrl("https://www.youtube.com/embed/"+id+"?hl=en"),[self=shared_from_this()](YouTubeResolver::Page page){self->script=playerScript(QString::fromUtf8(page.body));if(self->script.isEmpty())self->finish({{},page.error.isEmpty()?"Unable to find html5player file":page.error});else self->tokens();});}else tokens();
    }
    void tokens(){get(QUrl("https://www.youtube.com/watch?v=").resolved(QUrl(script)),[self=shared_from_this()](YouTubeResolver::Page page){if(!page.error.isEmpty()){self->finish({{},page.error});return;}self->actions=YouTubeResolver::extractActions(QString::fromUtf8(page.body));if(self->actions.isEmpty()){self->finish({{},"Could not extract signature deciphering actions"});return;}
        QJsonArray result;QHash<QString,int> positions;for(const auto &v:self->formats){auto f=v.toObject();auto cipher=f.value("signatureCipher");if(!truth(cipher))cipher=f.value("cipher");if(truth(cipher)){auto fields=queryObject(cipher.toString());for(auto i=fields.begin();i!=fields.end();++i)f[i.key()]=i.value();f.remove("signatureCipher");f.remove("cipher");}if(truth(f.value("url"))){auto url=downloadUrl(f.value("url").toString(),truth(f.value("s"))?YouTubeResolver::decipher(self->actions,f.value("s").toString()):QString{},f.value("sp").toString());if(url.isEmpty()){self->finish({{},"Invalid URL"});return;}f["url"]=url;}auto key=f.contains("url")?string(f.value("url")):"undefined";if(positions.contains(key))result[positions[key]]=f;else{positions[key]=result.size();result.append(f);}}self->formats=result;self->manifests();});}
    void manifests(){const auto streaming=player.value("streamingData").toObject();const auto dash=streaming.value("dashManifestUrl").toString();const auto hls=streaming.value("hlsManifestUrl").toString();
        if(!dash.isEmpty())get(QUrl("https://www.youtube.com/watch?v=").resolved(QUrl(dash)),[self=shared_from_this(),dash,hls](YouTubeResolver::Page page){if(!page.error.isEmpty()){self->finish({{},page.error});return;}QXmlStreamReader xml(page.body);QXmlStreamAttributes adaptation;QJsonObject last;while(!xml.atEnd()){xml.readNext();if(!xml.isStartElement())continue;auto name=xml.name().toString().toUpper();if(name=="ADAPTATIONSET")adaptation=xml.attributes();else if(name=="REPRESENTATION"){auto a=xml.attributes();bool valid=false;int itag=a.value("id").toInt(&valid);if(!valid)continue;last={{"itag",itag},{"url",dash},{"bitrate",a.value("bandwidth").toDouble()},{"mimeType",adaptation.value("mimeType").toString()+"; codecs=\""+a.value("codecs").toString()+"\""}};if(a.hasAttribute("height")){last["width"]=a.value("width").toInt();last["height"]=a.value("height").toInt();last["fps"]=a.value("frameRate").toInt();}else last["audioSampleRate"]=a.value("audioSamplingRate").toString();}}
            if(xml.hasError()){self->finish({{},"Invalid DASH manifest"});return;}if(!last.isEmpty())self->merge(last);self->hls(hls);});else this->hls(hls);
    }
    void merge(QJsonObject f){auto url=f.value("url");for(int i=0;i<formats.size();++i)if(formats[i].toObject().value("url")==url){formats[i]=f;return;}formats.append(f);}
    void hls(const QString &url){if(url.isEmpty()){finish(YouTubeResolver::chooseFormat(formats));return;}get(QUrl("https://www.youtube.com/watch?v=").resolved(QUrl(url)),[self=shared_from_this()](YouTubeResolver::Page page){if(!page.error.isEmpty()){self->finish({{},page.error});return;}for(const auto &line:QString::fromUtf8(page.body).split('\n'))if(line.startsWith("http://")||line.startsWith("https://")){const auto itag=match(line,"/itag/(\\d+)/");if(itag.isEmpty()){self->finish({{},"Cannot read properties of null (reading '1')"});return;}self->merge({{"itag",itag.toInt()},{"url",line}});}self->finish(YouTubeResolver::chooseFormat(self->formats));});}
};
}
YouTubeResolver::YouTubeResolver(QObject *parent,Fetch fetch):QObject(parent),state_(std::make_shared<State>()) {
    state_->owner=this;
    if(fetch){state_->fetch=std::move(fetch);return;}
    auto *manager=new QNetworkAccessManager(this);QPointer<QNetworkAccessManager> guarded=manager;
    state_->fetch=[guarded](const QUrl &url,PageCallback cb){
        const auto host=url.host().toLower();const bool allowed=host=="youtube.com"||host.endsWith(".youtube.com")||host=="googlevideo.com"||host.endsWith(".googlevideo.com");
        if(!guarded){cb({{},"YouTube resolver unavailable",0});return;}
        if(!allowed||(url.scheme()!="https"&&url.scheme()!="http")||!url.userInfo().isEmpty()||(url.port()!=-1&&url.port()!=443&&url.port()!=80)){cb({{},"YouTube resource destination is not allowed",0});return;}
        QNetworkRequest request(url);request.setRawHeader("User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/87.0.4280.101 Safari/537.36");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);request.setTransferTimeout(30000);
        auto *reply=guarded->get(request);auto bytes=std::make_shared<QByteArray>();auto tooLarge=std::make_shared<bool>(false);
        QObject::connect(reply,&QIODevice::readyRead,reply,[reply,bytes,tooLarge]{bytes->append(reply->readAll());if(bytes->size()>16*1024*1024){*tooLarge=true;reply->abort();}});
        QObject::connect(reply,&QNetworkReply::finished,reply,[reply,bytes,tooLarge,cb=std::move(cb)]()mutable{bytes->append(reply->readAll());const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();QString error;if(*tooLarge)error="YouTube response exceeds safe size limit";else if(status>=300&&status<400)error="YouTube redirect requires review";else if(status>=400)error="Status code: "+QString::number(status);else if(reply->error()!=QNetworkReply::NoError)error=reply->errorString();reply->deleteLater();cb({*bytes,error,status});});
    };
}
YouTubeResolver::~YouTubeResolver(){
    // Complete deferred consumers before the transport children disappear.
    auto pending=std::move(state_->pending);state_->pending.clear();for(auto list:pending)for(auto &cb:list)cb({{},"YouTube resolver unavailable"});
}
void YouTubeResolver::resolve(const QString &input,Callback cb) {
    const QUrl link("http://www.youtube.com/watch?v="+input);const auto raw=QUrlQuery(link).queryItemValue("v",QUrl::FullyDecoded);const auto id=raw.left(11);
    if(raw.isEmpty()){cb({{},"No video id found: http://www.youtube.com/watch?v="+input});return;}
    if(!QRegularExpression("^[a-zA-Z0-9-_]{11}$").match(id).hasMatch()){cb({{},"Video id ("+id+") does not match expected format (/^[a-zA-Z0-9-_]{11}$/)"});return;}
    auto state=state_;const auto now=QDateTime::currentMSecsSinceEpoch();if(state->cache.contains(id)&&state->cache[id].expires>now){cb(state->cache[id].result);return;}
    if(state->pending.contains(id)){state->pending[id].append(std::move(cb));return;}state->pending[id].append(std::move(cb));
    auto job=std::make_shared<Job>();job->id=id;job->fetch=state->fetch;job->done=[state,id](Result result){if(!state->owner)return;if(result.error.isEmpty()){if(state->cache.size()>=256)state->cache.erase(state->cache.begin());state->cache[id]={result,QDateTime::currentMSecsSinceEpoch()+1000};}auto callbacks=state->pending.take(id);for(auto &cb:callbacks)cb(result);};job->next();
}
}
