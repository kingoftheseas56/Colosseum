#pragma once

#include "TorrentHttpRouteAdapter.h"
#include "TorrentPieceSource.h"
#include "core/HttpRouter.h"
#include "enginefs/EngineFsControlPlane.h"
#include "settings/ServerSettings.h"
#include "torrent_http/TorrentHttpSurface.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

class TorrentEngine;

namespace colosseum::server::integration {

namespace EngineFs = ::Colosseum::Server::EngineFs;

// Production adapter for the already-owned libtorrent engine. W07 sees only
// the narrow HTTP backend; W02/W03 and W06 share the same engine records and
// handles, so creation, stats, eviction, and byte streaming cannot drift into
// separate torrent implementations.
class ProductionTorrentBackend final : public QObject,
                                       public torrent_http::TorrentHttpBackend,
                                       public TorrentStreamFactory,
                                       public EngineFs::IEngineFsBackendFactory
{
public:
    ProductionTorrentBackend(QString cacheDirectory,
                             server::ServerSettings &settings,
                             QObject *parent = nullptr);
    ProductionTorrentBackend(QString cacheDirectory,
                             server::ServerSettings &settings,
                             TorrentEngine *externalEngine,
                             QObject *parent = nullptr);
    ~ProductionTorrentBackend() override;

    ProductionTorrentBackend(const ProductionTorrentBackend &) = delete;
    ProductionTorrentBackend &operator=(const ProductionTorrentBackend &) = delete;

    void start();
    void stop();
    void setControlPlane(EngineFs::EngineFsControlPlane *controlPlane) noexcept
    {
        controlPlane_ = controlPlane;
    }

    TorrentEngine &engine() noexcept { return *engine_; }
    const TorrentEngine &engine() const noexcept { return *engine_; }

    // TorrentHttpBackend
    void ensureEngine(const QString &lowerInfoHash,
                      const QJsonObject &options,
                      ReadyCallback ready) override;
    void createFromTorrent(const QByteArray &torrentBytes,
                           TorrentReadyCallback ready) override;
    QJsonObject defaultEngineOptions(const QString &lowerInfoHash) const override;
    QJsonValue globalStats() const override;
    QJsonObject systemStats() const override;
    QJsonValue stats(const QString &lowerInfoHash,
                     std::optional<int> fileIndex) const override;
    QVector<torrent_http::TorrentFileView> files(const QString &lowerInfoHash) const override;
    std::optional<int> guessFileIndex(const QString &lowerInfoHash,
                                      const QJsonObject &seriesHint) const override;
    void remove(const QString &lowerInfoHash, std::function<void()> complete) override;
    void removeAll() override;
    void prewarm(const QString &lowerInfoHash, int fileIndex) override;
    void streamOpened(const QString &lowerInfoHash, int fileIndex) override;
    void streamClosed(const QString &lowerInfoHash, int fileIndex) override;

    // TorrentStreamFactory
    std::shared_ptr<TorrentStreamSession> open(
        const torrent_http::TorrentReadPlan &plan,
        const std::shared_ptr<server::CancellationToken> &cancellation,
        TorrentStreamCallbacks callbacks) override;

    // IEngineFsBackendFactory
    std::shared_ptr<EngineFs::IEngineFsBackend> create(
        const QString &canonicalHash,
        const QJsonObject &effectiveOptions) override;

private:
    struct EngineBackend;

    QString savePathFor(const QString &lowerInfoHash) const;
    bool ensureRecord(const QString &lowerInfoHash, QString *error);
    QVector<torrent_http::TorrentFileView> fileViews(const QString &lowerInfoHash) const;
    EngineFs::EngineFsBackendSnapshot snapshot(const QString &lowerInfoHash) const;
    void notifyReady(const QString &lowerInfoHash);
    void notifyError(const QString &lowerInfoHash, const QString &error);
    std::shared_ptr<EngineBackend> backendFor(const QString &lowerInfoHash) const;
    void removeBackend(const QString &lowerInfoHash, const EngineBackend *backend);

    QString cacheDirectory_;
    server::ServerSettings &settings_;
    std::unique_ptr<TorrentEngine> ownedEngine_;
    TorrentEngine *engine_ = nullptr;
    EngineFs::EngineFsControlPlane *controlPlane_ = nullptr;
    mutable std::mutex mutex_;
    QHash<QString, std::shared_ptr<EngineBackend>> backends_;
    QHash<QString, QVector<ReadyCallback>> readyCallbacks_;
    bool started_ = false;
};

// /create accepts either a local file or an HTTP(S) URL. It is deliberately a
// server-worker operation, matching the synchronous source-loader boundary in
// W07 while keeping the QNetworkAccessManager on the calling event-loop thread.
class ProductionTorrentCreateSource final : public torrent_http::TorrentCreateSource
{
public:
    void load(const QString &source,
              std::function<void(QByteArray bytes, QString error)> complete) override;
};

} // namespace colosseum::server::integration
