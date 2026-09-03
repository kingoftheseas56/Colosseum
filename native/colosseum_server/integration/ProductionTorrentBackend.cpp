#include "ProductionTorrentBackend.h"

#include "scheduler/FileStream.h"
#include "scheduler/SchedulerSpine.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <utility>
#include <vector>

#include "../../torrent/engine/TorrentEngine.h"

#ifdef HAS_LIBTORRENT
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#endif

namespace colosseum::server::integration {
namespace {

QString canonicalHash(QString value)
{
    return value.trimmed().toLower();
}

QString mediaName(const QString &name)
{
    return name.toLower();
}

bool isMediaFile(const QString &name)
{
    static const QRegularExpression media(
        QStringLiteral(R"(\.(mkv|mp4|m4v|avi|mov|webm|wmv|mpg|mpeg|ts|m3u8|flac|mp3|wav|wma|aac|ogg)$)"),
        QRegularExpression::CaseInsensitiveOption);
    return media.match(name).hasMatch();
}

class ProductionTorrentStreamSession final : public QObject, public TorrentStreamSession
{
public:
    ProductionTorrentStreamSession(lt::torrent_handle torrent,
                                    const torrent_http::TorrentReadPlan &plan,
                                    const std::shared_ptr<server::CancellationToken> &cancellation,
                                    TorrentStreamCallbacks callbacks,
                                    QObject *parent = nullptr)
        : QObject(parent)
        , torrent_(std::move(torrent))
        , plan_(plan)
        , cancellation_(cancellation)
        , callbacks_(std::move(callbacks))
    {
        const auto info = torrent_.torrent_file();
        if (!info || plan_.fileIndex < 0 || plan_.fileIndex >= info->files().num_files())
            throw std::invalid_argument("torrent stream file is unavailable");

        const auto &storage = info->files();
        std::vector<std::size_t> lengths;
        lengths.reserve(static_cast<std::size_t>(info->num_pieces()));
        for (int piece = 0; piece < info->num_pieces(); ++piece) {
            const auto offset = static_cast<qint64>(piece) * info->piece_length();
            lengths.push_back(static_cast<std::size_t>(
                std::min<qint64>(info->piece_length(), info->total_size() - offset)));
        }
        scheduler_ = std::make_unique<scheduler::SchedulerSpine>(std::move(lengths));
        source_ = std::make_shared<TorrentPieceSource>(torrent_);

        for (int piece = 0; piece < info->num_pieces(); ++piece) {
            if (torrent_.have_piece(lt::piece_index_t{piece}))
                scheduler_->markPieceAvailable(static_cast<std::size_t>(piece));
        }

        const auto fileLength = storage.file_size(lt::file_index_t{plan_.fileIndex});
        if (fileLength <= 0 || plan_.start < 0 || plan_.end < plan_.start
            || plan_.end >= fileLength)
            throw std::out_of_range("torrent stream range is unavailable");

        scheduler::FileStreamOptions options;
        options.start = static_cast<std::size_t>(plan_.start);
        options.end = static_cast<std::size_t>(plan_.end);
        options.priority = plan_.priority ? std::optional<bool>(*plan_.priority > 0) : std::nullopt;
        options.bufferBytes = 4 * 1024 * 1024;
        stream_ = std::make_unique<scheduler::FileStream>(
            *scheduler_, *source_,
            scheduler::FileSpan{static_cast<std::size_t>(storage.file_offset(
                                         lt::file_index_t{plan_.fileIndex})),
                                 static_cast<std::size_t>(fileLength)},
            static_cast<std::size_t>(info->piece_length()), options);

        timer_.setInterval(25);
        timer_.setSingleShot(false);
        connect(&timer_, &QTimer::timeout, this, [this] { refreshAvailability(); });
        stream_->setChunkObserver([this](const std::vector<std::byte> &chunk) {
            if (cancelled() || !callbacks_.onChunk)
                return;
            QByteArray bytes(static_cast<qsizetype>(chunk.size()), Qt::Uninitialized);
            std::memcpy(bytes.data(), chunk.data(), chunk.size());
            callbacks_.onChunk(std::move(bytes));
        });
        stream_->setErrorObserver([this](const std::error_code &error) {
            if (callbacks_.onError)
                callbacks_.onError(error);
            destroy();
        });
        stream_->setEndObserver([this] {
            timer_.stop();
            if (callbacks_.onEnd)
                callbacks_.onEnd();
        });
    }

    void start() override
    {
        if (destroyed_ || started_)
            return;
        started_ = true;
        refreshAvailability();
        if (!destroyed_ && !stream_->ended()) {
            timer_.start();
            stream_->start();
        }
    }

