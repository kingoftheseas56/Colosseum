#include "EngineFsControlPlane.h"

#include <QMetaType>

#include <limits>
#include <stdexcept>
#include <utility>

namespace Colosseum::Server::EngineFs {

EngineFsControlPlane::EngineFsControlPlane(IEngineFsBackendFactory& factory,
                                           IEngineFsTimerScheduler& scheduler,
                                           EventSink sink,
                                           EngineFsTimeouts timeouts)
    : factory_(factory)
    , scheduler_(scheduler)
    , sink_(std::move(sink))
    , timeouts_(timeouts)
{
}

EngineFsControlPlane::~EngineFsControlPlane()
{
    lifetime_.reset();
    cancelTimers(streamTimers_);
    cancelTimers(engineTimers_);
    cancelTimers(cacheTimers_);
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
        it->backend->setCallbacks({});
}

void EngineFsControlPlane::createEngine(const QString& infoHash,
                                        const QJsonObject& effectiveOptions,
                                        ReadyCallback ready)
{
    const QString hash = canonicalInfoHash(infoHash);
    QVariantList createArgs;
    createArgs << hash << QVariant::fromValue(effectiveOptions);
    emitJoined(QStringLiteral("engine-create"), createArgs);

    const bool isNew = !entries_.contains(hash);
    if (isNew) {
        auto backend = factory_.create(hash, effectiveOptions);
        if (!backend)
            throw std::runtime_error("EngineFS backend factory returned null");
        entries_.insert(hash, Entry{backend, effectiveOptions});
        order_.push_back(hash);
    }

    Entry& entry = entries_[hash];
    entry.options = effectiveOptions;
    entry.backend->resumeSwarm();

    const std::weak_ptr<int> guard(lifetime_);
    const auto backend = entry.backend;
    if (isNew) {
        EngineFsBackendCallbacks callbacks;
        callbacks.onError = [this, guard, hash](const QString& message) {
            if (guard.expired())
                return;
            emitEvent(QStringLiteral("engine-error:") + hash, {message});
            emitEvent(QStringLiteral("engine-error"), {hash, message});
        };
        callbacks.onInvalidPiece = [this, guard, hash](int piece) {
            if (guard.expired())
                return;
            emitEvent(QStringLiteral("engine-invalid-piece:") + hash, {piece});
            emitEvent(QStringLiteral("engine-invalid-piece"), {hash, piece});
        };
        backend->setCallbacks(std::move(callbacks));
        emitJoined(QStringLiteral("engine-created"), {hash});
    }

    // Stremio 4.20.17 module 172 registers a ready callback on every create,
    // including reuse, so a reused ready engine fans out engine-ready again.
    backend->whenReady([this, guard, hash, backend, ready = std::move(ready)](const QVariant& torrent) {
        if (guard.expired())
            return;
        emitEvent(QStringLiteral("engine-ready:") + hash, {torrent});
        emitEvent(QStringLiteral("engine-ready"), {hash, torrent});
        if (ready)
            ready(backend);
    });
}

bool EngineFsControlPlane::exists(const QString& infoHash) const
{
    return entries_.contains(canonicalInfoHash(infoHash));
}

QStringList EngineFsControlPlane::list() const
{
    return order_;
}

QVector<QJsonObject> EngineFsControlPlane::selections(const QString& infoHash) const
{
    const auto it = entries_.constFind(canonicalInfoHash(infoHash));
    return it == entries_.cend() ? QVector<QJsonObject>{} : it->backend->statisticsSnapshot().selections;
}

void EngineFsControlPlane::removeEngine(const QString& infoHash, DoneCallback done)
{
    const QString hash = canonicalInfoHash(infoHash);
    const auto it = entries_.find(hash);
    if (it == entries_.end()) {
        if (done)
            done();
        return;
    }

    const auto backend = it->backend;
    const std::weak_ptr<int> guard(lifetime_);
    backend->destroy([this, guard, hash, backend, done = std::move(done)]() mutable {
        if (guard.expired())
            return;
        const auto current = entries_.find(hash);
        if (current != entries_.end() && current->backend == backend) {
            // Module 172 emits engine-destroyed before deleting the registry entry.
            emitJoined(QStringLiteral("engine-destroyed"), {hash});
            entries_.erase(current);
            order_.removeAll(hash);
        }
        if (done)
            done();
    });
}

void EngineFsControlPlane::keepConcurrency(const QString& incomingHash,
                                            int concurrency,
                                            DoneCallback done)
{
    const int enginesCount = order_.size() + 1;
    if (concurrency <= 0 || enginesCount <= concurrency) {
        if (done)
            done();
        return;
    }
    const QString incoming = canonicalInfoHash(incomingHash);
    QStringList candidates;
    for (const QString& hash : order_) {
        if (hash != incoming && selections(hash).isEmpty())
            candidates.push_back(hash);
    }

    const int removeCount = qMin(candidates.size(), enginesCount - concurrency);
    if (removeCount <= 0) {
        if (done)
            done();
        return;
    }

    // Module 172 starts all removals and resolves when the callback belonging
    // to the last selected index fires. Keep that observable ordering.
    const auto completion = std::make_shared<DoneCallback>(std::move(done));
    for (int i = 0; i < removeCount; ++i) {
        removeEngine(candidates.at(i), [completion, i, removeCount] {
            if (i == removeCount - 1 && *completion)
                (*completion)();
        });
    }
}

void EngineFsControlPlane::noteStreamOpen(const QString& infoHash, int fileIndex)
{
    const QString hash = canonicalInfoHash(infoHash);
    emitEvent(QStringLiteral("stream-open"), {hash, fileIndex});
    const QString streamKey = hash + QLatin1Char(':') + QString::number(fileIndex);
    incrementCounter(streamCounters_, streamTimers_, streamKey, [this, hash, fileIndex] {
        emitJoined(QStringLiteral("stream-active"), {hash, fileIndex});
    });
    incrementCounter(engineCounters_, engineTimers_, hash, [this, hash] {
        emitJoined(QStringLiteral("engine-active"), {hash});
    });
}

void EngineFsControlPlane::noteStreamClose(const QString& infoHash, int fileIndex)
{
    const QString hash = canonicalInfoHash(infoHash);
    emitEvent(QStringLiteral("stream-close"), {hash, fileIndex});
    const QString streamKey = hash + QLatin1Char(':') + QString::number(fileIndex);
    decrementCounter(streamCounters_, streamTimers_, streamKey, timeouts_.stream,
                     [this, hash, fileIndex] {
        if (exists(hash))
            emitJoined(QStringLiteral("stream-inactive"), {hash, fileIndex});
    });
    decrementCounter(engineCounters_, engineTimers_, hash, timeouts_.engine,
                     [this, hash] {
        if (!exists(hash))
            return;
        // Module 564 listens to the direct engine-inactive event and removes
        // the engine before module 172 emits the colon-joined companion event.
        emitEvent(QStringLiteral("engine-inactive"), {hash});
        removeEngine(hash);
        emitEvent(QStringLiteral("engine-inactive:") + hash);
    });
}

void EngineFsControlPlane::noteStreamCreated(const QString& infoHash, int)
{
    const QString hash = canonicalInfoHash(infoHash);
    incrementCounter(cacheCounters_, cacheTimers_, hash, [] {});
}

void EngineFsControlPlane::noteStreamCached(const QString& infoHash, int)
{
    const QString hash = canonicalInfoHash(infoHash);
    decrementCounter(cacheCounters_, cacheTimers_, hash, timeouts_.stream,
                     [this, hash] {
        if (!exists(hash))
            return;
        // Module 564 pauses the swarm while handling the direct engine-idle
        // event; the joined event is emitted after that listener returns.
        emitEvent(QStringLiteral("engine-idle"), {hash});
        pauseSwarm(hash);
        emitEvent(QStringLiteral("engine-idle:") + hash);
    });
}

void EngineFsControlPlane::pauseSwarm(const QString& infoHash)
{
    const auto it = entries_.find(canonicalInfoHash(infoHash));
    if (it != entries_.end())
        it->backend->pauseSwarm();
}

void EngineFsControlPlane::resumeSwarm(const QString& infoHash)
{
    const auto it = entries_.find(canonicalInfoHash(infoHash));
    if (it != entries_.end())
        it->backend->resumeSwarm();
}

QJsonValue EngineFsControlPlane::statistics(const QString& infoHash,
                                            std::optional<int> fileIndex) const
{
    const QString hash = canonicalInfoHash(infoHash);
    const auto it = entries_.constFind(hash);
    if (it == entries_.cend())
        return QJsonValue(QJsonValue::Null);
    return statisticsFor(hash, it.value(), fileIndex);
}

QJsonObject EngineFsControlPlane::statisticsAll() const
{
    QJsonObject result;
    for (const QString& hash : order_) {
        const auto it = entries_.constFind(hash);
        if (it != entries_.cend())
            result.insert(hash, statisticsFor(hash, it.value(), std::nullopt));
    }
    return result;
}

void EngineFsControlPlane::emitEvent(const QString& name, QVariantList args)
{
    if (sink_)
        sink_(EngineFsEvent{name, std::move(args)});
}

void EngineFsControlPlane::emitJoined(const QString& name, const QVariantList& args)
{
    emitEvent(name, args);
    QStringList parts{name};
    for (const QVariant& arg : args)
        parts.push_back(jsJoinText(arg));
    emitEvent(parts.join(QLatin1Char(':')));
}

QString EngineFsControlPlane::jsJoinText(const QVariant& value)
{
    if (!value.isValid() || value.isNull())
        return {};
    if (value.metaType() == QMetaType::fromType<QJsonObject>()
        || value.metaType() == QMetaType::fromType<QJsonArray>())
        return QStringLiteral("[object Object]");
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return value.toString();
}

void EngineFsControlPlane::incrementCounter(CounterMap& counters,
                                            TimerMap& timers,
                                            const QString& key,
                                            std::function<void()> onPositive)
{
    if (!counters.contains(key)) {
        counters.insert(key, 0.0);
        onPositive();
    }
    counters[key] += 1.0;
    const auto timer = timers.find(key);
    if (timer != timers.end()) {
        scheduler_.cancel(timer.value());
        timers.erase(timer);
    }
}

void EngineFsControlPlane::decrementCounter(CounterMap& counters,
                                            TimerMap& timers,
                                            const QString& key,
                                            std::chrono::milliseconds timeout,
                                            std::function<void()> onZero)
{
    counters[key] = counters.value(key, 0.0) - 1.0;
    if (counters.value(key) != 0.0)
        return;

    const auto existing = timers.find(key);
    if (existing != timers.end())
        scheduler_.cancel(existing.value());

    const std::weak_ptr<int> guard(lifetime_);
    CounterMap* counterState = &counters;
    TimerMap* timerState = &timers;
    const TimerId id = scheduler_.schedule(timeout,
        [guard, counterState, timerState, key, onZero = std::move(onZero)]() mutable {
            if (guard.expired())
                return;
            onZero();
            counterState->remove(key);
            timerState->remove(key);
        });
    timers.insert(key, id);
}

void EngineFsControlPlane::cancelTimers(TimerMap& timers)
{
    for (auto it = timers.cbegin(); it != timers.cend(); ++it)
        scheduler_.cancel(it.value());
    timers.clear();
}

QJsonObject EngineFsControlPlane::fileJson(const EngineFsFileSnapshot& file)
{
    QJsonObject result = file.extra;
    result.insert(QStringLiteral("length"), static_cast<double>(file.length));
    result.insert(QStringLiteral("name"), file.name);
    result.insert(QStringLiteral("offset"), static_cast<double>(file.offset));
    result.insert(QStringLiteral("path"), file.path);
    return result;
}
QJsonArray EngineFsControlPlane::selectionsJson(const QVector<QJsonObject>& selections)
{
    QJsonArray result;
    for (const auto& selection : selections)
        result.push_back(selection);
    return result;
}

QJsonArray EngineFsControlPlane::wiresJson(const QVector<EngineFsWireSnapshot>& wires)
{
    QJsonArray result;
    for (const auto& wire : wires) {
        if (wire.peerChoking)
            continue;
        QJsonObject item;
        item.insert(QStringLiteral("requests"), wire.requests);
        item.insert(QStringLiteral("address"), wire.address);
        item.insert(QStringLiteral("amInterested"), wire.amInterested);
        item.insert(QStringLiteral("isSeeder"), wire.isSeeder);
        item.insert(QStringLiteral("downSpeed"), wire.downSpeed);
        item.insert(QStringLiteral("upSpeed"), wire.upSpeed);
        result.push_back(item);
    }
    return result;
}

QJsonObject EngineFsControlPlane::statisticsFor(const QString& hash,
                                                const Entry& entry,
                                                std::optional<int> fileIndex) const
{
    const EngineFsBackendSnapshot snapshot = entry.backend->statisticsSnapshot();
    QJsonObject stats;
    stats.insert(QStringLiteral("infoHash"), hash);
    if (snapshot.metadataReady && snapshot.torrentName)
        stats.insert(QStringLiteral("name"), *snapshot.torrentName);

    stats.insert(QStringLiteral("peers"), snapshot.wires.size());
    int unchoked = 0;
    for (const auto& wire : snapshot.wires)
        if (!wire.peerChoking)
            ++unchoked;
    stats.insert(QStringLiteral("unchoked"), unchoked);
    stats.insert(QStringLiteral("queued"), snapshot.queued);
    stats.insert(QStringLiteral("unique"), snapshot.uniquePeers);
    stats.insert(QStringLiteral("connectionTries"), snapshot.connectionTries);
    stats.insert(QStringLiteral("swarmPaused"), snapshot.swarmPaused);
    stats.insert(QStringLiteral("swarmConnections"), snapshot.swarmConnections);
    stats.insert(QStringLiteral("swarmSize"), snapshot.swarmSize);
    stats.insert(QStringLiteral("selections"), selectionsJson(snapshot.selections));

    if (fileIndex)
        stats.insert(QStringLiteral("wires"), QJsonValue(QJsonValue::Null));
    else
        stats.insert(QStringLiteral("wires"), wiresJson(snapshot.wires));

    if (snapshot.metadataReady) {
        QJsonArray files;
        for (const auto& file : snapshot.files)
            files.push_back(fileJson(file));
        stats.insert(QStringLiteral("files"), files);
    }

    stats.insert(QStringLiteral("downloaded"), static_cast<double>(snapshot.downloaded));
    stats.insert(QStringLiteral("uploaded"), static_cast<double>(snapshot.uploaded));
    stats.insert(QStringLiteral("downloadSpeed"), snapshot.downloadSpeed);
    // Preserve module 172 exactly: uploadSpeed accidentally reports downloadSpeed().
    stats.insert(QStringLiteral("uploadSpeed"), snapshot.downloadSpeed);
    if (snapshot.peerSearchSources)
        stats.insert(QStringLiteral("sources"), *snapshot.peerSearchSources);
    if (snapshot.peerSearchRunning)
        stats.insert(QStringLiteral("peerSearchRunning"), *snapshot.peerSearchRunning);
    stats.insert(QStringLiteral("opts"), entry.options);

    if (fileIndex && snapshot.metadataReady
        && *fileIndex >= 0 && *fileIndex < snapshot.files.size()) {
        const auto& file = snapshot.files.at(*fileIndex);
        stats.insert(QStringLiteral("streamLen"), static_cast<double>(file.length));
        stats.insert(QStringLiteral("streamName"), file.name);

        if (snapshot.pieceLength > 0) {
            const qint64 startPiece = file.offset / snapshot.pieceLength;
            const qint64 endPiece = (file.offset + file.length - 1) / snapshot.pieceLength;
            qint64 availablePieces = 0;
            for (qint64 piece = startPiece; piece <= endPiece; ++piece)
                if (snapshot.availablePieces.contains(static_cast<int>(piece)))
                    ++availablePieces;
            const qint64 filePieces = (file.length + snapshot.pieceLength - 1)
                / snapshot.pieceLength;
            if (filePieces > 0)
                stats.insert(QStringLiteral("streamProgress"),
                             static_cast<double>(availablePieces) / static_cast<double>(filePieces));
            else
                stats.insert(QStringLiteral("streamProgress"), QJsonValue(QJsonValue::Null));
        } else {
            stats.insert(QStringLiteral("streamProgress"), QJsonValue(QJsonValue::Null));
        }
    }
    return stats;
}

} // namespace Colosseum::Server::EngineFs
