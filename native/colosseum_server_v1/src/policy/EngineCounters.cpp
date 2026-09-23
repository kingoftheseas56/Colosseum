#include "server1/policy/EngineLifecycle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>

namespace server1::policy {
namespace {

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// JavaScript `x | 0`.
std::int64_t toInt32(double value)
{
    return static_cast<std::int64_t>(jsToInt32(Value::number(value)));
}

Value fileValue(const TorrentFile &file)
{
    return Value::object({
        {"path", Value::string(file.path)},
        {"name", Value::string(file.name)},
        {"length", Value::number(static_cast<double>(file.length))},
        {"offset", Value::number(static_cast<double>(file.offset))},
        {"__cacheEvents", Value::boolean(true)},
    });
}

std::string indexText(std::size_t idx)
{
    return jsNumberToString(static_cast<double>(idx));
}

} // namespace

// ---- M662 Counter ----

struct EngineCounter::Entry final {
    bool nan = false;
    std::int64_t count = 0;
    std::shared_ptr<EngineTimer> timer;
};

EngineCounter::EngineCounter(IdFn id, Hook onPositive, Hook onZero, TimeoutFn timeout,
                             LifecycleTimeout setTimeout)
    : id_(std::move(id)), onPositive_(std::move(onPositive)), onZero_(std::move(onZero)),
      timeout_(std::move(timeout)), setTimeout_(std::move(setTimeout)),
      entries_(std::make_shared<std::map<std::string, Entry>>())
{
}

EngineCounter::~EngineCounter()
{
    cancelAll();
}

void EngineCounter::increment(const std::string &hash, std::size_t idx)
{
    const auto id = id_(hash, idx);
    auto found = entries_->find(id);
    if (found == entries_->end()) {
        found = entries_->emplace(id, Entry{}).first;
        if (onPositive_)
            onPositive_(hash, idx);
        found = entries_->find(id);
        if (found == entries_->end())
            return;
    }
    if (!found->second.nan)
        ++found->second.count;
    if (found->second.timer) {
        found->second.timer->cancel();
        found->second.timer.reset();
    }
}

void EngineCounter::decrement(const std::string &hash, std::size_t idx)
{
    const auto id = id_(hash, idx);
    auto found = entries_->find(id);
    if (found == entries_->end()) {
        // counter[id]-- on an unknown id stores NaN; the id never reaches 0.
        Entry entry;
        entry.nan = true;
        entries_->emplace(id, std::move(entry));
        return;
    }
    if (found->second.nan)
        return;
    if (--found->second.count != 0)
        return;
    if (found->second.timer)
        found->second.timer->cancel();
    std::weak_ptr<std::map<std::string, Entry>> weak = entries_;
    auto hook = onZero_;
    found->second.timer = setTimeout_(timeout_ ? timeout_() : 0, [weak, hook, id, hash, idx]() {
        const auto entries = weak.lock();
        if (!entries)
            return;
        if (hook)
            hook(hash, idx);
        entries->erase(id);
    });
}

EngineCounter::Slot EngineCounter::slot(const std::string &id) const
{
    const auto found = entries_->find(id);
    if (found == entries_->end())
        return {};
    return {true, found->second.nan, found->second.count,
            found->second.timer && found->second.timer->active()};
}

void EngineCounter::cancelAll() noexcept
{
    for (auto &[id, entry] : *entries_) {
        static_cast<void>(id);
        if (entry.timer)
            entry.timer->cancel();
        entry.timer.reset();
    }
}

// ---- EngineCatalog over EngineRegistry ----

RegistryEngineCatalog::RegistryEngineCatalog(EngineRegistry &registry) : registry_(registry) {}

void RegistryEngineCatalog::observe(const EngineEvent &event)
{
    const auto key = lowercase(event.sourceKey);
    if (event.type == EngineEventType::Created) {
        if (std::find(order_.begin(), order_.end(), key) == order_.end())
            order_.push_back(key);
    } else if (event.type == EngineEventType::Destroyed) {
        order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
    }
}

std::vector<std::string> RegistryEngineCatalog::list() const
{
    std::vector<std::string> result;
    for (const auto &key : order_)
        if (registry_.exists(key))
            result.push_back(key);
    // Engines created before observe() was wired keep the registry's order.
    for (const auto &key : registry_.list())
        if (std::find(result.begin(), result.end(), key) == result.end())
            result.push_back(key);
    return result;
}

bool RegistryEngineCatalog::exists(const std::string &hash) const
{
    return registry_.exists(hash);
}

std::size_t RegistryEngineCatalog::selectionCount(const std::string &hash) const
{
    const auto engine = registry_.get(hash);
    return engine ? engine->selectionCount() : 0;
}

void RegistryEngineCatalog::destroy(const std::string &hash, std::function<void()> done)
{
    registry_.remove(hash, [done = std::move(done)](bool) {
        if (done)
            done();
    });
}

LifecycleEngine *RegistryEngineCatalog::engine(const std::string &)
{
    return nullptr;
}

// ---- LifecycleStream ----

void LifecycleStream::finish()
{
    close();
}

void LifecycleStream::close()
{
    if (closed_)
        return;
    closed_ = true;
    if (emitClose_) {
        auto emit = std::move(emitClose_);
        emitClose_ = {};
        emit();
    }
}

bool LifecycleStream::closed() const noexcept
{
    return closed_;
}

// ---- EngineLifecycle ----

struct EngineLifecycle::Impl final : std::enable_shared_from_this<Impl> {
    struct Tracker final {
        std::uint64_t serial = 0;
        std::string hash;
        std::size_t idx = 0;
        Value file;
        std::vector<std::int64_t> missing;
        double filePieces = 0.0;
    };

