#include "ProductionTorrentBackend.h"

#include "LibtorrentBlockTransport.h"
#include "SchedulerTransportBridge.h"
#include "scheduler/FileStream.h"
#include "scheduler/SchedulerSpine.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../torrent/engine/TorrentEngine.h"

#ifdef HAS_LIBTORRENT
#include <libtorrent/file_storage.hpp>
#include <libtorrent/download_priority.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_status.hpp>
#endif

namespace colosseum::server::integration {

#ifdef HAS_LIBTORRENT
struct PiecePriorityState final
{
    explicit PiecePriorityState(lt::torrent_handle value)
        : torrent(std::move(value))
        , original(torrent.get_piece_priorities())
        , wasAutoManaged(bool(torrent.status().flags & lt::torrent_flags::auto_managed))
        , wasUploadMode(bool(torrent.status().flags & lt::torrent_flags::upload_mode))
        , wasShareMode(bool(torrent.status().flags & lt::torrent_flags::share_mode))
    {
        // Libtorrent's queue manager may rewrite piece priorities when an
        // auto-managed torrent changes state. W06 owns selection for active
        // HTTP readers, so suspend that competing policy for the lease life.
        torrent.unset_flags(lt::torrent_flags::auto_managed
                            | lt::torrent_flags::upload_mode
                            | lt::torrent_flags::share_mode);
        torrent.set_flags(lt::torrent_flags::share_mode);
    }

    ~PiecePriorityState()
    {
        restoreOriginal();
    }

    void restoreOriginal()
    {
        if (!torrent.is_valid())
            return;
        if (!original.empty())
            torrent.prioritize_pieces(original);
        for (std::size_t piece = 0; piece < original.size(); ++piece)
            torrent.piece_priority(lt::piece_index_t{
                                   static_cast<std::int32_t>(piece)},
                                   original[piece]);
        torrent.unset_flags(lt::torrent_flags::auto_managed
                            | lt::torrent_flags::upload_mode
                            | lt::torrent_flags::share_mode);
        if (wasUploadMode)
            torrent.set_flags(lt::torrent_flags::upload_mode);
        if (wasShareMode)
            torrent.set_flags(lt::torrent_flags::share_mode);
        if (wasAutoManaged)
            torrent.set_flags(lt::torrent_flags::auto_managed);
    }

    void applyLocked()
    {
        // Keep selected pieces visible so libtorrent maintains an interested
        // peer connection. W06 remains the sole payload authority:
        // PeerPlugin::write_request() clears and suppresses every ordinary
        // picker reservation before it can reach the wire.
        std::vector<lt::download_priority_t> priorities(
            original.size(), lt::dont_download);
        for (const auto &[id, pieces] : scopes) {
            (void)id;
            for (std::size_t piece = 0; piece < pieces.size(); ++piece) {
                if (pieces[piece])
                    priorities[piece] = lt::top_priority;
            }
        }
        torrent.prioritize_pieces(priorities);
        for (std::size_t piece = 0; piece < priorities.size(); ++piece) {
            if (priorities[piece] == lt::top_priority)
                torrent.piece_priority(lt::piece_index_t{
                                           static_cast<std::int32_t>(piece)},
                                       lt::top_priority);
        }
    }

    std::mutex mutex;
    lt::torrent_handle torrent;
    std::vector<lt::download_priority_t> original;
    bool wasAutoManaged = false;
    bool wasUploadMode = false;
    bool wasShareMode = false;
    std::uint64_t nextScope = 1;
    std::map<std::uint64_t, std::vector<bool>> scopes;
};

struct PiecePriorityLease final
{
    PiecePriorityLease(std::shared_ptr<PiecePriorityState> owner,
                       std::uint64_t scope)
        : state(std::move(owner)), scope(scope)
    {
    }

    ~PiecePriorityLease()
    {
        if (!state)
            return;
        std::lock_guard lock(state->mutex);
        state->scopes.erase(scope);
        if (state->scopes.empty()) {
            state->restoreOriginal();
        } else {
            state->applyLocked();
        }
    }

