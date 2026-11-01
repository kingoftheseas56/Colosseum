#include "server1/http/TorrentRoutes.h"
#include "server1/http/FileReaderStream.h"
#include "server1/http/HttpContract.h"
#include "server1/http/TorrentRange.h"
#include "server1/http/SafeFileRegex.h"
#include "server1/http/SystemStatistics.h"
#include "server1/http/GuessFileIndex.h"
#include <stdexcept>
#include <QUrl>
#include <QFile>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrlQuery>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>

namespace server1::http {
namespace {
using namespace policy;
QString utf8String(const std::string &text){
    QStringDecoder decode(QStringDecoder::Utf8,QStringConverter::Flag::ConvertInitialBom);
    return decode(QByteArrayView(text.data(),static_cast<qsizetype>(text.size())));
}
struct Deferred {
    void *handle;
    explicit Deferred(void *response):handle(server1_http_response_defer(response)){}
    ~Deferred(){server1_http_deferred_destroy(handle);}
    void *response() const{return server1_http_deferred_response(handle);}
    void complete(){server1_http_deferred_complete(handle);}
};
struct CreateContext : QObject {
    EngineRegistry &registry;
    QNetworkAccessManager network;
    explicit CreateContext(EngineRegistry &r):registry(r),network(this){}
};
struct StreamLifetime {std::shared_ptr<TorrentEngine> engine;std::shared_ptr<EngineStreamLease> lease;};
void empty(void *response,int status){server1_http_response_set_status(response,status);server1_http_response_set_body(response,"",0);}
std::string mime(std::string name){
    static const std::map<std::string,std::string> types={
#include "TorrentMime.inc"
    };
    auto pos=name.find_last_of("./\\");if(pos!=std::string::npos)name.erase(0,pos+1);
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    auto found=types.find(name);return found==types.end()?"application/octet-stream":found->second;
}
int cors(const void *request,void *response,server1_http_next_fn next,void *continuation,void *){
    if(*server1_http_request_header_value_at(request,"origin",0)){
        server1_http_response_set_header(response,"Access-Control-Allow-Origin","*");
        if(std::string(server1_http_request_method(request))=="OPTIONS"){
            server1_http_response_set_header(response,"Access-Control-Allow-Methods","POST, GET, OPTIONS");
            const auto requested=server1_http_request_header_value_at(request,"access-control-request-headers",0);
            server1_http_response_set_header(response,"Access-Control-Allow-Headers",*requested?requested:"Range");
            server1_http_response_set_header(response,"Access-Control-Max-Age","1728000");empty(response,200);return 1;
        }
    }
    return next(continuation);
}
void json(void *response,const Value &value){
    const auto bytes=jsonStringify(value);server1_http_response_set_status(response,200);
    server1_http_response_set_header(response,"Content-Type","application/json");
    server1_http_response_set_body(response,bytes.data(),bytes.size());
}
int favicon(const void *,void *response,server1_http_next_fn,void *,void *){
    empty(response,404);server1_http_response_set_header(response,"Content-Type","application/json");return 1;
}
int stats(const void *request,void *response,server1_http_next_fn,void *,void *context){
    auto &registry=*static_cast<EngineRegistry *>(context);
    const std::string hash=server1_http_request_param(request,"infoHash");
    const std::string index=server1_http_request_param(request,"idx");
    const auto keys=registry.list();
    if(hash.empty()){Value::Object all;if(std::string(server1_http_request_target(request)).find("sys=1")!=std::string::npos)all.emplace_back("sys",systemStatistics());for(const auto &key:keys)all.emplace_back(key,registry.statistics(key));json(response,Value::object(std::move(all)));}
    else if(std::find(keys.begin(),keys.end(),hash)==keys.end())json(response,Value::null());
    else json(response,registry.statistics(hash,index.empty()?Value::missing():Value::string(index)));
    return 1;
}
Value fromJson(const QJsonValue &value){
    if(value.isNull() || value.isUndefined())return Value::null();
    if(value.isBool())return Value::boolean(value.toBool());
    if(value.isDouble())return Value::number(value.toDouble());
    if(value.isString())return Value::string(value.toString().toStdString());
    if(value.isArray()){Value::Array result;for(const auto &item:value.toArray())result.push_back(fromJson(item));return Value::array(std::move(result));}
    Value::Object result;const auto object=value.toObject();for(auto it=object.begin();it!=object.end();++it)result.emplace_back(it.key().toStdString(),fromJson(it.value()));return Value::object(std::move(result));
}
Value requestOptions(const void *request){
    const auto bytes=QByteArray(server1_http_request_body(request),static_cast<qsizetype>(server1_http_request_body_size(request)));
    const auto document=QJsonDocument::fromJson(bytes);
    return document.isObject()?fromJson(document.object()):Value::object({});
}
template<class Files>
std::optional<std::size_t> filteredFile(const Files &files,const QStringList &patterns){
    QStringList names;for(const auto &file:files)names.push_back(utf8String(file.name));
    return selectFilePattern(names,patterns);
}
void createResponse(EngineRegistry &registry,std::string hash,Value options,const std::shared_ptr<Deferred> &deferred){
    const auto guess=options.find("guessFileIdx")?*options.find("guessFileIdx"):Value::missing();
    QStringList patterns;
    if(const auto *value=options.find("fileMustInclude");value && value->kind()==Value::Kind::Array)
        for(const auto &item:value->asArray())if(item.kind()==Value::Kind::String)patterns.push_back(utf8String(item.asString()));
    registry.create({std::move(hash),std::move(options)},[deferred,patterns,guess](EngineCreateResult result){
        auto *response=deferred->response();if(!response)return;
        if(result.outcome==EngineCreateOutcome::Ready && result.engine){
            try{
                auto stats=result.engine->statistics();
                if(auto selected=filteredFile(result.engine->files(),patterns)){
                    auto object=stats.asObject();object.emplace_back("guessedFileIdx",Value::number(*selected));stats=Value::object(std::move(object));
                }
                if(!stats.find("guessedFileIdx") && jsTruthy(guess)){
                    const auto *files=stats.find("files");
                    if(files){auto object=stats.asObject();object.emplace_back("guessedFileIdx",guessFileIndex(*files,guess));stats=Value::object(std::move(object));}
                }
                json(response,stats);
            }catch(const std::exception &){empty(response,500);}
        }
        else empty(response,500);
        deferred->complete();
    });
}
int create(const void *request,void *response,server1_http_next_fn,void *,void *context){
    auto &owner=*static_cast<CreateContext *>(context);
    auto &registry=owner.registry;
    auto options=requestOptions(request);
    std::string hash=server1_http_request_param(request,"infoHash");
    empty(response,200);
    auto deferred=std::make_shared<Deferred>(response);
    if(!hash.empty()){createResponse(registry,std::move(hash),std::move(options),deferred);return 1;}
    const auto onBlob=[&registry,deferred](QByteArray raw){
        Value::Bytes bytes(raw.begin(),raw.end());
        auto metadata=TorrentMetadata::parse(bytes);
        if(!metadata){if(auto *response=deferred->response()){empty(response,500);deferred->complete();}return;}
        createResponse(registry,metadata->infoHash(),Value::object({{"torrent",Value::bytes(std::move(bytes))}}),deferred);
    };
    const auto *blob=options.find("blob");
    if(blob && blob->kind()==Value::Kind::String){
        const auto hex=blob->asString();QByteArray bytes;
        auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
        for(std::size_t n=0;n+1<hex.size();n+=2){const auto high=digit(hex[n]),low=digit(hex[n+1]);if(high<0 || low<0)break;bytes.append(static_cast<char>(high*16+low));}
        onBlob(bytes);
    }else if(const auto *from=options.find("from");from && from->kind()==Value::Kind::String){
        const auto path=QString::fromStdString(from->asString());
        if(path.startsWith("http")){
            auto *reply=owner.network.get(QNetworkRequest(QUrl(path)));
            QObject::connect(reply,&QNetworkReply::finished,&owner,[reply,onBlob]{
                // HTTP error bodies remain parseable, like node-fetch.buffer().
                onBlob(reply->readAll());reply->deleteLater();
            });
        }else{
            auto *watcher=new QFutureWatcher<QByteArray>(&owner);
            QObject::connect(watcher,&QFutureWatcher<QByteArray>::finished,&owner,[watcher,onBlob]{onBlob(watcher->result());watcher->deleteLater();});
            watcher->setFuture(QtConcurrent::run([path]{QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}));
        }
    }else{empty(response,500);deferred->complete();}
    return 1;
}
int remove(const void *request,void *response,server1_http_next_fn,void *,void *context){
    auto &registry=*static_cast<EngineRegistry *>(context);const std::string hash=server1_http_request_param(request,"infoHash");
    empty(response,200);
    if(hash.empty()){for(const auto &key:registry.list())registry.remove(key);json(response,Value::object({}));return 1;}
    auto deferred=std::make_shared<Deferred>(response);
    registry.remove(hash,[deferred](bool){if(auto *response=deferred->response()){json(response,Value::object({}));deferred->complete();}});
    return 1;
}
int torrent(const void *request,void *response,server1_http_next_fn,void *,void *context){
    auto &registry=*static_cast<EngineRegistry *>(context);
    server1_http_response_set_header(response,"transferMode.dlna.org","Streaming");
    server1_http_response_set_header(response,"contentFeatures.dlna.org","DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=017000 00000000000000000000000000");
    const std::string hash=server1_http_request_param(request,"infoHash");
    const std::string index=server1_http_request_param(request,"idx");
    const std::string method=server1_http_request_method(request);
    const std::string range=server1_http_request_header_value_at(request,"range",0);
    const std::string priority=server1_http_request_header_value_at(request,"enginefs-prio",0);
    auto queryUrl=QUrl::fromEncoded(server1_http_request_target(request));
    queryUrl.setQuery(queryUrl.query(QUrl::FullyEncoded).replace("+","%20"),QUrl::StrictMode);
    const QUrlQuery query(queryUrl);
    const auto download=query.queryItemValue("download",QUrl::FullyDecoded);
    const auto external=query.queryItemValue("external",QUrl::FullyDecoded);
    const auto subtitles=query.queryItemValue("subtitles",QUrl::FullyDecoded);
    const auto trackers=query.allQueryItemValues("tr",QUrl::FullyDecoded);
    auto patterns=query.allQueryItemValues("f",QUrl::FullyDecoded);
    if(patterns.size()==1 && patterns.front().isEmpty())patterns.clear();
    Value createOptions=Value::object({});
    if(trackers.size()>1 || (trackers.size()==1 && !trackers.front().isEmpty())){
        Value::Array sources;for(const auto &tracker:trackers)sources.push_back(Value::string(tracker.toStdString()));
        createOptions=Value::object({{"peerSearch",Value::object({{"min",Value::number(40)},{"max",Value::number(200)},{"sources",Value::array(std::move(sources))}})}});
    }
    empty(response,200);
    auto deferred=std::make_shared<Deferred>(response);
    registry.create({hash,std::move(createOptions)},[deferred,&registry,index,range,priority,method,download,external,subtitles,patterns](EngineCreateResult result){
        auto *response=deferred->response();if(!response)return;
        if(result.outcome!=EngineCreateOutcome::Ready || !result.engine){empty(response,500);deferred->complete();return;}
        const auto engine=result.engine;const auto files=engine->files();
        const auto number=index.empty()?-1:jsNumber(Value::string(index));
        std::optional<std::size_t> selected;
        try{selected=filteredFile(files,patterns);}catch(const std::exception &){empty(response,500);deferred->complete();return;}
        if(!selected && std::isnan(number)){
            for(std::size_t i=0;i<files.size();++i)if(files[i].name==index){selected=i;break;}
        }else if(!selected && number>=0 && number<static_cast<double>(files.size()) && std::floor(number)==number)selected=static_cast<std::size_t>(number);
        else if(!selected && number==-1){
            const auto stats=engine->statistics();
            if(const auto *entries=stats.find("files")){
                const auto guessed=guessFileIndex(*entries,Value::object({})).asNumber();
                if(guessed>=0)selected=static_cast<std::size_t>(guessed);
            }
        }
        if(!selected){empty(response,500);deferred->complete();return;}
        const auto &file=files[*selected];
        if(!external.isEmpty()){
            auto location="/"+engine->sourceKey()+"/"+QUrl::toPercentEncoding(utf8String(file.name)).toStdString()+(!download.isEmpty()?"?download=1":"");
            empty(response,307);server1_http_response_set_header(response,"Location",location.c_str());deferred->complete();return;
        }
        const auto safeValue=[](const std::string &value){return std::none_of(value.begin(),value.end(),[](unsigned char c){return (c<0x20 && c!='\t') || c==0x7f;});};
        if((!download.isEmpty() && !safeValue(file.name)) || (!subtitles.isEmpty() && !safeValue(subtitles.toStdString()))){empty(response,500);deferred->complete();return;}
        server1_http_response_set_socket_timeout(response,86400000);
        auto lifetime=std::make_shared<StreamLifetime>(StreamLifetime{engine,registry.openStream(engine->sourceKey(),*selected)});
        auto *owned=new std::shared_ptr<StreamLifetime>(lifetime);
        if(!server1_http_response_set_lifetime(response,+[](void *p){delete static_cast<std::shared_ptr<StreamLifetime> *>(p);},owned)){delete owned;empty(response,500);deferred->complete();return;}
        server1_http_response_set_header(response,"Accept-Ranges","bytes");
        server1_http_response_set_header(response,"Content-Type",mime(file.name).c_str());
        server1_http_response_set_header(response,"Cache-Control","max-age=0, no-cache");
        if(!download.isEmpty())server1_http_response_set_header(response,"Content-Disposition",("attachment; filename=\""+file.name+"\";").c_str());
        if(!subtitles.isEmpty())server1_http_response_set_header(response,"CaptionInfo.sec",subtitles.toStdString().c_str());
        if(!range.empty() && range.back()=='-'){
            const auto options=engine->options();const auto *circular=options.find("circularBuffer");
            if(!circular || !jsTruthy(*circular))registry.prewarmStream(engine->sourceKey(),*selected);
        }
        FileReadOptions options;
        const auto parsed=firstTorrentRange(static_cast<std::size_t>(file.length),range);
        auto length=static_cast<std::size_t>(file.length);
        if(parsed){options.start=parsed->start;options.end=parsed->end;length=parsed->end-parsed->start+1;server1_http_response_set_status(response,206);server1_http_response_set_header(response,"Content-Range",("bytes "+std::to_string(parsed->start)+"-"+std::to_string(parsed->end)+"/"+std::to_string(file.length)).c_str());}
        server1_http_response_set_header(response,"Content-Length",std::to_string(length).c_str());
        if(!priority.empty()){auto n=jsParseInt(Value::string(priority));options.priority=Value::number(std::isnan(n)||n==0?1:n);}
        if(method!="HEAD" && length){
            try{if(!attachFileReader(response,engine->createReader(*selected,options),lifetime))empty(response,500);}
            catch(...){empty(response,500);}
        }
        deferred->complete();
    });
    return 1;
}
}
bool installTorrentRoutes(void *router,policy::EngineRegistry &registry){
    auto *context=new CreateContext(registry);
    if(!server1_http_router_add_handler(router,0,0,"ALL","/create",create,context,+[](void *p){delete static_cast<CreateContext *>(p);})){delete context;return false;}
    return server1_http_router_add_middleware(router,0,"/",cors,nullptr,nullptr)
        && server1_http_router_add_handler(router,0,0,"ALL","/:infoHash/create",create,context,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/removeAll",remove,&registry,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/:infoHash/remove",remove,&registry,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/favicon.ico",favicon,nullptr,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/stats.json",stats,&registry,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/:infoHash/stats.json",stats,&registry,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/:infoHash/:idx/stats.json",stats,&registry,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/:infoHash/:idx/:tail*",torrent,&registry,nullptr)
        && server1_http_router_add_handler(router,0,0,"GET","/:infoHash",torrent,&registry,nullptr);
}
}