    Impl(EngineCatalog &catalogRef, EngineLifecycleConfig configValue)
        : catalog(catalogRef), config(std::move(configValue)),
          streamTimeout(config.streamTimeoutMs), engineTimeout(config.engineTimeoutMs)
    {
    }

    EngineCatalog &catalog;
    EngineLifecycleConfig config;
    std::uint64_t streamTimeout;
    std::uint64_t engineTimeout;
    std::unique_ptr<EngineCounter> streams;
    std::unique_ptr<EngineCounter> engines;
    std::unique_ptr<EngineCounter> idle;
    // file.__cacheEvents, per engine instance.
    std::map<std::string, std::pair<std::uint64_t, std::set<std::size_t>>> cacheEvents;
    // e.on("verify", onDownload) listeners, per engine.
    std::map<std::string, std::vector<Tracker>> trackers;
    std::uint64_t trackerSerial = 0;
    std::size_t failedOpens = 0;
    bool destroyed = false;

    void event(std::string name, Value::Array args)
    {
        if (config.onEvent)
            config.onEvent(LifecycleEvent{name, args});
    }

    // EngineFS.emit plus the listeners M172 registers, in registration order.
    void emit(const std::string &name, Value::Array args)
    {
        event(name, args);
        if (name == "stream-open") {
            const auto hash = args[0].asString();
            const auto idx = static_cast<std::size_t>(args[1].asNumber());
            onStreamOpen(hash, idx);
            streams->increment(hash, idx);
            engines->increment(hash, idx);
        } else if (name == "stream-close") {
            const auto hash = args[0].asString();
            const auto idx = static_cast<std::size_t>(args[1].asNumber());
            streams->decrement(hash, idx);
            engines->decrement(hash, idx);
        } else if (name == "stream-created") {
            idle->increment(args[0].asString(), static_cast<std::size_t>(args[1].asNumber()));
        } else if (name == "stream-cached") {
            idle->decrement(args[0].asString(), static_cast<std::size_t>(args[1].asNumber()));
        }
    }

    // M172 Emit(args): the event, then the colon-joined name.
    void emitJoined(const std::string &name, const std::string &hash,
                    std::optional<std::size_t> idx)
    {
        Value::Array args{Value::string(hash)};
        std::string joined = name + ":" + hash;
        if (idx) {
            args.push_back(Value::number(static_cast<double>(*idx)));
            joined += ":" + indexText(*idx);
        }
        emit(name, std::move(args));
        emit(joined, {});
    }

