#include "server1/policy/EngineRegistry.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace server1::policy {
namespace {

std::string canonicalKey(std::string key)
{
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return key;
}

bool validV1Key(const std::string &key)
{
    return key.size() == 40
        && std::all_of(key.begin(), key.end(), [](unsigned char value) {
            return (value >= '0' && value <= '9')
                || (value >= 'a' && value <= 'f');
        });
}

ports::V1InfoHash hashBytes(const std::string &key)
{
    ports::V1InfoHash result{};
    const auto nibble = [](char value) -> std::uint8_t {
        return value <= '9' ? static_cast<std::uint8_t>(value - '0')
                            : static_cast<std::uint8_t>(value - 'a' + 10);
    };
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = static_cast<std::uint8_t>(
            (nibble(key[index * 2]) << 4U) | nibble(key[index * 2 + 1]));
    return result;
}

Value normalizeInputOptions(Value options)
{
    if (options.isMissing() || options.isNull())
        options = Value::object({});
    if (options.kind() != Value::Kind::Object)
        return Value::object({});
    const auto *stream = options.find("stream");
    const auto *torrent = options.find("torrent");
    if (stream && jsTruthy(*stream) && (!torrent || !jsTruthy(*torrent)))
        options = shallowExtend(options, Value::object({{"torrent", *stream}}));
    return options;
}

Value defaultsFor(const std::string &key)
{
    return Value::object({
        {"peerSearch", Value::object({
            {"min", Value::number(40)},
            {"max", Value::number(200)},
            {"sources", Value::array({Value::string("dht:" + key)})},
        })},
        {"dht", Value::boolean(false)},
        {"tracker", Value::boolean(false)},
    });
}

std::string effectivePath(const Value &options,
                          const std::filesystem::path &cacheRoot,
                          const std::string &key)
{
    const auto *path = options.find("path");
    if (path && jsTruthy(*path) && path->kind() == Value::Kind::String)
        return path->asString();
    const auto root = cacheRoot.empty() ? std::filesystem::temp_directory_path()
                                        : cacheRoot;
    return (root / key).string();
}

ports::TorrentSource sourceFor(const Value &options)
{
    const auto *torrent = options.find("torrent");
    if (!torrent)
        return ports::InfoHashSource{};
    if (torrent->kind() == Value::Kind::String)
        return ports::MagnetSource{torrent->asString()};
    if (torrent->kind() == Value::Kind::Bytes)
        return ports::MetainfoSource{torrent->asBytes()};
    return ports::InfoHashSource{};
}

} // namespace

struct EngineRegistry::Impl final : std::enable_shared_from_this<EngineRegistry::Impl> {
    struct Completion final {
        EngineCreateCallback callback;
        std::atomic_bool done{false};

        void finish(EngineCreateOutcome outcome,
                    std::shared_ptr<TorrentEngine> engine = {},
                    std::string error = {})
        {
            if (done.exchange(true))
                return;
            if (callback)
                callback({outcome, std::move(engine), std::move(error)});
        }
    };

    struct PendingCreate final {
        EngineCreateRequest request;
        EngineRequestToken token = 0;
        std::shared_ptr<Completion> completion;
    };

    struct Entry final {
        ports::EngineGeneration generation = 0;
        std::shared_ptr<TorrentEngine> engine;
        std::vector<std::shared_ptr<Completion>> pending;
        std::vector<std::shared_ptr<Completion>> requests;
        bool terminalDelivered = false;
    };

    using Task = std::function<void(Impl &)>;

    struct Queue final {
        std::mutex mutex;
        std::deque<Task> tasks;
        bool accepting = true;

        void push(Task task)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (accepting)
                tasks.push_back(std::move(task));
        }

