#include "server1/local/LocalAddonRuntime.h"
#include <QPointer>
namespace server1::local {
LocalAddonRuntime::LocalAddonRuntime(Config config,QObject *parent):QObject(parent),config_(std::move(config)),indexer_(config_.indexing,this),metadata_(config_.enrichment,this),addon_(addonConfig(),this){}
LocalAddonRuntime::~LocalAddonRuntime(){cancel();}
LocalAddon::Config LocalAddonRuntime::addonConfig(){LocalAddon::Config c;c.roots=config_.roots;c.disableCatalogSupport=config_.disableCatalogSupport;
    c.lookupTorrent=config_.lookupTorrent;
    c.enrichLookupMetaAsync=[this](const QJsonObject &entry,std::function<void(QJsonObject)> callback){metadata_.enrich(entry,std::move(callback));};
    c.indexTorrentAsync=[this](const QJsonObject &torrent,std::function<void(QJsonObject)> callback){indexer_.indexParsedTorrent(torrent,std::move(callback));};
    c.indexFileAsync=[this](const QString &path,std::function<void(QJsonObject)> callback){QPointer<LocalAddonRuntime> owner(this);auto generation=generation_;indexer_.indexFile(path,[owner,generation,path,callback](QJsonObject entry){if(!owner||generation!=owner->generation_)return;if(!entry.isEmpty())owner->entries_.append(path,entry,&owner->lastError_);callback(entry);});};
    c.enrichMetaAsync=[this](const QJsonObject &entry,std::function<void(QJsonObject)> callback){QPointer<LocalAddonRuntime> owner(this);auto generation=generation_;metadata_.enrich(entry,[owner,generation,entry,callback](QJsonObject meta){if(!owner||generation!=owner->generation_)return;owner->metas_.append(entry["itemId"].toString(),meta,&owner->lastError_);callback(meta);});};return c;
}
bool LocalAddonRuntime::start(QString database,std::function<void()> finished){cancel();lastError_.clear();if(database.isEmpty()){lastError_="explicit local-addon database path required";return false;}if(!metas_.open(database+"Meta",&lastError_)||!entries_.open(database,&lastError_)){entries_.close();metas_.close();return false;}for(auto key:entries_.keys())addon_.saveEntry(key,entries_.entry(key));for(auto key:metas_.keys())addon_.saveMeta(key,metas_.entry(key));addon_.startIndexing(std::move(finished));return true;}
void LocalAddonRuntime::cancel(){++generation_;addon_.cancelIndexing();addon_.cancelRequests();indexer_.cancel();metadata_.cancel();entries_.close();metas_.close();}
}
