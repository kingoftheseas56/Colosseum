#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace colosseum::server::torrent_http {

struct TorrentFileView {
    int index = -1;
    QString name;
    QString path;
    qint64 length = 0;
    qint64 offset = 0;
};

struct TorrentHttpRequest {
    QByteArray method;
    QString path;
    QHash<QString, QStringList> query;
    QHash<QByteArray, QByteArray> headers;
    QJsonObject body;

    QByteArray header(const QByteArray &name) const;
    QStringList queryValues(const QString &name) const;
    bool queryTruthy(const QString &name) const;
};

struct TorrentReadPlan {
    QString infoHash;
    int fileIndex = -1;
    qint64 start = 0;
    qint64 end = -1;
    std::optional<int> priority;
};

class StreamLease final {
public:
    explicit StreamLease(std::function<void()> close);
    ~StreamLease();
    void close();

private:
    std::function<void()> close_;
    std::atomic_bool closed_{false};
};

struct TorrentHttpReply {
    int status = 200;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
    std::optional<TorrentReadPlan> readPlan;
    std::shared_ptr<StreamLease> streamLease;
};

class TorrentHttpBackend {
public:
    using ReadyCallback = std::function<void(const QString &error)>;
    using TorrentReadyCallback = std::function<void(const QString &lowerInfoHash,
                                                     const QString &error)>;

    virtual ~TorrentHttpBackend() = default;

    virtual void ensureEngine(const QString &lowerInfoHash,
                              const QJsonObject &options,
                              ReadyCallback ready) = 0;
    virtual void createFromTorrent(const QByteArray &torrentBytes,
                                   TorrentReadyCallback ready) = 0;

    // The W02/W03 adapter supplies effective defaults and stats. W07 does not own them.
    virtual QJsonObject defaultEngineOptions(const QString &lowerInfoHash) const = 0;
    virtual QJsonValue globalStats() const = 0;
    virtual QJsonObject systemStats() const = 0;
    virtual QJsonValue stats(const QString &lowerInfoHash,
                             std::optional<int> fileIndex) const = 0;
    virtual QVector<TorrentFileView> files(const QString &lowerInfoHash) const = 0;

    // W04 owns GuessFileIdx semantics. W07 only decides when the route invokes it.
    virtual std::optional<int> guessFileIndex(const QString &lowerInfoHash,
                                              const QJsonObject &seriesHint) const = 0;

    virtual void remove(const QString &lowerInfoHash, std::function<void()> complete) = 0;
    virtual void removeAll() = 0;

    // W06 owns FileStream/selection behavior. W07 emits only the observable route intents.
    virtual void prewarm(const QString &lowerInfoHash, int fileIndex) = 0;
    virtual void streamOpened(const QString &lowerInfoHash, int fileIndex) = 0;
    virtual void streamClosed(const QString &lowerInfoHash, int fileIndex) = 0;
};

class TorrentCreateSource {
public:
    virtual ~TorrentCreateSource() = default;
    virtual void load(const QString &source,
                      std::function<void(QByteArray bytes, QString error)> complete) = 0;
};

class TorrentHttpSurface final {
public:
    using ReplyCallback = std::function<void(TorrentHttpReply)>;

    TorrentHttpSurface(TorrentHttpBackend &backend, TorrentCreateSource &source);

    // Returns false only when the path/method belongs to another mounted feature family.
    // A true return may complete asynchronously after metadata readiness/source loading.
    bool dispatch(const TorrentHttpRequest &request, ReplyCallback reply);

private:
    void createByHash(const QString &rawHash,
                      const TorrentHttpRequest &request,
                      ReplyCallback reply);
    void createFromBody(const TorrentHttpRequest &request, ReplyCallback reply);
    void serveMedia(const TorrentHttpRequest &request,
                    const QStringList &parts,
                    ReplyCallback reply);

    TorrentHttpReply jsonReply(const QJsonValue &value, int status = 200) const;
    TorrentHttpReply emptyReply(int status) const;

    TorrentHttpBackend &backend_;
    TorrentCreateSource &source_;
};

} // namespace colosseum::server::torrent_http
