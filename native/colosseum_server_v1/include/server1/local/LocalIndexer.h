#pragma once
#include "server1/local/LocalMetadata.h"
#include "server1/local/LocalImdb.h"
#include <QQueue>
#include <QDateTime>
#include <vector>
namespace server1::local {
// Native M501 processing with explicit opt-in providers. Never discovers files.
class LocalIndexer final : public QObject {
public:
    struct Config { LocalMetadata::Config metadata; LocalImdb::Config imdb; qsizetype maxTorrentBytes=16*1024*1024; };
    using Callback=std::function<void(QJsonObject)>;
    explicit LocalIndexer(Config config,QObject *parent=nullptr);
    ~LocalIndexer() override;
    void indexFile(QString path,Callback callback);
    void indexParsedTorrent(QJsonObject torrent,Callback callback);
    void cancel();
    static QJsonObject decodeTorrent(QByteArray bytes,QString *error=nullptr);
private:
    void processFile(QJsonObject file,Callback callback);
    void recognize(QJsonObject query,Callback callback);
    void pump();
    struct Task {QJsonObject query;Callback callback;QString key;};
    struct Cached {QJsonObject result;qint64 when;};
    Config config_;LocalMetadata metadata_;LocalImdb imdb_;QQueue<Task> queue_;QHash<QString,std::vector<Callback>> waiting_;QHash<QString,Cached> cache_;int active_=0;quint64 generation_=0;
};
}