        std::deque<Task> take()
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::deque<Task> result;
            result.swap(tasks);
            return result;
        }
    };

    explicit Impl(EngineRegistryConfig registryConfig)
        : config(std::move(registryConfig)), workQueue(std::make_shared<Queue>()),
          callbackQueue(std::make_shared<Queue>())
    {}

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> workLock(workQueue->mutex);
            workQueue->accepting = false;
            workQueue->tasks.clear();
            std::lock_guard<std::mutex> callbackLock(callbackQueue->mutex);
            callbackQueue->accepting = false;
            callbackQueue->tasks.clear();
        }
        for (auto &[key, entry] : entries) {
            static_cast<void>(key);
            entry.engine->close();
        }
        for (auto &completion : completions)
            completion->finish(EngineCreateOutcome::Cancelled);
    }

    struct DispatchGate final {
        std::atomic_bool returned{false};
        std::atomic_bool rejected{false};
        std::atomic_bool ranInline{false};
    };

    void enqueue(const std::shared_ptr<Queue> &fallback,
                 const EnginePost &executor,
                 Task task)
    {
        if (executor) {
            auto owned = std::make_shared<Task>(task);
            auto gate = std::make_shared<DispatchGate>();
            std::weak_ptr<Impl> weakSelf = weak_from_this();
            bool accepted = false;
            try {
                accepted = executor([weakSelf, owned, gate] {
                    if (!gate->returned.load(std::memory_order_acquire)) {
                        gate->ranInline.store(true, std::memory_order_release);
                        return;
                    }
                    if (gate->rejected.load(std::memory_order_acquire)) return;
                    if (const auto self = weakSelf.lock()) (*owned)(*self);
                });
            } catch (...) {
                accepted = false;
            }
            if (!accepted || gate->ranInline.load(std::memory_order_acquire))
                gate->rejected.store(true, std::memory_order_release);
            gate->returned.store(true, std::memory_order_release);
            if (accepted && !gate->ranInline.load(std::memory_order_acquire)) return;
        }
        fallback->push(std::move(task));
    }

    void enqueueWork(Task task) { enqueue(workQueue, config.workExecutor, std::move(task)); }
    void enqueueCallback(Task task)
    { enqueue(callbackQueue, config.callbackExecutor, std::move(task)); }

    void emit(EngineEventType type,
              const std::string &key,
              ports::EngineGeneration generation,
              Value options = Value::missing(),
              std::string error = {})
    {
        if (config.onEvent)
            config.onEvent({type, key, generation, std::move(options), std::move(error)});
    }

    void applyCreate(PendingCreate pending)
    {
        const auto key = pending.request.sourceKey;
        if (!validV1Key(key)) {
            enqueueCallback([completion = pending.completion](Impl &) {
                completion->finish(EngineCreateOutcome::InvalidSource, {},
                                   "source key must be a 40-character v1 info hash");
            });
            return;
        }

        Value options = shallowExtend(defaultsFor(key), pending.request.options);
        const std::string path = effectivePath(options, config.cacheRoot, key);
        options = shallowExtend(options, Value::object({
            {"path", Value::string(path)},
            {"id", Value::string("-TS0008-native-" + std::to_string(pending.token))},
        }));

        auto current = entries.find(key);
        const bool isNew = current == entries.end();
        const auto generation = isNew ? nextGeneration++ : current->second.generation;
        enqueueCallback([key, generation, options](Impl &self) mutable {
            self.emit(EngineEventType::Create, key, generation, std::move(options));
        });

        if (isNew) {
            ports::TorrentOpenRequest openRequest(
                generation, hashBytes(key), sourceFor(options), path);
            std::unique_ptr<ports::TorrentTransport> transport;
            try {
                transport = config.transportFactory
                    ? config.transportFactory(openRequest)
                    : ports::openTorrentTransport(openRequest);
            } catch (const std::exception &error) {
                const std::string message = error.what();
                enqueueCallback([completion = pending.completion, message](Impl &) {
                    completion->finish(EngineCreateOutcome::SourceError, {}, message);
                });
                return;
            }
            if (!transport) {
                enqueueCallback([completion = pending.completion](Impl &) {
                    completion->finish(EngineCreateOutcome::SourceError, {},
                                       "torrent transport could not be opened");
                });
                return;
            }
            std::shared_ptr<TorrentEngine> engine;
            try {
                std::weak_ptr<Impl> weakSelf = weak_from_this();
                EnginePost engineWork = [weakSelf](EngineContinuation continuation) {
                    if (auto target = weakSelf.lock()) {
                        target->enqueueWork([continuation = std::move(continuation)](Impl &) mutable {
                            if (continuation) continuation();
                        });
                        return true;
                    }
                    return false;
                };
                engine = EngineRegistry::makeEngine(
                    key, generation, options, std::filesystem::path(path), std::move(transport),
                    std::move(engineWork), config.repeat, config.monotonicClock);
            } catch (const std::exception &error) {
                const std::string message = error.what();
                enqueueCallback([completion = pending.completion, message](Impl &) {
                    completion->finish(EngineCreateOutcome::SourceError, {}, message);
                });
                return;
            }
            current = entries.emplace(key, Entry{generation, std::move(engine), {}, {}, false}).first;
            ++constructionCount;
            enqueueCallback([key, generation](Impl &self) {
                const auto current = self.entries.find(key);
                if (current != self.entries.end() && current->second.generation == generation)
                    self.emit(EngineEventType::Created, key, generation);
            });
        }

        auto &entry = current->second;
        entry.requests.push_back(pending.completion);
        entry.engine->resume(options);
        if (entry.engine->ready()) {
            enqueueReady(key, entry.generation, pending.completion);
        } else if (entry.engine->failed()) {
            const auto error = entry.engine->sourceError();
            enqueueCallback([key, generation = entry.generation,
                         completion = pending.completion, error](Impl &self) mutable {
                const auto current = self.entries.find(key);
                if (current != self.entries.end() && current->second.generation == generation)
                    completion->finish(EngineCreateOutcome::SourceError,
                                       current->second.engine, std::move(error));
            });
        } else {
            entry.pending.push_back(std::move(pending.completion));
        }
    }

    void enqueueReady(const std::string &key,
                      ports::EngineGeneration generation,
                      std::shared_ptr<Completion> completion)
    {
        enqueueWork([key, generation, completion = std::move(completion)](Impl &self) mutable {
            self.enqueueCallback([key, generation, completion = std::move(completion)](
                                     Impl &callbackSelf) mutable {
                const auto current = callbackSelf.entries.find(key);
                if (current == callbackSelf.entries.end()
                    || current->second.generation != generation
                    || !current->second.engine->ready())
                    return;
                callbackSelf.emit(EngineEventType::ScopedReady, key, generation);
                auto afterScoped = callbackSelf.entries.find(key);
                if (afterScoped == callbackSelf.entries.end()
                    || afterScoped->second.generation != generation
                    || !afterScoped->second.engine->ready())
                    return;
                callbackSelf.emit(EngineEventType::Ready, key, generation);
                auto afterReady = callbackSelf.entries.find(key);
                if (afterReady == callbackSelf.entries.end()
                    || afterReady->second.generation != generation
                    || !afterReady->second.engine->ready())
                    return;
                completion->finish(EngineCreateOutcome::Ready, afterReady->second.engine);
            });
        });
    }

    void terminalFailure(const std::string &key, Entry &entry)
    {
        if (entry.terminalDelivered)
            return;
        entry.terminalDelivered = true;
        auto pending = std::move(entry.pending);
        entry.pending.clear();
        const auto generation = entry.generation;
        const auto error = entry.engine->sourceError();
        enqueueCallback([key, generation, pending = std::move(pending), error](Impl &self) mutable {
            const auto current = self.entries.find(key);
            if (current == self.entries.end() || current->second.generation != generation
                || !current->second.engine->failed())
                return;
            self.emit(EngineEventType::ScopedError, key, generation, Value::missing(), error);
            auto afterScoped = self.entries.find(key);
            if (afterScoped == self.entries.end()
                || afterScoped->second.generation != generation)
                return;
            self.emit(EngineEventType::Error, key, generation, Value::missing(), error);
            auto afterError = self.entries.find(key);
            if (afterError == self.entries.end()
                || afterError->second.generation != generation)
                return;
            for (auto &completion : pending)
                completion->finish(EngineCreateOutcome::SourceError,
                                   afterError->second.engine, error);
        });
    }

    EngineRegistryConfig config;
    std::shared_ptr<Queue> workQueue;
    std::shared_ptr<Queue> callbackQueue;
    std::map<std::string, Entry> entries;
    std::vector<std::shared_ptr<Completion>> completions;
    EngineRequestToken nextToken = 1;
    ports::EngineGeneration nextGeneration = 1;
    std::size_t constructionCount = 0;
};

