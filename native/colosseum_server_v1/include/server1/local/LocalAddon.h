#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <functional>
#include <memory>
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
        std::function<QJsonObject(const QJsonObject &)> enrichMeta;
    };
    explicit LocalAddon(Config config, QObject *parent = nullptr);
    ~LocalAddon() override;
    QJsonObject manifest() const;
    QJsonObject catalog() const;
    QJsonObject streams(const QString &type, const QString &id) const;
    QJsonObject meta(const QString &id, qint64 nowMs) const;
    bool saveEntry(const QString &key, const QJsonObject &entry);
    void saveMeta(const QString &id, const QJsonObject &meta);
    bool load(const QString &database);
    bool persist(const QString &database) const;
    void startIndexing(std::function<void()> finished = {});
    void cancelIndexing();
    qsizetype size() const { return qsizetype(entries_.size()); }
private:
    struct Record { QString key; QJsonObject entry; };
    Config config_;
    std::vector<Record> entries_;
    QJsonObject metas_;
    quint64 generation_ = 0;
    QJsonObject aggregate(const QString &id) const;
    void step(std::shared_ptr<QStringList> files, quint64 generation, std::function<void()> finished);
};
// Routes hold a weak QObject pointer, never ownership. Owner may be destroyed first.
bool installLocalAddonRoutes(void *router, LocalAddon &addon);
}
