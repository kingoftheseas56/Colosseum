#include "server1/http/ArchiveRoutes.h"
#include "server1/http/HttpContract.h"
#include "server1/http/SafeFileRegex.h"
#include "server1/archive/ArchiveByteSource.h"
#include "server1/archive/ZipSource.h"
#include "server1/archive/TarSource.h"
#include "server1/archive/GzipSource.h"
#include "server1/archive/RarSevenZipSource.h"
#include <QRegularExpression>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <cstring>
#include <map>
namespace server1::http {
namespace {
using namespace server1::archive;
struct Request;
struct Registry {
    SourceFamily family;
    ArchiveKeys keys;
    explicit Registry(SourceFamily f):family(f),keys(f){}
    QHash<QString,ArchiveStoredEntry> storedCached;
    QHash<QString,TarEntry> tarCached;
    QHash<QString,ZipEntry> cached;
    QHash<QString,QList<std::weak_ptr<Request>>> waiting;
};
QByteArray mime(const QString& name) {
    static const std::map<std::string,std::string> types={
#include "TorrentMime.inc"
    };
    auto found=types.find(QFileInfo(name).suffix().toLower().toStdString());
    return found==types.end()?QByteArray("application/octet-stream"):QByteArray::fromStdString(found->second);
}
void body(void*response,int status,const QByteArray&bytes,const char*type=nullptr) {
    server1_http_response_set_status(response,status);
    if(type)server1_http_response_set_header(response,"content-type",type);
    server1_http_response_set_body(response,bytes.constData(),size_t(bytes.size()));
}
struct Request : std::enable_shared_from_this<Request> {
    std::shared_ptr<Registry> registry;
    QString key,method,range,origin,target;QJsonObject options;
    void* deferred=nullptr;void* wake=nullptr;
    std::unique_ptr<ArchiveByteSource> source;
    std::unique_ptr<ZipSource> zip;
    std::unique_ptr<ZipEntryReader> reader;
    std::unique_ptr<GzipReader> gzip;
    std::unique_ptr<TarSource> tar;
    std::unique_ptr<TarEntryReader> tarReader;
    std::unique_ptr<RarSevenZipSource> storedArchive;
    std::unique_ptr<ArchiveStoredReader> storedReader;
    std::function<void(qint64,ZipEntryReader::Completion)> producer;
    qint64 remaining=-1;
    bool selectionError=false;
    QByteArray buffer;qsizetype position=0;bool pending=false,eof=false,failed=false;
    ~Request(){producer={};storedReader.reset();storedArchive.reset();tarReader.reset();tar.reset();gzip.reset();reader.reset();zip.reset();source.reset();server1_http_stream_wake_destroy(wake);server1_http_deferred_destroy(deferred);}
    void finish(int status,const QByteArray&bytes={}) {
        auto response=server1_http_deferred_response(deferred);if(!response)return;
        body(response,status,bytes);server1_http_deferred_complete(deferred);
    }
    std::ptrdiff_t pull(void*destination,std::size_t capacity) {
        if(!capacity)return 0;
        if(position<buffer.size()) {
            auto count=qMin<qsizetype>(buffer.size()-position,qsizetype(qMin<std::size_t>(capacity,65536)));
            std::memcpy(destination,buffer.constData()+position,size_t(count));position+=count;
            if(position==buffer.size()){buffer.clear();position=0;}
            return count;
        }
        if(failed)return -1;
        if(eof||remaining==0)return 0;
        if(!pending){pending=true;std::weak_ptr<Request> weak=shared_from_this();producer(remaining<0?65536:qMin<qint64>(65536,remaining),[weak](ZipChunk result){
            auto self=weak.lock();if(!self)return;
            self->pending=false;
            if(self->remaining>=0){if(result.bytes.size()>self->remaining)result.bytes.truncate(self->remaining);self->remaining-=result.bytes.size();}
            self->buffer=std::move(result.bytes);self->position=0;self->eof=result.eof;self->failed=!result.error.isEmpty()||(result.eof&&self->remaining>0);
            server1_http_stream_wake_notify(self->wake);
        });}
        return SERVER1_HTTP_WOULD_BLOCK;
    }
    void serve(ZipEntry entry) {
        auto response=server1_http_deferred_response(deferred);if(!response)return;
        auto plan=zipRangePlan(entry,method,range);
        if(!plan.contentRange.isEmpty())server1_http_response_set_header(response,"content-range",plan.contentRange.toUtf8().constData());
        // Reject negative/inverted wire lengths instead of emitting malformed HTTP.
        if(plan.status>=400||plan.contentLength<0){finish(plan.contentLength<0?500:plan.status);return;}
        server1_http_response_set_status(response,plan.status);
        server1_http_response_set_header(response,"content-type",mime(entry.name).constData());
        server1_http_response_set_header(response,"content-length",QByteArray::number(plan.contentLength).constData());
        if(plan.acceptRanges){
            server1_http_response_set_header(response,"accept-ranges","bytes");
            server1_http_response_set_header(response,"access-control-allow-origin",(origin.isEmpty()?QByteArray("*"):origin.toUtf8()).constData());
            server1_http_response_set_header(response,"transferMode.dlna.org","Streaming");
            server1_http_response_set_header(response,"contentFeatures.dlna.org","DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000");
        }
        if(plan.head){server1_http_deferred_complete(deferred);return;}
        if(entry.method==0){entry.dataOffset=plan.sourceStart;entry.size=entry.compressedSize=plan.contentLength;}
        reader=std::make_unique<ZipEntryReader>(entry,
            [this](qint64 offset,qint64 count,auto cb){return source->read(offset,count,std::move(cb));},
            [this](quint64 token){source->cancel(token);});
        producer=[this](qint64 count,auto callback){reader->pull(count,std::move(callback));};
        stream(plan.contentLength);
    }
    void stream(qint64 length) {
        auto response=server1_http_deferred_response(deferred);if(!response)return;
        remaining=length;
        auto context=new std::shared_ptr<Request>(shared_from_this());
        if(!server1_http_response_set_async_stream(response,
            [](void*context,void*destination,std::size_t count){return (*static_cast<std::shared_ptr<Request>*>(context))->pull(destination,count);},
            [](void*context){delete static_cast<std::shared_ptr<Request>*>(context);},context,length<0?SERVER1_HTTP_UNKNOWN_CONTENT_LENGTH:size_t(length))){delete context;finish(500);return;}
        wake=server1_http_response_stream_wake_handle(response);
        server1_http_deferred_complete(deferred);
    }
    bool matches(const QString&name,int index) {
        QStringList patterns;for(auto value:options["fileMustInclude"].toArray())patterns.append(value.toString());
        if(patterns.isEmpty()&&!options.contains("fileIdx"))patterns.append("/.mkv$|.mp4$|.avi$|.ts$/i");
        if(patterns.isEmpty())return !options.contains("fileIdx")||(options["fileIdx"].isDouble()&&options["fileIdx"].toDouble()==index);
        for(const auto&pattern:patterns){auto result=matchFilePattern(name,pattern);if(!result.valid){selectionError=true;return true;}if(result.matches.value_or(false))return true;}
        return false;
    }
    void gzipHeaders(QString name,qint64 length) {
        auto response=server1_http_deferred_response(deferred);if(!response)return;
        if(!range.isEmpty()&&range!="bytes=0-"&&(length<=0||range!="bytes=0-"+QString::number(length-1))){finish(405);return;}
        server1_http_response_set_status(response,method=="HEAD"?204:200);
        server1_http_response_set_header(response,"content-type",name.isEmpty()?"application/octet-stream":mime(name).constData());
        server1_http_response_set_header(response,"accept-ranges","none");
        if(length>0)server1_http_response_set_header(response,"content-length",QByteArray::number(length).constData());
        if(method=="HEAD"){server1_http_deferred_complete(deferred);return;}
        stream(length>0?length:-1);
    }
    void tarSelected(TarEntry entry,QString error) {
        if(selectionError||!error.isEmpty()){finish(500);return;}
        if(registry->family==SourceFamily::Tar){
            registry->tarCached[key]=entry;
            ZipEntry stored;stored.name=entry.name;stored.dataOffset=entry.dataOffset;stored.size=stored.compressedSize=entry.size;
            serve(stored);return;
        }
        tarReader=std::make_unique<TarEntryReader>(entry,
            [this](qint64 offset,qint64 count,auto callback){return gzip->readForward(offset,count,std::move(callback));},
            [this](quint64 token){gzip->cancel(token);});
        producer=[this](qint64 count,auto callback){tarReader->pull(count,std::move(callback));};
        gzipHeaders(entry.name,entry.size);
    }
    void startTar(bool compressed) {
        ZipSource::Read read;ZipSource::Cancel cancel;
        if(compressed){
            gzip=std::make_unique<GzipReader>(source->size(),
                [this](qint64 offset,qint64 count,auto cb){return source->read(offset,count,std::move(cb));},
                [this](quint64 token){source->cancel(token);});
            auto urls=*registry->keys.get(key);auto first=urls.first();auto url=(first.isObject()?first.toObject()["url"].toString():first.toString()).section('?',0,0).toLower();
            if(!url.contains(".tar.gz")&&!url.contains(".tgz")){
                producer=[this](qint64 count,auto callback){gzip->pull(count,std::move(callback));};
                gzipHeaders({},-1);return;
            }
            read=[this](qint64 offset,qint64 count,auto callback){return gzip->readForward(offset,count,std::move(callback));};
            cancel=[this](quint64 token){gzip->cancel(token);};
        }else{
            auto cached=registry->tarCached.constFind(key);if(cached!=registry->tarCached.cend()){tarSelected(*cached,{});return;}
            read=[this](qint64 offset,qint64 count,auto callback){return source->read(offset,count,std::move(callback));};
            cancel=[this](quint64 token){source->cancel(token);};
        }
        tar=std::make_unique<TarSource>(compressed?-1:source->size(),std::move(read),std::move(cancel),nullptr,compressed);
        std::weak_ptr<Request> weak=shared_from_this();auto index=std::make_shared<int>(-1);
        tar->find([weak,index](const TarEntry&entry){if(entry.type!="file")return false;auto self=weak.lock();return self&&self->matches(entry.name,++*index);},
            [weak](TarEntry entry,QString error){if(auto self=weak.lock())self->tarSelected(std::move(entry),error);});
    }
    void directory(ZipDirectory directory) {
        if(!directory.error.isEmpty()){finish(500,"There was an error with the zip parser.");return;}
        QStringList patterns;for(auto value:options["fileMustInclude"].toArray())patterns.append(value.toString());
        if(patterns.isEmpty()&&!options.contains("fileIdx"))patterns.append("/.mkv$|.mp4$|.avi$|.ts$/i");
        std::optional<ZipEntry> selected;
        for(int index=0;index<directory.entries.size();++index){
            auto entry=directory.entries[index];bool matches=patterns.isEmpty()&&(!options.contains("fileIdx")||(options["fileIdx"].isDouble()&&options["fileIdx"].toDouble()==index));
            for(const auto&pattern:patterns){auto result=matchFilePattern(entry.name,pattern);if(!result.valid){finish(500);return;}if(result.matches.value_or(false)){matches=true;break;}}
            if(matches){selected=entry;break;}
        }
        if(!selected){finish(500,"There was an error with the zip parser.");return;}
        std::weak_ptr<Request> weak=shared_from_this();
        zip->localEntry(*selected,[weak](ZipEntry entry,QString error){auto self=weak.lock();if(!self)return;if(!error.isEmpty()){self->finish(500,error.toUtf8());return;}if(!entry.method)self->registry->cached[self->key]=entry;self->serve(entry);});
    }
    QByteArray storedError() const {
        return registry->family==SourceFamily::Rar?"There was an error with the rar parser.":"There was an error with the 7zip parser.";
    }
    void storedSelected(ArchiveStoredEntry entry,QString error) {
        if(selectionError||!error.isEmpty()){finish(500,storedError());return;}
        registry->storedCached[target]=entry;
        auto response=server1_http_deferred_response(deferred);if(!response)return;
        qint64 start=0,end=entry.length-1;int status=method=="HEAD"?204:200;
        if(method!="HEAD"&&!range.isEmpty()){
            auto text=range;int marker=text.indexOf("bytes=");if(marker>=0)text.remove(marker,6);
            auto parts=text.split('-');auto parse=[](const QString &s,bool *valid){auto match=QRegularExpression("^\\s*([+-]?\\d+)").match(s);if(!match.hasMatch()){*valid=false;return qint64(0);}return match.captured(1).toLongLong(valid);};
            bool valid=false;start=parse(parts.value(0),&valid);if(!valid)start=0;
            if(!parts.value(1).isEmpty()){end=parse(parts.value(1),&valid);if(!valid){finish(500);return;}}
            status=206;
        }
        // Invalid source ranges throw after headers in JS. Do not emit malformed
        // HTTP or retain a hung response for negative/inverted/out-of-bounds input.
        if(method!="HEAD"&&!(range.isEmpty()&&entry.length==0)&&(start<0||end<start||end>=entry.length)){finish(500);return;}
        auto count=method=="HEAD"?entry.length:end-start+1;
        server1_http_response_set_status(response,status);
        server1_http_response_set_header(response,"accept-ranges","bytes");
        server1_http_response_set_header(response,"content-type",mime(entry.name).constData());
        server1_http_response_set_header(response,"content-length",QByteArray::number(count).constData());
        if(status==206)server1_http_response_set_header(response,"content-range",("bytes "+QByteArray::number(start)+"-"+QByteArray::number(end)+"/"+QByteArray::number(entry.length)).constData());
        if(method=="HEAD"||count==0){server1_http_deferred_complete(deferred);return;}
        storedReader=std::make_unique<ArchiveStoredReader>(entry,
            [this](qint64 offset,qint64 count,auto cb){return source->read(offset,count,std::move(cb));},
            [this](quint64 token){source->cancel(token);},nullptr,start,end);
        producer=[this](qint64 count,auto callback){storedReader->pull(count,std::move(callback));};
        stream(count);
    }
    void startStored() {
        auto cached=registry->storedCached.constFind(target);if(cached!=registry->storedCached.cend()){storedSelected(*cached,{});return;}
        storedArchive=std::make_unique<RarSevenZipSource>(registry->family,source->size(),
            [this](qint64 offset,qint64 count,auto cb){return source->read(offset,count,std::move(cb));},
            [this](quint64 token){source->cancel(token);});
        storedArchive->setVolumeLengths(source->partLengths());std::weak_ptr<Request> weak=shared_from_this();
        storedArchive->find([weak](const QString&name,int index){auto self=weak.lock();return self&&self->matches(name,index);},
            [weak](ArchiveStoredEntry entry,QString error){if(auto self=weak.lock())self->storedSelected(std::move(entry),error);});
    }
    void start() {
        auto urls=registry->keys.get(key);if(!urls)return;
        source=std::make_unique<ArchiveByteSource>();std::weak_ptr<Request> weak=shared_from_this();
        if(registry->family==SourceFamily::Rar||registry->family==SourceFamily::SevenZip)*urls=orderArchiveStoredUrls(registry->family,*urls);
        source->open(registry->family,*urls,[weak](QString error){
            auto self=weak.lock();if(!self)return;
            if(!error.isEmpty()){auto family=self->registry->family;if(family==SourceFamily::Rar||family==SourceFamily::SevenZip){self->finish(500,self->storedError());return;}self->finish(500,family==SourceFamily::Zip?"There was an error with the zip parser.":family==SourceFamily::Tar?"There was an error with the tar parser.":"There was an error with the gzip parser.");return;}
            if(self->registry->family==SourceFamily::Rar||self->registry->family==SourceFamily::SevenZip){self->startStored();return;}
            if(self->registry->family!=SourceFamily::Zip){self->startTar(self->registry->family==SourceFamily::Gzip);return;}
            auto cached=self->registry->cached.constFind(self->key);
            if(cached!=self->registry->cached.cend()){self->serve(*cached);return;}
            self->zip=std::make_unique<ZipSource>(self->source->size(),
                [weak](qint64 offset,qint64 count,auto cb){auto current=weak.lock();return current?current->source->read(offset,count,std::move(cb)):quint64(0);},
                [weak](quint64 token){if(auto current=weak.lock())current->source->cancel(token);});
            self->zip->setVolumeLengths(self->source->partLengths());
            self->zip->open([weak](ZipDirectory result){if(auto current=weak.lock())current->directory(std::move(result));});
        });
    }
};
struct Context {std::shared_ptr<Registry> registry;bool create;};
int handle(const void*request,void*response,server1_http_next_fn,void*,void*opaque) {
    auto& context=*static_cast<Context*>(opaque);auto registry=context.registry;
    const auto query=[&](const char*name){return QString::fromUtf8(server1_http_request_query_value(request,name));};
    auto method=QString::fromUtf8(server1_http_request_method(request));
    if(context.create) {
        QJsonParseError parse;auto document=QJsonDocument::fromJson(QByteArray(server1_http_request_body(request),qsizetype(server1_http_request_body_size(request))),&parse);
        auto value=document.isArray()?QJsonValue(document.array()):QJsonValue(document.object());
        auto result=registry->keys.create(method,value,QString::fromUtf8(server1_http_request_param(request,"createKey")),query("lz"));
        if(!result.error.isEmpty()){body(response,result.status,result.error.toUtf8(),"text/plain");return 1;}
        if(result.status==302){server1_http_response_set_header(response,"location",result.location.toUtf8().constData());body(response,302,{});}
        else {auto bytes=QJsonDocument(QJsonObject{{"key",result.key}}).toJson(QJsonDocument::Compact);server1_http_response_set_header(response,"content-length",QByteArray::number(bytes.size()).constData());body(response,200,bytes,"application/json");}
        auto waiting=registry->waiting.take(result.key);for(auto weak:waiting)if(auto pending=weak.lock())pending->start();
        return 1;
    }
    const auto key=query("key");if(key.isEmpty()){body(response,500,{});return 1;}
    auto pending=std::make_shared<Request>();pending->registry=registry;pending->key=key;pending->method=method;pending->target=QString::fromUtf8(server1_http_request_target(request));pending->range=QString::fromUtf8(server1_http_request_header_value_at(request,"range",0));pending->origin=QString::fromUtf8(server1_http_request_header_value_at(request,"origin",0));pending->options=QJsonDocument::fromJson(query("o").toUtf8()).object();
    pending->deferred=server1_http_response_defer(response);if(!pending->deferred)return 0;
    auto lifetime=new std::shared_ptr<Request>(pending);
    if(!server1_http_response_set_lifetime(response,[](void*context){delete static_cast<std::shared_ptr<Request>*>(context);},lifetime)){delete lifetime;return 0;}
    if(registry->keys.get(key))pending->start();else{
        auto&waiting=registry->waiting[key];
        // Retired responses never retain a producer or an event listener.
        for(auto it=waiting.begin();it!=waiting.end();)if(it->expired())it=waiting.erase(it);else ++it;
        waiting.append(pending);
    }
    return 1;
}
}
void mountArchiveRoutes(void*router) {
    for(auto family:{SourceFamily::Zip,SourceFamily::Tar,SourceFamily::Gzip,SourceFamily::Rar,SourceFamily::SevenZip}){
        auto registry=std::make_shared<Registry>(family);
        QString prefix=family==SourceFamily::Rar?"/rar":family==SourceFamily::SevenZip?"/7zip":family==SourceFamily::Zip?"/zip":family==SourceFamily::Tar?"/tar":"/tgz";
        for(auto item:{std::pair<const char*,const char*>{"POST","/create/:createKey"},{"ALL","/create"},{"GET","/stream"}}){
            auto path=(prefix+QString::fromLatin1(item.second)).toUtf8();
            server1_http_router_add_handler(router,0,0,item.first,path.constData(),handle,new Context{registry,QString::fromLatin1(item.second).contains("/create")},[](void*context){delete static_cast<Context*>(context);});
        }
    }
}
}