    void selectRange(std::size_t first, std::size_t last)
    {
        std::lock_guard lock(state->mutex);
        const auto found = state->scopes.find(scope);
        if (found == state->scopes.end())
            return;
        std::fill(found->second.begin(), found->second.end(), false);
        if (first <= last && first < found->second.size()) {
            last = std::min(last, found->second.size() - 1);
            for (std::size_t piece = first; piece <= last; ++piece)
                found->second[piece] = true;
        }
        state->applyLocked();
    }

    std::shared_ptr<PiecePriorityState> state;
    std::uint64_t scope = 0;
};
#else
struct PiecePriorityState final {};
struct PiecePriorityLease final {};
#endif

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

class ProductionTorrentStreamSession final
    : public QObject
    , public TorrentStreamSession
    , public std::enable_shared_from_this<ProductionTorrentStreamSession>
{
public:
    ProductionTorrentStreamSession(lt::torrent_handle torrent,
                                    const torrent_http::TorrentReadPlan &plan,
                                    const std::shared_ptr<server::CancellationToken> &cancellation,
                                    std::shared_ptr<PiecePriorityLease> priorityLease,
                                    TorrentStreamCallbacks callbacks,
                                    std::shared_ptr<LibtorrentBlockTransport> transport,
                                    std::shared_ptr<TorrentVerifiedPieceCache> verifiedCache,
                                    bool schedulerDispatchEnabled,
                                    QObject *parent = nullptr)
        : QObject(parent)
        , torrent_(std::move(torrent))
        , plan_(plan)
        , cancellation_(cancellation)
        , priorityLease_(std::move(priorityLease))
        , callbacks_(std::move(callbacks))
        , transport_(std::move(transport))
    {
        const auto info = torrent_.torrent_file();
        if (!info || plan_.fileIndex < 0 || plan_.fileIndex >= info->files().num_files())
            throw std::invalid_argument("torrent stream file is unavailable");

        const auto &storage = info->files();
        // The native scheduler owns piece selection for this HTTP reader. A
        // resumed libtorrent torrent otherwise continues its ordinary rarest-
        // first picker and can download unrelated files beside the requested
        // stream, defeating scheduler authority (and multifile isolation).
        std::vector<std::size_t> lengths;
        lengths.reserve(static_cast<std::size_t>(info->num_pieces()));
        for (int piece = 0; piece < info->num_pieces(); ++piece) {
            const auto offset = static_cast<qint64>(piece) * info->piece_length();
            lengths.push_back(static_cast<std::size_t>(
                std::min<qint64>(info->piece_length(), info->total_size() - offset)));
        }
        scheduler_ = std::make_unique<scheduler::SchedulerSpine>(std::move(lengths));
        source_ = std::make_shared<TorrentPieceSource>(
            torrent_, std::move(verifiedCache), true);
        if (!transport_)
            throw std::invalid_argument("torrent block transport is unavailable");
        bridge_ = std::make_unique<SchedulerTransportBridge>(
            *scheduler_, *transport_, transport_->metrics());
        bridge_->setDispatchEnabled(schedulerDispatchEnabled);
        bridge_->setAllowChokedBootstrap(true);
        bridge_->setCompletedObserver([this](scheduler::CompletedPiece completed) {
            if (destroyed_)
                return;
            // The scheduler receives peer bytes before libtorrent's disk
            // writer necessarily makes them visible to QFile. Retain the
            // assembled piece until libtorrent confirms have_piece(); the
            // source remains gated by that verification bit and then serves
            // these exact bytes without reopening the visibility race.
            completedPieces_[completed.piece] = std::move(completed.bytes);
            scheduler_->refresh();
            refreshAvailability();
        });

        for (int piece = 0; piece < info->num_pieces(); ++piece) {
            if (torrent_.have_piece(lt::piece_index_t{piece})) {
                initiallyAvailablePieces_.insert(static_cast<std::size_t>(piece));
                scheduler_->markPieceAvailable(static_cast<std::size_t>(piece));
                source_->markPieceVisible(static_cast<std::size_t>(piece));
            }
        }

        const auto fileLength = storage.file_size(lt::file_index_t{plan_.fileIndex});
        if (fileLength <= 0 || plan_.start < 0 || plan_.end < plan_.start
            || plan_.end >= fileLength)
            throw std::out_of_range("torrent stream range is unavailable");

        const auto requestedFile = lt::file_index_t{plan_.fileIndex};
        const auto requestedStart = storage.file_offset(requestedFile) + plan_.start;
        const auto requestedEnd = storage.file_offset(requestedFile) + plan_.end;
        const auto firstRequestedPiece = requestedStart / info->piece_length();
        const auto lastRequestedPiece = requestedEnd / info->piece_length();
        if (priorityLease_)
            priorityLease_->selectRange(static_cast<std::size_t>(firstRequestedPiece),
                                        static_cast<std::size_t>(lastRequestedPiece));

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
        connect(&timer_, &QTimer::timeout, this, [this] { pumpScheduler(); });
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
        if (QThread::currentThread() != thread()) {
            if (thread() && thread()->isRunning()) {
                const auto self = shared_from_this();
                QMetaObject::invokeMethod(this, [self] { self->startOnThread(); },
                                          Qt::QueuedConnection);
            }
            return;
        }
        startOnThread();
    }

    void destroy() override
    {
        if (QThread::currentThread() != thread() && thread() && thread()->isRunning()) {
            // Teardown can originate on the HTTP worker while the runtime
            // thread is synchronously stopping that worker.  A blocking
            // cross-thread invoke would deadlock that shutdown cycle.
            const auto self = shared_from_this();
            QMetaObject::invokeMethod(this, [self] { self->destroyOnThread(); },
                                      Qt::QueuedConnection);
            return;
        }
        destroyOnThread();
    }

private:
    void startOnThread()
    {
        if (destroyed_ || started_)
            return;
        started_ = true;
        refreshAvailability();
        if (!destroyed_ && !stream_->ended()) {
            timer_.start();
            stream_->start();
            pumpScheduler();
        }
    }

    void destroyOnThread()
    {
        if (destroyed_)
            return;
        destroyed_ = true;
        timer_.stop();
        if (stream_)
            stream_->destroy();
        callbacks_ = {};
        priorityLease_.reset();
    }

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
            const auto pieceNumber = static_cast<std::size_t>(piece);
            if (!torrent_.have_piece(lt::piece_index_t{piece}))
                continue;
            const auto provided = completedPieces_.find(pieceNumber);
            if (!initiallyAvailablePieces_.contains(pieceNumber)
                && provided == completedPieces_.end()) {
                // A W06 transport completion is the authoritative visibility
                // event for a newly downloaded piece. Do not let libtorrent's
                // have_piece() transition race ahead and expose a disk read
                // before the scheduler has assembled the verified bytes.
                continue;
            }
            if (provided != completedPieces_.end()) {
                source_->provideVerifiedPiece(pieceNumber,
                                              std::move(provided->second));
                completedPieces_.erase(provided);
            }
            source_->notifyPieceFinished(pieceNumber);
            if (!scheduler_->isPieceAvailable(pieceNumber)) {
                scheduler_->markPieceAvailable(pieceNumber);
                changed = true;
            }
        }
        if (changed)
            scheduler_->refresh();
    }

    void refreshPeers()
    {
        const auto info = torrent_.torrent_file();
        if (!info || !bridge_)
            return;

        std::vector<lt::peer_info> peers;
        torrent_.get_peer_info(peers);
        std::set<std::string> current;
        const auto pieceCount = static_cast<std::size_t>(info->num_pieces());
        for (const auto &snapshot : peers) {
            if (snapshot.ip.port() == 0)
                continue;
            scheduler::PeerState peer;
            peer.id = LibtorrentBlockTransport::peerIdentity(snapshot.ip);
            peer.peerChoking = !!(snapshot.flags & lt::peer_info::remote_choked);
            peer.peerInterested = !!(snapshot.flags & lt::peer_info::remote_interested);
            peer.amInterested = !!(snapshot.flags & lt::peer_info::interesting);
            peer.amChoking = !!(snapshot.flags & lt::peer_info::choked);
            peer.downloaded = static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, snapshot.total_download));
            peer.downloadSpeed = static_cast<double>(
                std::max(0, snapshot.payload_down_speed));
            peer.uploadSpeed = static_cast<double>(
                std::max(0, snapshot.payload_up_speed));
            peer.peerPieces.resize(pieceCount);
            for (std::size_t piece = 0; piece < pieceCount; ++piece) {
                const auto index = lt::piece_index_t{
                    static_cast<std::int32_t>(piece)};
                if (piece < static_cast<std::size_t>(snapshot.pieces.size()))
                    peer.peerPieces[piece] = snapshot.pieces[index];
            }
            current.insert(peer.id);
            bridge_->upsertPeer(std::move(peer));
        }

        for (const auto &peerId : peerIds_) {
            if (!current.contains(peerId))
                bridge_->retirePeer(peerId);
        }
        peerIds_ = std::move(current);
    }

    void pumpScheduler()
    {
        if (cancelled()) {
            destroy();
            return;
        }
        refreshPeers();
        if (bridge_)
            bridge_->pump();
        refreshAvailability();
    }

    lt::torrent_handle torrent_;
    torrent_http::TorrentReadPlan plan_;
    std::shared_ptr<server::CancellationToken> cancellation_;
    std::shared_ptr<PiecePriorityLease> priorityLease_;
    TorrentStreamCallbacks callbacks_;
    std::unique_ptr<scheduler::SchedulerSpine> scheduler_;
    std::shared_ptr<TorrentPieceSource> source_;
    std::shared_ptr<LibtorrentBlockTransport> transport_;
    std::unique_ptr<SchedulerTransportBridge> bridge_;
    std::set<std::string> peerIds_;
    std::set<std::size_t> initiallyAvailablePieces_;
    std::unique_ptr<scheduler::FileStream> stream_;
    std::map<std::size_t, std::vector<std::byte>> completedPieces_;
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