    bool exists(const std::string &hash) const { return catalog.exists(lowercase(hash)); }

    // M172 L18349-18373: once per file per engine, stream-created, the progress
    // listener and the whole-file selection. Runs when the engine is ready.
    void onStreamOpen(const std::string &hash, std::size_t idx)
    {
        const auto key = lowercase(hash);
        auto *engine = catalog.engine(key);
        if (!engine)
            return;
        const auto instance = engine->instance();
        std::weak_ptr<Impl> weak = weak_from_this();
        engine->ready([weak, key, hash, idx, instance]() {
            if (const auto self = weak.lock(); self && !self->destroyed)
                self->onReady(key, hash, idx, instance);
        });
    }

    void onReady(const std::string &key, const std::string &hash, std::size_t idx,
                 std::uint64_t instance)
    {
        auto *engine = catalog.engine(key);
        if (!engine || engine->instance() != instance)
            return;
        const auto *torrent = engine->torrent();
        if (!torrent || idx >= torrent->files.size())
            return;
        auto &flags = cacheEvents[key];
        if (flags.first != instance)
            flags = {instance, {}};
        if (!flags.second.insert(idx).second)
            return;
        const auto &file = torrent->files[idx];
        const auto fileObject = fileValue(file);
        emit("stream-created", {Value::string(hash), Value::number(static_cast<double>(idx)),
                                fileObject});

        const double pieceLength = static_cast<double>(torrent->pieceLength);
        const double offset = static_cast<double>(file.offset);
        const double length = static_cast<double>(file.length);
        Tracker tracker;
        tracker.serial = ++trackerSerial;
        tracker.hash = hash;
        tracker.idx = idx;
        tracker.file = fileObject;
        for (auto piece = toInt32(offset / pieceLength);
             piece <= toInt32((offset + length - 1) / pieceLength); ++piece)
            if (!engine->havePiece(piece))
                tracker.missing.push_back(piece);
        tracker.filePieces = std::ceil(length / pieceLength);
        const auto serial = tracker.serial;
        trackers[key].push_back(std::move(tracker));
        download(key, serial, std::nullopt);

        double vLen = pieceLength;
        if (torrent->realPieceLength && *torrent->realPieceLength != 0)
            vLen = static_cast<double>(*torrent->realPieceLength);
        else if (torrent->verificationLen && *torrent->verificationLen != 0)
            vLen = static_cast<double>(*torrent->verificationLen);
        const double ratio = vLen / pieceLength;
        const auto startPiece = toInt32(offset / vLen);
        const auto endPiece = toInt32((offset + length - 1) / vLen);
        if (!engine->buffer())
            engine->select(static_cast<double>(startPiece) * ratio,
                           static_cast<double>(endPiece + 1) * ratio);
    }

    // onDownload(p) for one tracker.
    void download(const std::string &key, std::uint64_t serial, std::optional<std::int64_t> piece)
    {
        auto &list = trackers[key];
        const auto found = std::find_if(list.begin(), list.end(),
                                        [serial](const Tracker &t) { return t.serial == serial; });
        if (found == list.end())
            return;
        if (piece) {
            const auto missing = std::find(found->missing.begin(), found->missing.end(), *piece);
            if (missing == found->missing.end())
                return;
            found->missing.erase(missing);
        }
        const auto idx = found->idx;
        const auto hash = found->hash;
        const auto file = found->file;
        const double progress =
            (found->filePieces - static_cast<double>(found->missing.size())) / found->filePieces;
        const bool complete = found->missing.empty();
        emit("stream-progress:" + hash + ":" + indexText(idx),
             {Value::number(progress), Value::missing()});
        if (!complete)
            return;
        Value path = Value::missing();
        if (auto *engine = catalog.engine(key)) {
            if (const auto dest = engine->storeDest(idx))
                path = Value::string(*dest);
        }
        auto &current = trackers[key];
        current.erase(std::remove_if(current.begin(), current.end(),
                                     [serial](const Tracker &t) { return t.serial == serial; }),
                      current.end());
        emit("stream-cached:" + hash + ":" + indexText(idx), {path, file});
        emit("stream-cached", {Value::string(hash), Value::number(static_cast<double>(idx)), path,
                               file});
    }