    void destroy() override
    {
        if (destroyed_)
            return;
        destroyed_ = true;
        timer_.stop();
        if (stream_)
            stream_->destroy();
        callbacks_ = {};
    }

private:
    bool cancelled() const
    {
        return destroyed_ || (cancellation_ && cancellation_->isCancelled());
    }

    void refreshAvailability()
    {
        if (cancelled()) {
            destroy();
            return;
        }
        const auto info = torrent_.torrent_file();
        if (!info)
            return;
        bool changed = false;
        for (int piece = 0; piece < info->num_pieces(); ++piece) {
            if (torrent_.have_piece(lt::piece_index_t{piece})) {
                source_->notifyPieceFinished(static_cast<std::size_t>(piece));
                if (!scheduler_->isPieceAvailable(static_cast<std::size_t>(piece))) {
                    scheduler_->markPieceAvailable(static_cast<std::size_t>(piece));
                    changed = true;
                }
            }
        }
        if (changed)
            scheduler_->refresh();
    }

    lt::torrent_handle torrent_;
    torrent_http::TorrentReadPlan plan_;
    std::shared_ptr<server::CancellationToken> cancellation_;
    TorrentStreamCallbacks callbacks_;
    std::unique_ptr<scheduler::SchedulerSpine> scheduler_;
    std::shared_ptr<TorrentPieceSource> source_;
    std::unique_ptr<scheduler::FileStream> stream_;
    QTimer timer_;
    bool started_ = false;
    bool destroyed_ = false;
};

} // namespace

struct ProductionTorrentBackend::EngineBackend final : public EngineFs::IEngineFsBackend
{
    EngineBackend(ProductionTorrentBackend &owner, QString hash, QJsonObject options)
        : owner(owner), hash(std::move(hash)), options(std::move(options))
    {
    }

    void resumeSwarm() override { owner.engine().resumeTorrent(hash); }
    void pauseSwarm() override { owner.engine().pauseTorrent(hash); }
    void destroy(std::function<void()> done) override
    {
        owner.engine().removeTorrent(hash);
        owner.removeBackend(hash, this);
        if (done)
            done();
    }

    void whenReady(std::function<void(const QVariant &)> callback) override
    {
        if (!callback)
            return;
        if (owner.engine().hasMetadata(hash)) {
            callback(QVariant::fromValue(hash));
            return;
        }
        std::lock_guard lock(mutex);
        readyCallbacks.push_back(std::move(callback));
    }

    void setCallbacks(EngineFs::EngineFsBackendCallbacks value) override
    {
        std::lock_guard lock(mutex);
        callbacks = std::move(value);
    }

    EngineFs::EngineFsBackendSnapshot statisticsSnapshot() const override
    {
        return owner.snapshot(hash);
    }

    void notifyReady()
    {
        std::vector<std::function<void(const QVariant &)>> callbacksToRun;
        {
            std::lock_guard lock(mutex);
            callbacksToRun.swap(readyCallbacks);
        }
        for (auto &callback : callbacksToRun)
            callback(QVariant::fromValue(hash));
    }

    void notifyError(const QString &message)
    {
        EngineFs::EngineFsBackendCallbacks current;
        {
            std::lock_guard lock(mutex);
            current = callbacks;
        }
        if (current.onError)
            current.onError(message);
    }