bool ProductionTorrentBackend::ensureRecord(const QString &lowerInfoHash,
                                            const QJsonObject &effectiveOptions,
                                            QString *error)
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
    applyPeerSearchSources(hash, effectiveOptions);
    if (error)
        error->clear();
    return true;
}

void ProductionTorrentBackend::applyPeerSearchSources(
    const QString &lowerInfoHash, const QJsonObject &effectiveOptions)
{
    const QJsonArray sources = effectiveOptions.value(QStringLiteral("peerSearch"))
                                   .toObject()
                                   .value(QStringLiteral("sources"))
                                   .toArray();
    QStringList trackers;
    trackers.reserve(sources.size());
    for (const QJsonValue &sourceValue : sources) {
        const QString source = sourceValue.toString().trimmed();
        if (source.startsWith(QStringLiteral("tracker:"), Qt::CaseInsensitive)) {
            const QString url = source.mid(QStringLiteral("tracker:").size()).trimmed();
            if (!url.isEmpty())
                trackers.push_back(url);
        } else if (source.startsWith(QStringLiteral("udp://"), Qt::CaseInsensitive)
                   || source.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
                   || source.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
                   || source.startsWith(QStringLiteral("ws://"), Qt::CaseInsensitive)
                   || source.startsWith(QStringLiteral("wss://"), Qt::CaseInsensitive)) {
            // TorrentHttpSurface carries request `tr` values as raw URLs;
            // EngineFS defaults carry the source-port's `tracker:` prefix.
            trackers.push_back(source);
        }
    }
    engine_->replaceTrackers(canonicalHash(lowerInfoHash), trackers);
}

