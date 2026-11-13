#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>
#include <QUrl>
#include <functional>
namespace server1::local {
// Opt-in native M1069 suggestion fallback. Disabled by default. Only parsed
// names/year are sent, never file paths. Endpoints are replaceable for fixtures.
class LocalImdb final : public QObject {
public:
    struct Config { bool enabled=false; QUrl base=QUrl("https://sg.media-imdb.com/suggests/"); int timeoutMs=15000; qsizetype maxBytes=2*1024*1024; };
    using Callback=std::function<void(QJsonObject)>;
    explicit LocalImdb(Config config,QObject *parent=nullptr);
    ~LocalImdb() override;
    void recognize(QJsonObject query,Callback callback);
    void cancel();
    static QJsonObject match(QJsonObject query,QJsonArray suggestions);
    static QString parseSearchTerm(QString term);
private:
    void attempt(QJsonObject query,Callback callback,bool loose,quint64 generation);
    Config config_;QNetworkAccessManager network_;QHash<QNetworkReply*,bool> pending_;quint64 generation_=0;
};
}