EngineRegistry::EngineRegistry(EngineRegistryConfig config)
    : impl_(std::make_shared<Impl>(std::move(config)))
{}

EngineRegistry::~EngineRegistry() = default;

EngineRequestToken EngineRegistry::create(EngineCreateRequest request,
                                          EngineCreateCallback callback)
{
    struct HookGate final {
        std::mutex mutex;
        bool returned = false;
        bool used = false;
        bool cancelled = false;
    };
    request.sourceKey = canonicalKey(std::move(request.sourceKey));
    request.options = normalizeInputOptions(std::move(request.options));
    const auto token = impl_->nextToken++;
    auto completion = std::make_shared<Impl::Completion>();
    completion->callback = std::move(callback);
    impl_->completions.push_back(completion);
    auto pending = std::make_shared<Impl::PendingCreate>(
        Impl::PendingCreate{request, token, completion});
    auto gate = std::make_shared<HookGate>();
    std::weak_ptr<Impl> weakImpl = impl_;
    auto schedule = [weakImpl, pending] {
        if (auto state = weakImpl.lock())
            state->enqueueWork([pending](Impl &owner) mutable {
                owner.applyCreate(std::move(*pending));
            });
    };
    EngineContinuation continuation = [gate, schedule]() mutable {
        bool scheduleNow = false;
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            if (gate->used || gate->cancelled) return;
            gate->used = true;
            scheduleNow = gate->returned;
        }
        if (scheduleNow) schedule();
    };

    bool scheduleAfterReturn = false;
    try {
        if (impl_->config.beforeCreate)
            impl_->config.beforeCreate(request, std::move(continuation));
        else
            continuation();
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            gate->returned = true;
            scheduleAfterReturn = gate->used && !gate->cancelled;
        }
    } catch (const std::exception &error) {
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            gate->returned = true;
            gate->cancelled = true;
        }
        const std::string message = error.what();
        impl_->enqueueCallback([completion, message](Impl &) {
            completion->finish(EngineCreateOutcome::SourceError, {}, message);
        });
    }
    if (scheduleAfterReturn) schedule();
    return token;
}