    ProductionTorrentBackend &owner;
    QString hash;
    QJsonObject options;
    mutable std::mutex mutex;
    EngineFs::EngineFsBackendCallbacks callbacks;
    std::vector<std::function<void(const QVariant &)>> readyCallbacks;
};

ProductionTorrentBackend::ProductionTorrentBackend(QString cacheDirectory,
                                                   server::ServerSettings &settings,
                                                   QObject *parent)
    : ProductionTorrentBackend(std::move(cacheDirectory), settings, nullptr, parent)
{
}

ProductionTorrentBackend::ProductionTorrentBackend(QString cacheDirectory,
                                                   server::ServerSettings &settings,
                                                   TorrentEngine *externalEngine,
                                                   QObject *parent)
    : QObject(parent)
    , cacheDirectory_(QDir::cleanPath(std::move(cacheDirectory)))
    , settings_(settings)
    , ownedEngine_(externalEngine ? nullptr : std::make_unique<TorrentEngine>(cacheDirectory_, this))
    , engine_(externalEngine ? externalEngine : ownedEngine_.get())
{
    connect(engine_, &TorrentEngine::metadataReady, this,
            [this](const QString &hash, const QString &, qint64, const QJsonArray &) {
                notifyReady(canonicalHash(hash));
            });
    connect(engine_, &TorrentEngine::torrentError, this,
            [this](const QString &hash, const QString &message) {
                notifyError(canonicalHash(hash), message);
            });
    connect(engine_, &TorrentEngine::torrentAddFailed, this,
            [this](const QString &hash, const QString &message) {
                notifyError(canonicalHash(hash), message);
            });
}

ProductionTorrentBackend::~ProductionTorrentBackend()
{
    stop();
}

void ProductionTorrentBackend::start()
{
    if (started_)
        return;
    QDir().mkpath(cacheDirectory_);
    engine_->start();
    started_ = true;
}

void ProductionTorrentBackend::stop()
{
    if (!started_)
        return;
    if (ownedEngine_)
        engine_->stop();
    started_ = false;
}

QString ProductionTorrentBackend::savePathFor(const QString &lowerInfoHash) const
{
    const QString configured = settings_.values().value(QStringLiteral("cacheRoot")).toString();
    const QString root = configured.isEmpty() ? cacheDirectory_ : configured;
    return QDir(root).filePath(QStringLiteral("torrents/%1").arg(lowerInfoHash));
}

bool ProductionTorrentBackend::ensureRecord(const QString &lowerInfoHash, QString *error)
{
    const QString hash = canonicalHash(lowerInfoHash);
    if (engine_->hasTorrent(hash)) {
        if (error)
            error->clear();
        return true;
    }
    const QString savePath = savePathFor(hash);
    QDir().mkpath(savePath);
    const QString added = engine_->addMagnet(
        QStringLiteral("magnet:?xt=urn:btih:%1").arg(hash), savePath, true);
    if (added.isEmpty()) {
        if (error)
            *error = QStringLiteral("torrent could not be added");
        return false;
    }
    if (error)
        error->clear();
    return true;
}

void ProductionTorrentBackend::ensureEngine(const QString &lowerInfoHash,
                                             const QJsonObject &options,
                                             ReadyCallback ready)
{
    const QString hash = canonicalHash(lowerInfoHash);
    QString error;
    if (!ensureRecord(hash, &error)) {
        if (ready)
            ready(error);
        return;
    }

    if (controlPlane_ && !controlPlane_->exists(hash))
        controlPlane_->createEngine(hash, options);
    else if (controlPlane_)
        controlPlane_->resumeSwarm(hash);

    if (engine_->hasMetadata(hash)) {
        if (ready)
            ready({});
        return;
    }
    if (ready) {
        std::lock_guard lock(mutex_);
        readyCallbacks_[hash].push_back(std::move(ready));
    }
}

void ProductionTorrentBackend::createFromTorrent(const QByteArray &torrentBytes,
                                                  TorrentReadyCallback ready)
{
    const QString defaultRoot = settings_.values().value(QStringLiteral("cacheRoot"))
                                    .toString();
    const QString saveRoot = defaultRoot.isEmpty() ? cacheDirectory_ : defaultRoot;
    const QString hash = engine_->addTorrentBytes(torrentBytes,
                                                  QDir(saveRoot).filePath(QStringLiteral("torrents")),
                                                  true);
    if (hash.isEmpty()) {
        if (ready)
            ready({}, QStringLiteral("torrent could not be parsed or added"));
        return;
    }
    if (controlPlane_)
        controlPlane_->createEngine(hash, {});
    if (ready)
        ready(hash, {});
}

QJsonObject ProductionTorrentBackend::defaultEngineOptions(const QString &) const
{
    const QJsonObject values = settings_.values();
    const int minPeers = values.value(QStringLiteral("btMinPeersForStable")).toInt(5);
    const int maxPeers = values.value(QStringLiteral("btMaxConnections")).toInt(55);
    return QJsonObject{{QStringLiteral("peerSearch"),
                       QJsonObject{{QStringLiteral("min"), minPeers},
                                   {QStringLiteral("max"), maxPeers}}}};
}

QJsonValue ProductionTorrentBackend::globalStats() const
{
    return controlPlane_ ? QJsonValue(controlPlane_->statisticsAll()) : QJsonValue(QJsonObject{});
}

QJsonObject ProductionTorrentBackend::systemStats() const
{
    return QJsonObject{{QStringLiteral("os"), QSysInfo::prettyProductName()},
                       {QStringLiteral("kernel"), QSysInfo::kernelType()},
                       {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()}};
}

QJsonValue ProductionTorrentBackend::stats(const QString &lowerInfoHash,
                                           std::optional<int> fileIndex) const
{
    const QString hash = canonicalHash(lowerInfoHash);
    if (controlPlane_)
        return controlPlane_->statistics(hash, fileIndex);
    return QJsonValue(QJsonObject{{QStringLiteral("infoHash"), hash}});
}

QVector<torrent_http::TorrentFileView> ProductionTorrentBackend::fileViews(
    const QString &lowerInfoHash) const
{
    QVector<torrent_http::TorrentFileView> result;
#ifdef HAS_LIBTORRENT
    const lt::torrent_handle handle = engine_->torrentHandle(canonicalHash(lowerInfoHash));
    const auto info = handle.torrent_file();
    if (!info)
        return result;
    const auto &storage = info->files();
    result.reserve(storage.num_files());
    for (int index = 0; index < storage.num_files(); ++index) {
        const lt::file_index_t fileIndex{index};
        const QString path = QString::fromStdString(storage.file_path(fileIndex));
        result.push_back({index, path, path,
                          static_cast<qint64>(storage.file_size(fileIndex)),
                          static_cast<qint64>(storage.file_offset(fileIndex))});
    }
#else
    Q_UNUSED(lowerInfoHash);
#endif
    return result;
}

QVector<torrent_http::TorrentFileView> ProductionTorrentBackend::files(
    const QString &lowerInfoHash) const
{
    return fileViews(lowerInfoHash);
}

std::optional<int> ProductionTorrentBackend::guessFileIndex(const QString &lowerInfoHash,
                                                            const QJsonObject &) const
{
    const auto values = fileViews(lowerInfoHash);
    std::optional<int> selected;
    for (const auto &file : values) {
        if (!isMediaFile(mediaName(file.name)))
            continue;
        if (!selected || file.length > values.at(*selected).length)
            selected = file.index;
    }
    return selected;
}

void ProductionTorrentBackend::remove(const QString &lowerInfoHash,
                                       std::function<void()> complete)
{
    const QString hash = canonicalHash(lowerInfoHash);
    if (controlPlane_)
        controlPlane_->removeEngine(hash, std::move(complete));
    else {
        engine_->removeTorrent(hash);
        if (complete)
            complete();
    }
}

void ProductionTorrentBackend::removeAll()
{
    const auto statuses = engine_->allStatuses();
    for (const auto &status : statuses)
        remove(status.infoHash, {});
}

void ProductionTorrentBackend::prewarm(const QString &lowerInfoHash, int)
{
    engine_->setSequentialDownload(canonicalHash(lowerInfoHash), true);
    engine_->resumeTorrent(canonicalHash(lowerInfoHash));
}

void ProductionTorrentBackend::streamOpened(const QString &lowerInfoHash, int fileIndex)
{
    if (controlPlane_)
        controlPlane_->noteStreamOpen(canonicalHash(lowerInfoHash), fileIndex);
}

void ProductionTorrentBackend::streamClosed(const QString &lowerInfoHash, int fileIndex)
{
    if (controlPlane_)
        controlPlane_->noteStreamClose(canonicalHash(lowerInfoHash), fileIndex);
}

std::shared_ptr<TorrentStreamSession> ProductionTorrentBackend::open(
    const torrent_http::TorrentReadPlan &plan,
    const std::shared_ptr<server::CancellationToken> &cancellation,
    TorrentStreamCallbacks callbacks)
{
#ifdef HAS_LIBTORRENT
    try {
        const lt::torrent_handle handle = engine_->torrentHandle(canonicalHash(plan.infoHash));
        if (!handle.is_valid())
            return {};
        return std::make_shared<ProductionTorrentStreamSession>(
            handle, plan, cancellation, std::move(callbacks));
    } catch (const std::exception &) {
        return {};
    }
#else
    Q_UNUSED(plan);
    Q_UNUSED(cancellation);
    Q_UNUSED(callbacks);
    return {};
#endif
}

std::shared_ptr<EngineFs::IEngineFsBackend> ProductionTorrentBackend::create(
    const QString &canonicalHashValue, const QJsonObject &effectiveOptions)
{
    const QString hash = canonicalHash(canonicalHashValue);
    auto backend = std::make_shared<EngineBackend>(*this, hash, effectiveOptions);
    std::lock_guard lock(mutex_);
    backends_.insert(hash, backend);
    return backend;
}

std::shared_ptr<ProductionTorrentBackend::EngineBackend>
ProductionTorrentBackend::backendFor(const QString &lowerInfoHash) const
{
    std::lock_guard lock(mutex_);
    return backends_.value(canonicalHash(lowerInfoHash));
}

void ProductionTorrentBackend::notifyReady(const QString &lowerInfoHash)
{
    const QString hash = canonicalHash(lowerInfoHash);
    QVector<ReadyCallback> callbacks;
    {
        std::lock_guard lock(mutex_);
        callbacks = std::move(readyCallbacks_[hash]);
        readyCallbacks_.remove(hash);
    }
    for (auto &callback : callbacks)
        if (callback)
            callback({});
    if (const auto backend = backendFor(hash))
        backend->notifyReady();
}

void ProductionTorrentBackend::notifyError(const QString &lowerInfoHash,
                                            const QString &error)
{
    const QString hash = canonicalHash(lowerInfoHash);
    QVector<ReadyCallback> callbacks;
    {
        std::lock_guard lock(mutex_);
        callbacks = std::move(readyCallbacks_[hash]);
        readyCallbacks_.remove(hash);
    }
    for (auto &callback : callbacks)
        if (callback)
            callback(error);
    if (const auto backend = backendFor(hash))
        backend->notifyError(error);
}

void ProductionTorrentBackend::removeBackend(const QString &lowerInfoHash,
                                             const EngineBackend *backend)
{
    std::lock_guard lock(mutex_);
    const QString hash = canonicalHash(lowerInfoHash);
    const auto it = backends_.find(hash);
    if (it != backends_.end() && it.value().get() == backend)
        backends_.erase(it);
}

EngineFs::EngineFsBackendSnapshot ProductionTorrentBackend::snapshot(
    const QString &lowerInfoHash) const
{
    EngineFs::EngineFsBackendSnapshot snapshot;
    const QString hash = canonicalHash(lowerInfoHash);
    const auto statuses = engine_->allStatuses();
    const auto status = std::find_if(statuses.cbegin(), statuses.cend(),
                                     [&hash](const TorrentStatus &value) {
                                         return canonicalHash(value.infoHash) == hash;
                                     });
    if (status == statuses.cend())
        return snapshot;

    snapshot.metadataReady = engine_->hasMetadata(hash);
    snapshot.torrentName = status->name;
    const auto views = fileViews(hash);
    for (const auto &file : views) {
        EngineFs::EngineFsFileSnapshot value;
        value.length = file.length;
        value.name = file.name;
        value.path = file.path;
        value.offset = file.offset;
        snapshot.files.push_back(std::move(value));
    }
#ifdef HAS_LIBTORRENT
    const lt::torrent_handle handle = engine_->torrentHandle(hash);
    const auto info = handle.torrent_file();
    if (info)
        snapshot.pieceLength = info->piece_length();
    if (info) {
        for (int piece = 0; piece < info->num_pieces(); ++piece)
            if (handle.have_piece(lt::piece_index_t{piece}))
                snapshot.availablePieces.insert(piece);
    }
#endif
    const auto peers = engine_->peersFor(hash);
    snapshot.uniquePeers = peers.size();
    snapshot.swarmConnections = peers.size();
    snapshot.wires.reserve(peers.size());
    for (const auto &peer : peers) {
        EngineFs::EngineFsWireSnapshot wire;
        wire.address = peer.address + QLatin1Char(':') + QString::number(peer.port);
        wire.peerChoking = true;
        wire.isSeeder = peer.progress >= 0.999f;
        wire.downSpeed = peer.downSpeed;
        wire.upSpeed = peer.upSpeed;
        snapshot.wires.push_back(std::move(wire));
    }
    snapshot.swarmPaused = status->stateString == QStringLiteral("paused");
    snapshot.downloaded = status->totalDone;
    snapshot.uploaded = 0;
    snapshot.downloadSpeed = status->downloadRate;
    snapshot.uploadSpeed = status->uploadRate;
    return snapshot;
}

void ProductionTorrentCreateSource::load(
    const QString &source, std::function<void(QByteArray, QString)> complete)
{
    if (!complete)
        return;
    const QUrl url(source);
    if (url.isLocalFile() || url.scheme().isEmpty()) {
        QFile file(url.isLocalFile() ? url.toLocalFile() : source);
        if (!file.open(QIODevice::ReadOnly)) {
            complete({}, file.errorString());
            return;
        }
        complete(file.readAll(), {});
        return;
    }
    if (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")) {
        complete({}, QStringLiteral("torrent source must be a local file or HTTP(S) URL"));
        return;
    }

    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = network.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray bytes = reply->readAll();
    const QString error = reply->error() == QNetworkReply::NoError
        ? QString() : reply->errorString();
    reply->deleteLater();
    complete(bytes, error);
}

} // namespace colosseum::server::integration