    void verify(const std::string &hash, std::int64_t piece)
    {
        const auto key = lowercase(hash);
        const auto found = trackers.find(key);
        if (found == trackers.end())
            return;
        std::vector<std::uint64_t> serials;
        for (const auto &tracker : found->second)
            serials.push_back(tracker.serial);
        for (const auto serial : serials)
            download(key, serial, piece);
    }

    void forget(const std::string &hash)
    {
        const auto key = lowercase(hash);
        trackers.erase(key);
        cacheEvents.erase(key);
    }

    void remove(const std::string &hash, std::function<void()> done)
    {
        const auto key = lowercase(hash);
        if (!catalog.exists(key)) {
            if (done)
                done();
            return;
        }
        std::weak_ptr<Impl> weak = weak_from_this();
        catalog.destroy(key, [weak, hash, key, done = std::move(done)]() {
            const auto self = weak.lock();
            if (self && !self->destroyed) {
                self->emitJoined("engine-destroyed", hash, std::nullopt);
                self->forget(key);
            }
            if (done)
                done();
        });
    }

    void resolve(std::function<void()> done)
    {
        if (!done)
            return;
        if (config.nextTurn && config.nextTurn(done))
            return;
        done();
    }
};

EngineLifecycle::EngineLifecycle(EngineCatalog &catalog, EngineLifecycleConfig config)
    : impl_(std::make_shared<Impl>(catalog, std::move(config)))
{
    auto *impl = impl_.get();
    const auto timeout = impl->config.setTimeout;
    impl->streams = std::make_unique<EngineCounter>(
        [](const std::string &hash, std::size_t idx) { return hash + ":" + indexText(idx); },
        [impl](const std::string &hash, std::size_t idx) {
            impl->emitJoined("stream-active", hash, idx);
        },
        [impl](const std::string &hash, std::size_t idx) {
            if (impl->exists(hash))
                impl->emitJoined("stream-inactive", hash, idx);
        },
        [impl]() { return impl->streamTimeout; }, timeout);
    impl->engines = std::make_unique<EngineCounter>(
        [](const std::string &hash, std::size_t) { return hash; },
        [impl](const std::string &hash, std::size_t) {
            impl->emitJoined("engine-active", hash, std::nullopt);
        },
        [impl](const std::string &hash, std::size_t) {
            if (impl->exists(hash))
                impl->emitJoined("engine-inactive", hash, std::nullopt);
        },
        [impl]() { return impl->engineTimeout; }, timeout);
    impl->idle = std::make_unique<EngineCounter>(
        [](const std::string &hash, std::size_t) { return hash; }, EngineCounter::Hook{},
        [impl](const std::string &hash, std::size_t) {
            if (impl->exists(hash))
                impl->emitJoined("engine-idle", hash, std::nullopt);
        },
        [impl]() { return impl->streamTimeout; }, timeout);
}

EngineLifecycle::~EngineLifecycle()
{
    // Teardown retires every owned timer and listener; nothing fires afterwards.
    impl_->destroyed = true;
    impl_->streams->cancelAll();
    impl_->engines->cancelAll();
    impl_->idle->cancelAll();
    impl_->trackers.clear();
}

void EngineLifecycle::setStreamTimeoutMs(std::uint64_t value) noexcept { impl_->streamTimeout = value; }
void EngineLifecycle::setEngineTimeoutMs(std::uint64_t value) noexcept { impl_->engineTimeout = value; }
std::uint64_t EngineLifecycle::streamTimeoutMs() const noexcept { return impl_->streamTimeout; }
std::uint64_t EngineLifecycle::engineTimeoutMs() const noexcept { return impl_->engineTimeout; }

LifecycleStream EngineLifecycle::openStream(const std::string &hash, std::size_t idx)
{
    LifecycleStream stream;
    if (!impl_->exists(hash)) {
        // M172's stream-open handler dereferences getEngine(hash) and throws
        // before the counters run; the route then never installs its close
        // guard. The event is observable, nothing else happens.
        impl_->event("stream-open", {Value::string(hash), Value::number(static_cast<double>(idx))});
        ++impl_->failedOpens;
        return stream;
    }
    impl_->emit("stream-open", {Value::string(hash), Value::number(static_cast<double>(idx))});
    std::weak_ptr<Impl> weak = impl_;
    stream.closed_ = false;
    stream.emitClose_ = [weak, hash, idx]() {
        if (const auto self = weak.lock(); self && !self->destroyed)
            self->emit("stream-close", {Value::string(hash), Value::number(static_cast<double>(idx))});
    };
    return stream;
}

void EngineLifecycle::verify(const std::string &hash, std::int64_t piece)
{
    impl_->verify(hash, piece);
}

void EngineLifecycle::forget(const std::string &hash)
{
    impl_->forget(hash);
}

void EngineLifecycle::remove(const std::string &hash, std::function<void()> done)
{
    impl_->remove(hash, std::move(done));
}

void EngineLifecycle::removeAll()
{
    for (const auto &hash : impl_->catalog.list())
        impl_->remove(hash, {});
}

void EngineLifecycle::keepConcurrency(const std::string &hash, const Value &concurrency,
                                      std::function<void()> done)
{
    const auto listed = impl_->catalog.list();
    const double enginesCount = static_cast<double>(listed.size()) + 1.0;
    const double limit = jsNumber(concurrency);
    if (!jsTruthy(concurrency) || enginesCount <= limit) {
        impl_->resolve(std::move(done));
        return;
    }
    const auto self = lowercase(hash);
    std::vector<std::string> candidates;
    for (const auto &ih : listed)
        if (lowercase(ih) != self && impl_->catalog.selectionCount(ih) == 0)
            candidates.push_back(ih);
    // Array.prototype.slice(0, enginesCount - concurrency).
    double end = enginesCount - limit;
    const double size = static_cast<double>(candidates.size());
    if (std::isnan(end))
        end = 0.0;
    end = std::trunc(end);
    if (end < 0.0)
        end = std::max(size + end, 0.0);
    end = std::min(end, size);
    candidates.resize(static_cast<std::size_t>(end));
    if (candidates.empty()) {
        impl_->resolve(std::move(done));
        return;
    }
    auto shared = std::make_shared<std::function<void()>>(std::move(done));
    std::weak_ptr<Impl> weak = impl_;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const bool last = i + 1 == candidates.size();
        impl_->remove(candidates[i], [weak, shared, last]() {
            if (!last)
                return;
            if (const auto self = weak.lock())
                self->resolve(*shared);
            else if (*shared)
                (*shared)();
        });
    }
}

EngineCounter::Slot EngineLifecycle::streamSlot(const std::string &hash, std::size_t idx) const
{
    return impl_->streams->slot(hash + ":" + indexText(idx));
}

EngineCounter::Slot EngineLifecycle::engineSlot(const std::string &hash) const
{
    return impl_->engines->slot(hash);
}

EngineCounter::Slot EngineLifecycle::idleSlot(const std::string &hash) const
{
    return impl_->idle->slot(hash);
}

std::size_t EngineLifecycle::pendingCacheTrackers() const noexcept
{
    std::size_t count = 0;
    for (const auto &[key, list] : impl_->trackers) {
        static_cast<void>(key);
        count += list.size();
    }
    return count;
}

bool EngineLifecycle::cacheEventsEmitted(const std::string &hash, std::size_t idx) const
{
    const auto found = impl_->cacheEvents.find(lowercase(hash));
    return found != impl_->cacheEvents.end() && found->second.second.count(idx) != 0;
}

} // namespace server1::policy
