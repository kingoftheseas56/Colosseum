#pragma once
#include "server1/local/LocalAddon.h"
#include "server1/local/LocalAddonLog.h"
#include "server1/local/LocalIndexer.h"
namespace server1::local {
// Optional native composition. Constructor neither scans nor connects. Call start
// with an explicit database path only after roots/provider configuration is approved.
class LocalAddonRuntime final : public QObject {
public:
    struct Config {
        QStringList roots;
        bool disableCatalogSupport=true;
        LocalIndexer::Config indexing;
        LocalMetadata::Config enrichment;
        // Optional direct engine-registry create/statistics adapter; no self-HTTP.
        std::function<void(const QString &,std::function<void(QJsonObject)>)> lookupTorrent;
    };
    explicit LocalAddonRuntime(Config config,QObject *parent=nullptr);
    ~LocalAddonRuntime() override;
    bool start(QString database,std::function<void()> finished={});
    void cancel();
    LocalAddon &addon(){return addon_;}
    QString lastError() const{return lastError_;}
private:
    LocalAddon::Config addonConfig();
    Config config_;LocalIndexer indexer_;LocalMetadata metadata_;
    LocalAddonLog entries_{true},metas_{false};LocalAddon addon_;QString lastError_;quint64 generation_=0;
};
}