void ProductionTorrentBackend::ensureEngine(const QString &lowerInfoHash,
                                             const QJsonObject &options,
                                             ReadyCallback ready)
{
    const QString hash = canonicalHash(lowerInfoHash);
    QJsonObject effectiveOptions = defaultEngineOptions(hash);
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (it.key() != QStringLiteral("peerSearch")) {
            effectiveOptions.insert(it.key(), it.value());
            continue;
        }
        QJsonObject peerSearch = effectiveOptions.value(QStringLiteral("peerSearch"))
                                     .toObject();
        const QJsonObject overridePeerSearch = it.value().toObject();
        for (auto peerIt = overridePeerSearch.constBegin();
             peerIt != overridePeerSearch.constEnd(); ++peerIt)
            peerSearch.insert(peerIt.key(), peerIt.value());
        effectiveOptions.insert(QStringLiteral("peerSearch"), peerSearch);
    }
    QString error;
    if (!ensureRecord(hash, effectiveOptions, &error)) {
        if (ready)
            ready(error);
        return;
    }

    // A request-level `tr` override is intentionally applied even when the
    // EngineFS record already exists. This mirrors module 172's per-create
    // peerSearch replacement while leaving torrent-file announce metadata
    // untouched on the normal no-override path.
    if (options.value(QStringLiteral("peerSearch")).toObject()
            .contains(QStringLiteral("sources")))
        applyPeerSearchSources(hash, effectiveOptions);

    if (controlPlane_ && !controlPlane_->exists(hash))
        controlPlane_->createEngine(hash, effectiveOptions);
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
    const QJsonObject effectiveOptions = defaultEngineOptions(hash);
    // Module 172's /create passes parsed metainfo as options.torrent and uses
    // its announce list when present; trackerless metainfo falls back to the
    // default peerSearch sources. libtorrent already owns announced trackers
    // from the .torrent, so augment only the trackerless case here.
    if (engine_->trackersFor(hash).isEmpty())
        engine_->replaceTrackers(hash, TorrentEngine::defaultTrackerPool());
    if (controlPlane_)
        controlPlane_->createEngine(hash, effectiveOptions);
    else
        engine_->resumeTorrent(hash);
    if (ready)
        ready(hash, {});
}

