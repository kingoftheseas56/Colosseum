#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <functional>
namespace server1::local {
// Explicit opt-in adapter for source M1068 metadata recognition and M502 enrichment.
// Does not parse filenames or invoke the IMDb suggestion fallback. Query is an
// already parsed {name, type, year}; enrichment sends only type + IMDb ID.
class LocalMetadata final : public QObject {
public:
    struct Config {
        bool enabled = false;
        QUrl movieNames;
        QUrl seriesNames;
        QUrl metaBase;
        int timeoutMs = 15000;
        qsizetype maxResponseBytes = 16 * 1024 * 1024;
        static Config sourceEndpoints(); // remains disabled until explicitly enabled
    };
    using Callback = std::function<void(QJsonObject)>;
    explicit LocalMetadata(Config config, QObject *parent = nullptr);
    ~LocalMetadata() override;
    void recognize(QJsonObject query, Callback callback);
    void enrich(QJsonObject entry, Callback callback);
    void indexParsedFile(QString path, QJsonObject parsed, Callback callback);
    void cancel();
    static QString simplifyName(QJsonObject query);
    static QJsonObject matchMetadata(QJsonObject query, QJsonArray records);
    static QJsonObject genericMeta(QJsonObject entry);
private:
    void deliver(Callback callback, QJsonObject result);
    quint64 generation_ = 0;
    void get(QUrl url, std::function<void(QJsonObject)> callback);
    Config config_;
    QNetworkAccessManager network_;
    QHash<QString,QJsonArray> names_;
    QHash<QNetworkReply *,std::function<void(QJsonObject)>> pending_;
};
}
