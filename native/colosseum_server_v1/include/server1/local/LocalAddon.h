#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <functional>
#include <memory>
#include <map>
#include <vector>
namespace server1::local {
// All access and callbacks belong to the object's Qt thread. No implicit discovery,
// network requests, listeners, or home-directory defaults.
class LocalAddon final : public QObject {
public:
    struct Config {
        QStringList roots;
        bool disableCatalogSupport = true;
        std::function<QJsonObject(const QString &)> indexFile;
        std::function<void(const QString &, std::function<void(QJsonObject)>)> indexFileAsync;
        std::function<void(const QJsonObject &, std::function<void(QJsonObject)>)> enrichMetaAsync;
        std::function<void(const QJsonObject &, std::function<void(QJsonObject)>)> enrichLookupMetaAsync;
        // Optional engine-registry adapter. Empty result denotes lookup failure.
        std::function<void(const QString &, std::function<void(QJsonObject)>)> lookupTorrent;
        std::function<void(const QJsonObject &, std::function<void(QJsonObject)>)> indexTorrentAsync;
        std::function<QJsonObject(const QJsonObject &)> enrichMeta;
    };
    explicit LocalAddon(Config config, QObject *parent = nullptr);
    ~LocalAddon() override;
    QJsonObject manifest() const;
    QJsonObject catalog() const;
    QJsonObject streams(const QString &type, const QString &id) const;
    QJsonObject meta(const QString &id, qint64 nowMs) const;
    void resolveMeta(QString id, qint64 nowMs, std::function<void(QJsonObject)> callback);
    void cancelRequests();
    bool saveEntry(const QString &key, const QJsonObject &entry);
    void saveMeta(const QString &id, const QJsonObject &meta);
    bool load(const QString &database);
    bool persist(const QString &database) const;
    void startIndexing(std::function<void()> finished = {});
    void cancelIndexing();
    qsizetype size() const { return qsizetype(entries_.size()); }
private:
    struct Discovery;
    struct Record { QString key; QJsonObject entry; };
    std::shared_ptr<Config> config_;
    std::vector<Record> entries_;
    QJsonObject metas_;
    quint64 generation_ = 0;
    QJsonObject aggregate(const QString &id) const;
    QJsonObject projectMeta(QJsonObject entry, qint64 nowMs, QJsonObject suppliedMeta = {}) const;
    void completeMeta(quint64 request, QJsonObject result);
    std::map<quint64,std::function<void(QJsonObject)>> metaRequests_;
    quint64 nextRequest_ = 0;
    bool destroying_ = false;
    void discover(std::shared_ptr<Discovery> discovery, quint64 generation, std::function<void()> finished);
    void step(std::shared_ptr<QStringList> files, quint64 generation, std::function<void()> finished);
};
// Routes hold a weak QObject pointer, never ownership. Owner may be destroyed first.
bool installLocalAddonRoutes(void *router, LocalAddon &addon);
}
