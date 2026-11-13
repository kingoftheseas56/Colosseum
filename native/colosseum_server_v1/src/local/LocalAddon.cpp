#include "server1/local/LocalAddon.h"
#include "server1/http/HttpContract.h"
#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QTimeZone>
#include <algorithm>
namespace server1::local {
namespace {
QString image(const QString &kind,const QString &id) { return "https://images.metahub.space/"+kind+"/medium/"+id+"/img"; }
QString videoId(const QJsonObject &f) {
    QString id=f["imdb_id"].toString();
    if(f["season"].toInt() && f["episode"].toInt())id+=":"+QString::number(f["season"].toInt())+":"+QString::number(f["episode"].toInt());
    return id;
}
QJsonObject genericMeta(const QJsonObject &entry) {
    auto files=entry["files"].toArray();QJsonObject largest,imdb;
    for(const auto &v:files){auto f=v.toObject();if(imdb.isEmpty()&&!f["imdb_id"].toString().isEmpty())imdb=f;if(!f["parsedName"].toString().isEmpty()&&(largest.isEmpty()||f["length"].toDouble()>largest["length"].toDouble()))largest=f;}
    QJsonObject result{{"id",entry["itemId"]},{"type","other"},{"name",largest.isEmpty()?entry["name"]:largest["parsedName"]},{"showAsVideos",true}};
    if(!imdb.isEmpty())for(const auto &kind:{"poster","background","logo"})result[kind]=image(kind,imdb["imdb_id"].toString());
    return result;
}
}
LocalAddon::LocalAddon(Config config,QObject *parent):QObject(parent),config_(std::make_shared<Config>(std::move(config))){}
LocalAddon::~LocalAddon(){destroying_=true;cancelIndexing();cancelRequests();}
QJsonObject LocalAddon::manifest() const {
    QJsonArray resources;
    if(!config_->disableCatalogSupport)resources.append("catalog");
    resources.append(QJsonObject{{"name","meta"},{"types",QJsonArray{"other"}},{"idPrefixes",QJsonArray{"local:","bt:"}}});
    resources.append(QJsonObject{{"name","stream"},{"types",QJsonArray{"movie","series"}},{"idPrefixes",QJsonArray{"tt"}}});
    return {{"id","org.stremio.local"},{"version","1.10.0"},{"description","Local add-on to find playable files: .torrent, .mp4, .mkv and .avi"},{"name",config_->disableCatalogSupport?"Local Files (without catalog support)":"Local Files"},{"resources",resources},{"types",QJsonArray{"movie","series","other"}},{"catalogs",config_->disableCatalogSupport?QJsonArray{}:QJsonArray{QJsonObject{{"type","other"},{"id","local"}}}}};
}
bool LocalAddon::saveEntry(const QString &key,const QJsonObject &entry){for(const auto &r:entries_)if(r.key==key)return false;entries_.push_back({key,entry});return true;}
void LocalAddon::saveMeta(const QString &id,const QJsonObject &meta){if(!metas_.contains(id))metas_[id]=meta;}
QJsonObject LocalAddon::aggregate(const QString &id) const {
    QJsonObject result;QJsonArray files;
    for(const auto &r:entries_)if(r.entry["itemId"]==id){if(result.isEmpty())result=r.entry;for(auto v:r.entry["files"].toArray())files.append(v);}
    if(!result.isEmpty())result["files"]=files;
    return result;
}
QJsonObject LocalAddon::catalog() const {
    QJsonArray output;QStringList seen;
    for(const auto &r:entries_){auto id=r.entry["itemId"].toString();if(id.isEmpty()||seen.contains(id))continue;seen.append(id);auto entry=aggregate(id);auto files=entry["files"].toArray();if(files.isEmpty())continue;
        if(metas_.contains(id)){output.append(metas_[id]);continue;}auto first=files.first().toObject();auto imdb=first["imdb_id"].toString();output.append(QJsonObject{{"id",id},{"type","other"},{"name",first["parsedName"].toString().isEmpty()?entry["name"]:first["parsedName"]},{"poster",imdb.isEmpty()?QJsonValue(QJsonValue::Null):QJsonValue(image("poster",imdb))}});
    }return {{"metas",output}};
}
QJsonObject LocalAddon::streams(const QString &type,const QString &id) const {
    QJsonArray output;QStringList seen;
    if(!id.startsWith("tt")||(type!="movie"&&type!="series"))return {{"streams",output}};
    auto local="local:"+id.section(':',0,0);
    for(const auto &r:entries_)if(r.entry["itemId"]==local){auto files=r.entry["files"].toArray();if(files.isEmpty())continue;auto f=files.first().toObject();if(f["type"]==type&&videoId(f)==id){auto url="file://"+f["path"].toString();output.append(QJsonObject{{"id",url},{"url",url},{"subtitle","ADDON_STREAM_LOCALFILE"},{"title",QFileInfo(f["path"].toString()).fileName()}});}}
    for(const auto &r:entries_){auto key=r.entry["itemId"].toString();if(!key.startsWith("bt:")||seen.contains(key))continue;seen.append(key);auto files=r.entry["files"].toArray();for(qsizetype i=0;i<files.size();++i){auto f=files[i].toObject();if(f["type"]!=type||videoId(f)!=id)continue;QJsonObject stream{{"title",QFileInfo(f["path"].toString()).fileName()},{"infoHash",r.entry["ih"]},{"fileIdx",i},{"id",r.entry["ih"].toString()+"/"+QString::number(i)}};if(r.entry.contains("sources"))stream["sources"]=r.entry["sources"];output.append(stream);}}
    return {{"streams",output}};
}
QJsonObject LocalAddon::meta(const QString &id,qint64 nowMs) const {
    auto entry=aggregate(id);if(entry.isEmpty())return {};
    return projectMeta(entry,nowMs);
}
QJsonObject LocalAddon::projectMeta(QJsonObject entry,qint64 nowMs,QJsonObject suppliedMeta) const {
    const auto id=entry.value("itemId").toString();
    auto array=entry["files"].toArray();std::vector<QJsonObject> files;for(auto v:array)files.push_back(v.toObject());
    // JS comparator returns NaN (treated as zero) for absent episode data.
    // Stable insertion avoids passing this non-total relation to std::sort.
    auto less=[](const QJsonObject&a,const QJsonObject&b){if(!a.contains("season")||!b.contains("season"))return false;auto season=a["season"].toInt()-b["season"].toInt();if(season)return season<0;if(!a.contains("episode")||!b.contains("episode"))return false;return a["episode"].toInt()<b["episode"].toInt();};
    for(size_t at=1;at<files.size();++at){auto value=files[at];auto index=at;while(index&&less(value,files[index-1])){files[index]=files[index-1];--index;}files[index]=value;}

    QJsonArray videos;int i=0;for(const auto &f:files){QJsonObject stream;
        if(!entry["ih"].toString().isEmpty()){stream={{"infoHash",entry["ih"]},{"fileIdx",f["idx"]},{"title",entry["ih"].toString()+"/"+QString::number(f["idx"].toInt())}};if(entry.contains("sources"))stream["sources"]=entry["sources"];}
        else stream={{"title",f["path"]},{"url","file://"+f["path"].toString()},{"subtitle","ADDON_STREAM_LOCALFILE"}};
        QStringList ids;if(!f["imdb_id"].toString().isEmpty())ids<<f["imdb_id"].toString();for(auto key:{"season","episode"})if(f[key].toInt())ids<<QString::number(f[key].toInt());
        QJsonObject video{{"id",ids.isEmpty()?stream["title"].toString():ids.join(':')},{"title",f["name"]},{"publishedAt",entry.contains("dateModified")?entry["dateModified"]:QJsonValue(QDateTime::fromMSecsSinceEpoch(nowMs,QTimeZone::UTC).toString(Qt::ISODateWithMs))},{"released",QDateTime::fromMSecsSinceEpoch(nowMs-60000*i++,QTimeZone::UTC).toString(Qt::ISODateWithMs)},{"stream",stream},{"thumbnail",QJsonValue::Null}};
        for(auto key:{"season","episode"})if(f.contains(key))video[key]=f[key];
        if(!f["imdb_id"].toString().isEmpty())video["thumbnail"]=(f["season"].toInt()&&f["episode"].toInt())?"https://episodes.metahub.space/"+f["imdb_id"].toString()+"/"+QString::number(f["season"].toInt())+"/"+QString::number(f["episode"].toInt())+"/w780.jpg":image("background",f["imdb_id"].toString());
        videos.append(video);
    }auto result=!suppliedMeta.isEmpty()?suppliedMeta:metas_.contains(id)?metas_[id].toObject():genericMeta(entry);result["videos"]=videos;return {{"meta",result}};
}
void LocalAddon::completeMeta(quint64 request,QJsonObject result){auto it=metaRequests_.find(request);if(it==metaRequests_.end())return;auto callback=std::move(it->second);metaRequests_.erase(it);callback(result);}
void LocalAddon::cancelRequests(){auto requests=std::move(metaRequests_);metaRequests_.clear();for(auto &request:requests)request.second({});}
void LocalAddon::resolveMeta(QString id,qint64 nowMs,std::function<void(QJsonObject)> callback){
    auto cached=meta(id,nowMs);
    if(destroying_||!cached.isEmpty()||!id.startsWith("bt:")||!config_->lookupTorrent||!config_->indexTorrentAsync){callback(cached);return;}
    const auto request=++nextRequest_;metaRequests_[request]=std::move(callback);QPointer<LocalAddon> owner(this);
    auto project=[owner,request,nowMs](QJsonObject entry){
        if(!owner||!owner->metaRequests_.count(request))return;
        if(entry.isEmpty()){owner->completeMeta(request,{});return;}
        auto finish=[owner,request,nowMs,entry](QJsonObject metadata){if(owner&&owner->metaRequests_.count(request))owner->completeMeta(request,owner->projectMeta(entry,nowMs,metadata));};
        auto id=entry.value("itemId").toString();
        try{
            const auto config=owner->config_;
            if(owner->metas_.contains(id))finish(owner->metas_.value(id).toObject());
            else if(config->enrichLookupMetaAsync)config->enrichLookupMetaAsync(entry,finish);
            else if(config->enrichMetaAsync)config->enrichMetaAsync(entry,finish);
            else finish(config->enrichMeta?config->enrichMeta(entry):genericMeta(entry));
        }catch(...){if(owner)owner->completeMeta(request,{});}
    };
    const auto config=config_;
    try{config->lookupTorrent(id.mid(3),[owner,request,project](QJsonObject torrent){
        if(!owner||!owner->metaRequests_.count(request))return;
        if(torrent.isEmpty()){owner->completeMeta(request,{});return;}
        const auto config=owner->config_;
        try{config->indexTorrentAsync(torrent,project);}catch(...){if(owner)owner->completeMeta(request,{});}
    });}catch(...){if(owner)owner->completeMeta(request,{});}
}

bool LocalAddon::load(const QString &database){QFile file(database);if(!file.open(QIODevice::ReadOnly))return false;while(!file.atEnd()){auto r=QJsonDocument::fromJson(file.readLine()).object();auto key=r["id"].toString();if(r["v"]!="1.10.0"||!QFileInfo(key).isReadable()||!r["entry"].isObject())continue;saveEntry(key,r["entry"].toObject());}return true;}
bool LocalAddon::persist(const QString &database) const {QSaveFile file(database);if(!file.open(QIODevice::WriteOnly))return false;for(const auto &r:entries_){auto line=QJsonDocument(QJsonObject{{"id",r.key},{"entry",r.entry},{"v","1.10.0"}}).toJson(QJsonDocument::Compact)+'\n';if(file.write(line)!=line.size())return false;}return file.commit();}
struct LocalAddon::Discovery {
    struct ByName {
        bool operator()(const QString &a,const QString &b) const {return a.compare(b,Qt::CaseInsensitive)<0;}
    };
    struct Directory {
        std::unique_ptr<QDirIterator> iterator;
        std::multimap<QString,QFileInfo,ByName> children;
    };
    QStringList roots;
    qsizetype nextRoot = 0;
    std::vector<Directory> directories;
    std::shared_ptr<QStringList> files = std::make_shared<QStringList>();
};
void LocalAddon::cancelIndexing(){++generation_;}
void LocalAddon::startIndexing(std::function<void()> finished){
    cancelIndexing();
    if(destroying_)return;
    auto discovery=std::make_shared<Discovery>();
    discovery->roots=config_->roots;
    discover(std::move(discovery),generation_,std::move(finished));
}
void LocalAddon::discover(std::shared_ptr<Discovery> discovery,quint64 generation,std::function<void()> finished){
    QTimer::singleShot(0,this,[this,discovery,generation,finished]{
        if(generation!=generation_)return;
        // Keep the existing root order, depth-first traversal, and the
        // discovery-before-indexing boundary, but yield even for ignored files,
        // empty directories, or invalid roots. Filtering inside QDirIterator
        // could otherwise scan a whole directory during a single hasNext().
        static const QStringList excluded{"node_modules","bin","src","build","dist"};
        static const QStringList extensions{"torrent","mp4","mkv","avi"};
        auto open=[&](const QString &path){
            Discovery::Directory directory;
            directory.iterator=std::make_unique<QDirIterator>(path,
                QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot);
            discovery->directories.push_back(std::move(directory));
        };
        QElapsedTimer elapsed;elapsed.start();
        for(int work=0;work<128&&elapsed.elapsed()<4;++work){
            if(discovery->directories.empty()){
                if(discovery->nextRoot==discovery->roots.size()){
                    step(discovery->files,generation,finished);
                    return;
                }
                const QFileInfo root(discovery->roots.at(discovery->nextRoot++));
                // Explicit roots only. Never traverse root or child symlinks.
                if(!root.isSymLink()&&root.isDir())open(root.absoluteFilePath());
                continue;
            }
            auto &directory=discovery->directories.back();
            if(directory.iterator){
                if(!directory.iterator->hasNext()){
                    directory.iterator.reset();
                    continue;
                }
                directory.iterator->next();
                const auto file=directory.iterator->fileInfo();
                const auto name=file.fileName();
                if(file.isSymLink()||file.isHidden()||name.startsWith('.')||excluded.contains(name))continue;
                // QDir::NoSort used the previous QDir's default Name|IgnoreCase
                // sorting, not Unsorted. Incremental insertion preserves that
                // comparator without a bulk sort. Equal-fold ties retain
                // iterator order; QDir's former unstable tie order was unspecified.
                if(file.isDir()||(file.isFile()&&extensions.contains(file.suffix().toLower())))
                    directory.children.emplace(name,file);
                continue;
            }
            if(directory.children.empty()){
                discovery->directories.pop_back();
                continue;
            }
            // Recheck after yielding: a queued directory may have been replaced
            // with a symlink while its siblings were being enumerated.
            const QFileInfo file(directory.children.begin()->second.absoluteFilePath());
            directory.children.erase(directory.children.begin());
            if(file.isSymLink()||file.isHidden())continue;
            if(file.isDir()){
                // Root children have depth one; files at depth seven remain eligible.
                if(discovery->directories.size()<7)open(file.absoluteFilePath());
            }else if(file.isFile()&&extensions.contains(file.suffix().toLower())){
                discovery->files->append(file.absoluteFilePath());
            }
        }
        discover(discovery,generation,finished);
    });
}
void LocalAddon::step(std::shared_ptr<QStringList> files,quint64 generation,std::function<void()> finished){
    QTimer::singleShot(0,this,[this,files,generation,finished]{
        if(generation!=generation_)return;
        if(files->isEmpty()||entries_.size()>=10000){if(finished)finished();return;}
        auto path=files->takeFirst();
        for(const auto&r:entries_)if(r.key==path){step(files,generation,finished);return;}
        QPointer<LocalAddon> owner(this);
        auto called=std::make_shared<bool>(false);
        auto complete=[owner,path,files,generation,finished,called](QJsonObject entry){
            if(*called)return;
            *called=true;
            if(!owner||owner->generation_!=generation)return;
            if(!entry.isEmpty()){
                owner->saveEntry(path,entry);
                if(!entry["itemId"].toString().isEmpty()&&!entry["files"].toArray().isEmpty()){
                    auto save=[owner,generation,id=entry["itemId"].toString()](QJsonObject meta){if(owner&&owner->generation_==generation&&!meta.isEmpty())owner->saveMeta(id,meta);};
                    // Providers may synchronously cancel, restart, or delete the
                    // owner. Retain the original callable (including mutable
                    // state), not a fresh copy of it, until it returns.
                    const auto config=owner->config_;
                    if(config->enrichMetaAsync)config->enrichMetaAsync(entry,save);
                    else save(config->enrichMeta?config->enrichMeta(entry):genericMeta(entry));
                }
            }
            if(owner&&owner->generation_==generation)owner->step(files,generation,finished);
        };
        const auto config=config_;
        if(config->indexFileAsync)config->indexFileAsync(path,complete);
        else complete(config->indexFile?config->indexFile(path):QJsonObject{});
    });
}

namespace {
struct Route{QPointer<LocalAddon> addon;QString resource;};
void jsonResponse(void *response,const QJsonObject &result){server1_http_response_set_status(response,200);server1_http_response_set_header(response,"Content-Type","application/json; charset=utf-8");server1_http_response_set_header(response,"Access-Control-Allow-Origin","*");auto bytes=result.isEmpty()?QByteArray("null"):QJsonDocument(result).toJson(QJsonDocument::Compact);server1_http_response_set_body(response,bytes.data(),bytes.size());}
struct PendingMeta{void *handle=nullptr;~PendingMeta(){server1_http_deferred_destroy(handle);}};
int handle(const void *request,void *response,server1_http_next_fn,void*,void *context){
    auto route=*static_cast<Route*>(context);if(!route.addon){server1_http_response_set_status(response,503);return 1;}
    auto param=[&](const char*k){auto v=server1_http_request_param(request,k);return QString::fromUtf8(v?v:"");};QJsonObject result;
    if(route.resource=="meta"){
        auto pending=std::make_shared<PendingMeta>();pending->handle=server1_http_response_defer(response);
        route.addon->resolveMeta(param("id"),QDateTime::currentMSecsSinceEpoch(),[pending](QJsonObject meta){auto *response=server1_http_deferred_response(pending->handle);if(!response)return;jsonResponse(response,meta);server1_http_deferred_complete(pending->handle);});return 1;
    }
    if(route.resource=="manifest")result=route.addon->manifest();else if(route.resource=="catalog")result=route.addon->catalog();else result=route.addon->streams(param("type"),param("id"));jsonResponse(response,result);return 1;
}
}
bool installLocalAddonRoutes(void *router,LocalAddon &addon){
    for(const auto &resource:{QString("manifest"),QString("catalog"),QString("meta"),QString("stream")}){
        QStringList patterns;
        if(resource=="manifest")patterns<<"/local-addon/manifest.json";
        else patterns<<"/local-addon/"+resource+"/:type/:id.json"<<"/local-addon/"+resource+"/:type/:id/:extra.json";
        for(const auto &pattern:patterns){auto*r=new Route{&addon,resource};if(!server1_http_router_add_handler(router,0,0,"GET",pattern.toUtf8().constData(),handle,r,[](void*p){delete static_cast<Route*>(p);})){delete r;return false;}}
    }return true;
}
}