std::shared_ptr<TorrentEngine> EngineRegistry::get(const std::string &sourceKey) const
{
    const auto current = impl_->entries.find(canonicalKey(sourceKey));
    return current == impl_->entries.end() ? std::shared_ptr<TorrentEngine>{}
                                           : current->second.engine;
}

bool EngineRegistry::exists(const std::string &sourceKey) const
{
    return !!get(sourceKey);
}

bool EngineRegistry::remove(const std::string &sourceKey,
                            EngineRemoveCallback callback)
{
    const auto key = canonicalKey(sourceKey);
    const auto current = impl_->entries.find(key);
    if (current == impl_->entries.end()) {
        if (callback)
            impl_->enqueueCallback([callback = std::move(callback)](Impl &) mutable {
                callback(false);
            });
        return false;
    }

    auto engine = current->second.engine;
    const auto generation = engine->generation();
    auto requests = std::move(current->second.requests);
    impl_->entries.erase(current);
    engine->close();
    impl_->enqueueCallback([key, generation, engine = std::move(engine),
                        requests = std::move(requests),
                        callback = std::move(callback)](Impl &self) mutable {
        self.emit(EngineEventType::Destroyed, key, generation);
        for (auto &completion : requests)
            completion->finish(EngineCreateOutcome::Removed, engine);
        if (callback)
            callback(true);
    });
    return true;
}

std::vector<std::string> EngineRegistry::list() const
{
    std::vector<std::string> result;
    result.reserve(impl_->entries.size());
    for (const auto &[key, entry] : impl_->entries) {
        static_cast<void>(entry);
        result.push_back(key);
    }
    return result;
}

void EngineRegistry::poll()
{
    for (auto &[key, entry] : impl_->entries) {
        for (const auto &observation : entry.engine->pollTransport()) {
            if (const auto *metadata = std::get_if<ports::MetadataReadyObservation>(&observation)) {
                std::string error;
                if (entry.engine->acceptMetadata(*metadata, &error)) {
                    auto pending = std::move(entry.pending);
                    entry.pending.clear();
                    for (auto &completion : pending)
                        impl_->enqueueReady(key, entry.generation, std::move(completion));
                } else if (entry.engine->failed()) {
                    impl_->terminalFailure(key, entry);
                }
            } else if (const auto *failure = std::get_if<ports::SourceFailureObservation>(&observation)) {
                entry.engine->acceptSourceFailure(*failure);
                if (entry.engine->failed())
                    impl_->terminalFailure(key, entry);
            } else if (std::holds_alternative<ports::ClosedObservation>(observation)
                       && !entry.engine->ready()) {
                entry.engine->acceptSourceFailure(ports::SourceFailureObservation{
                    entry.generation, hashBytes(key),
                    "torrent transport closed before metadata", false});
                if (entry.engine->failed())
                    impl_->terminalFailure(key, entry);
            } else if (const auto *available =
                           std::get_if<ports::AvailablePiecesObservation>(&observation)) {
                static_cast<void>(available);
                entry.engine->acceptRuntimeObservation(observation);
            } else if (const auto *peer = std::get_if<ports::PeerObservation>(&observation)) {
                static_cast<void>(peer);
                entry.engine->acceptRuntimeObservation(observation);
            } else if (const auto *block = std::get_if<ports::BlockObservation>(&observation)) {
                static_cast<void>(block);
                entry.engine->acceptRuntimeObservation(observation);
            } else if (std::holds_alternative<ports::FailureObservation>(observation)
                       || std::holds_alternative<ports::UploadRequestObservation>(observation)
                       || std::holds_alternative<ports::UploadCancelObservation>(observation)) {
                entry.engine->acceptRuntimeObservation(observation);
            }
        }
    }
}

void EngineRegistry::dispatch()
{
    const auto core = impl_;
    auto work = core->workQueue->take();
    for (auto &task : work) task(*core);
    auto callbacks = core->callbackQueue->take();
    for (auto &task : callbacks) task(*core);
}

std::size_t EngineRegistry::constructionCount() const noexcept
{
    return impl_->constructionCount;
}

} // namespace server1::policy