QJsonObject ProductionTorrentBackend::defaultEngineOptions(const QString &lowerInfoHash) const
{
    const auto values = settings_.values();
    const auto positiveInteger = [&values](const QString &key,
                                           const double fallback) {
        const QJsonValue value = values.value(key);
        if (!value.isDouble())
            return fallback;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number <= 0.0
            || std::floor(number) != number)
            return fallback;
        return number;
    };

    QStringList sources;
    const auto &defaultTrackers = TorrentEngine::defaultTrackerPool();
    sources.reserve(defaultTrackers.size() + 1);
    for (const QString &tracker : defaultTrackers)
        sources.push_back(QStringLiteral("tracker:") + tracker);
    sources.push_back(QStringLiteral("dht:") + canonicalHash(lowerInfoHash));

    const auto minPeers = positiveInteger(QStringLiteral("btMinPeersForStable"), 5.0);
    QJsonObject swarmCap{{QStringLiteral("minPeers"), minPeers},
                         {QStringLiteral("maxSpeed"),
                          positiveInteger(QStringLiteral("btDownloadSpeedSoftLimit"),
                                          1677721.6)}};
    QJsonObject defaults{
        {QStringLiteral("peerSearch"),
         QJsonObject{{QStringLiteral("min"), 40},
                     {QStringLiteral("max"), 150},
                     {QStringLiteral("sources"),
                      QJsonArray::fromStringList(sources)}}},
        {QStringLiteral("dht"), false},
        {QStringLiteral("tracker"), false},
        {QStringLiteral("connections"),
         positiveInteger(QStringLiteral("btMaxConnections"), 35.0)},
        {QStringLiteral("handshakeTimeout"),
         positiveInteger(QStringLiteral("btHandshakeTimeout"), 20000.0)},
        {QStringLiteral("timeout"),
         positiveInteger(QStringLiteral("btRequestTimeout"), 4000.0)},
        {QStringLiteral("virtual"), true},
        {QStringLiteral("swarmCap"), swarmCap},
        {QStringLiteral("growler"),
         QJsonObject{{QStringLiteral("flood"), 0},
                     {QStringLiteral("pulse"),
                      positiveInteger(QStringLiteral("btDownloadSpeedHardLimit"),
                                      2621440.0)}}}};

    // Module 46923 enables this memory-backed policy when caching is disabled
    // (including the source port's --no-cache path). ServerSettings persists
    // that mode as cacheSize == 0; argv.noCache has no native counterpart.
    if (values.value(QStringLiteral("cacheSize")).isDouble()
        && values.value(QStringLiteral("cacheSize")).toDouble() == 0.0) {
        defaults.insert(QStringLiteral("buffer"), 15728640);
        defaults.insert(QStringLiteral("circularBuffer"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("memory")},
                                    {QStringLiteral("size"), 47185920}});
        defaults.insert(QStringLiteral("swarmCap"),
                        QJsonObject{{QStringLiteral("minPeers"), minPeers},
                                    {QStringLiteral("maxBuffer"), 0.75}});
    }
    return defaults;
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
    auto cleanup = [this, hash, complete = std::move(complete)]() mutable {
        // The engine handle is gone before this callback runs. Drop the
        // backend-owned per-torrent adapters now, while allowing active HTTP
        // sessions to finish through their own shared references. This keeps
        // a remove/re-add cycle from reusing a plugin state or priority lease
        // belonging to the old libtorrent torrent instance.
        {
            std::lock_guard lock(mutex_);
            blockTransports_.remove(hash);
            verifiedPieceCaches_.remove(hash);
            priorityLeases_.remove(hash);
            readyCallbacks_.remove(hash);
        }
        if (complete)
            complete();
    };
    if (controlPlane_)
        controlPlane_->removeEngine(hash, std::move(cleanup));
    else {
        engine_->removeTorrent(hash);
        cleanup();
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
    // Prewarm may wake the swarm for metadata/peer discovery, but it must not
    // install libtorrent's sequential picker as a competing stream scheduler.
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

std::shared_ptr<PiecePriorityLease> ProductionTorrentBackend::acquirePiecePriorityLease(
    const QString &lowerInfoHash)
{
#ifdef HAS_LIBTORRENT
    const QString hash = canonicalHash(lowerInfoHash);
    const lt::torrent_handle handle = engine_->torrentHandle(hash);
    if (!handle.is_valid() || !handle.torrent_file())
        return {};

    std::shared_ptr<PiecePriorityState> state;
    {
        std::lock_guard lock(mutex_);
        state = priorityLeases_.value(hash);
        if (!state) {
            state = std::make_shared<PiecePriorityState>(handle);
            priorityLeases_.insert(hash, state);
        }
        std::lock_guard stateLock(state->mutex);
        const auto scope = state->nextScope++;
        state->scopes.emplace(scope,
                              std::vector<bool>(state->original.size(), false));
        state->applyLocked();
        return std::make_shared<PiecePriorityLease>(std::move(state), scope);
    }
#else
    Q_UNUSED(lowerInfoHash);
    return {};
#endif
}

std::shared_ptr<LibtorrentBlockTransport> ProductionTorrentBackend::blockTransportFor(
    const QString &lowerInfoHash)
{
#ifdef HAS_LIBTORRENT
    const QString hash = canonicalHash(lowerInfoHash);
    std::lock_guard lock(mutex_);
    if (const auto existing = blockTransports_.value(hash))
        return existing;

    const lt::torrent_handle handle = engine_->torrentHandle(hash);
    if (!handle.is_valid())
        return {};
    auto transport = std::make_shared<LibtorrentBlockTransport>(handle);
    blockTransports_.insert(hash, transport);
    return transport;
#else
    Q_UNUSED(lowerInfoHash);
    return {};
#endif
}

std::shared_ptr<TorrentVerifiedPieceCache> ProductionTorrentBackend::verifiedPieceCacheFor(
    const QString &lowerInfoHash)
{
#ifdef HAS_LIBTORRENT
    const QString hash = canonicalHash(lowerInfoHash);
    std::lock_guard lock(mutex_);
    if (const auto existing = verifiedPieceCaches_.value(hash))
        return existing;
    auto cache = std::make_shared<TorrentVerifiedPieceCache>();
    verifiedPieceCaches_.insert(hash, cache);
    return cache;
#else
    Q_UNUSED(lowerInfoHash);
    return {};
#endif
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
        const auto transport = blockTransportFor(plan.infoHash);
        if (!transport)
            return {};
        const auto verifiedCache = verifiedPieceCacheFor(plan.infoHash);
        const auto priorityLease = acquirePiecePriorityLease(plan.infoHash);
        return std::make_shared<ProductionTorrentStreamSession>(
            handle, plan, cancellation, priorityLease, std::move(callbacks), transport,
            verifiedCache, schedulerDispatchEnabledForTests_);
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

SchedulerTransportMetricsSnapshot ProductionTorrentBackend::schedulerTransportMetrics(
    const QString &lowerInfoHash) const
{
#ifdef HAS_LIBTORRENT
    const QString hash = canonicalHash(lowerInfoHash);
    std::shared_ptr<LibtorrentBlockTransport> transport;
    {
        std::lock_guard lock(mutex_);
        transport = blockTransports_.value(hash);
    }
    return transport ? transport->metricsSnapshot()
                     : SchedulerTransportMetricsSnapshot{};
#else
    Q_UNUSED(lowerInfoHash);
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
