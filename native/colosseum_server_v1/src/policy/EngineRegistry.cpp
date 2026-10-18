#include "server1/policy/EngineRegistry.h"
#include "server1/policy/EngineCounters.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cmath>
#include <set>
#include <deque>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>

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

// Posts through an executor and reports whether it took ownership of the task
// for later execution. A task the executor runs inline, before post returns on
// the posting thread, is refused (the wrapper does nothing) so the caller can
// route it elsewhere. A task started on another thread before post returns
// waits for the verdict, so a refused task is never run twice.
bool postDeferred(const EnginePost &executor, EngineContinuation task)
{
    struct Gate final {
        std::mutex mutex;
        std::condition_variable decided;
        bool returned = false;
        bool rejected = false;
        bool ranInline = false;
        std::thread::id poster = std::this_thread::get_id();
    };
    if (!executor) return false;
    auto gate = std::make_shared<Gate>();
    auto owned = std::make_shared<EngineContinuation>(std::move(task));
    bool accepted = false;
    try {
        accepted = executor([gate, owned] {
            {
                std::unique_lock<std::mutex> lock(gate->mutex);
                if (!gate->returned && std::this_thread::get_id() == gate->poster) {
                    gate->ranInline = true;
                    return;
                }
                gate->decided.wait(lock, [&] { return gate->returned; });
                if (gate->rejected) return;
            }
            if (*owned) (*owned)();
        });
    } catch (...) {
        accepted = false;
    }
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->rejected = !accepted || gate->ranInline;
        gate->returned = true;
    }
    gate->decided.notify_all();
    return accepted && !gate->ranInline;
}

// A registry-owned background thread. It is the work lane whenever the injected
// workExecutor is absent, refuses a task, runs it inline, or runs it on the app
// thread.
class Worker final {
public:
    Worker() : state_(std::make_shared<State>())
    {
        auto state = state_;
        thread_ = std::thread([state] { run(state); });
    }
    ~Worker()
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->stop = true;
        }
        state_->wake.notify_all();
        if (thread_.get_id() == std::this_thread::get_id())
            thread_.detach();
        else if (thread_.joinable())
            thread_.join();
    }
    void post(EngineContinuation task)
    {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->tasks.push_back(std::move(task));
        }
        state_->wake.notify_all();
    }

private:
    struct State final {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<EngineContinuation> tasks;
        bool stop = false;
    };
    static void run(std::shared_ptr<State> state)
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        while (true) {
            state->wake.wait(lock, [&] { return state->stop || !state->tasks.empty(); });
            if (state->tasks.empty()) {
                if (state->stop) return;
                continue;
            }
            auto task = std::move(state->tasks.front());
            state->tasks.pop_front();
            lock.unlock();
            try {
                if (task) task();
            } catch (...) {
            }
            task = nullptr;
            lock.lock();
        }
    }

    std::shared_ptr<State> state_;
    std::thread thread_;
};

struct WorkerHolder final {
    std::mutex mutex;
    std::shared_ptr<Worker> worker;
    std::shared_ptr<Worker> get()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!worker) worker = std::make_shared<Worker>();
        return worker;
    }
};

// One engine's serialized work lane. Tasks run one at a time, in post order,
// never on the app thread.
class Strand final : public std::enable_shared_from_this<Strand> {
public:
    Strand(EnginePost executor, std::shared_ptr<WorkerHolder> workers, std::thread::id appThread)
        : executor_(std::move(executor)), workers_(std::move(workers)), appThread_(appThread)
    {}

    bool post(EngineContinuation task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(std::move(task));
            if (scheduled_) return true;
            scheduled_ = true;
        }
        schedule();
        return true;
    }

private:
    void schedule()
    {
        auto self = shared_from_this();
        if (executor_ && postDeferred(executor_, [self] {
                if (std::this_thread::get_id() == self->appThread_) {
                    self->toWorker();
                    return;
                }
                self->drain();
            }))
            return;
        toWorker();
    }

    void toWorker()
    {
        auto self = shared_from_this();
        workers_->get()->post([self] { self->drain(); });
    }

    void drain()
    {
        while (true) {
            EngineContinuation task;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (tasks_.empty()) {
                    scheduled_ = false;
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            try {
                if (task) task();
            } catch (...) {
            }
        }
    }

    EnginePost executor_;
    std::shared_ptr<WorkerHolder> workers_;
    std::thread::id appThread_;
    std::mutex mutex_;
    std::deque<EngineContinuation> tasks_;
    bool scheduled_ = false;
};

// The app-lane boundary: callbackExecutor when it defers, otherwise the queue
// that dispatch() drains.
class AppLane final {
public:
    explicit AppLane(EnginePost executor) : executor_(std::move(executor)) {}

    bool post(EngineContinuation task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!open_) return false;
        }
        if (executor_) {
            auto shared = std::make_shared<EngineContinuation>(std::move(task));
            if (postDeferred(executor_, [shared] { if (*shared) (*shared)(); }))
                return true;
            task = std::move(*shared);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!open_) return false;
        queue_.push_back(std::move(task));
        return true;
    }

    // Terminal delivery during destruction goes only through the executor.
    bool postThroughExecutor(EngineContinuation task)
    {
        return postDeferred(executor_, std::move(task));
    }

    std::deque<EngineContinuation> take()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::deque<EngineContinuation> result;
        result.swap(queue_);
        return result;
    }

    void shut()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = false;
        queue_.clear();
    }

private:
    EnginePost executor_;
    std::mutex mutex_;
    std::deque<EngineContinuation> queue_;
    bool open_ = true;
};

} // namespace

struct EngineRegistry::Impl final : std::enable_shared_from_this<EngineRegistry::Impl> {
    struct Completion final {
        EngineCreateCallback callback;
        std::atomic_bool done{false};
        std::optional<EngineCreateOutcome> decided;
        std::shared_ptr<TorrentEngine> decidedEngine;
        std::string decidedError;

        void decide(EngineCreateOutcome outcome, std::shared_ptr<TorrentEngine> engine = {},
                    std::string error = {})
        {
            if (done.load() || decided) return;
            decided = outcome;
            decidedEngine = std::move(engine);
            decidedError = std::move(error);
        }
        void deliver(EngineCreateOutcome outcome, std::shared_ptr<TorrentEngine> engine = {},
                     std::string error = {})
        {
            if (done.exchange(true)) return;
            auto target = std::move(callback);
            callback = nullptr;
            decidedEngine.reset();
            if (target) target({outcome, std::move(engine), std::move(error)});
        }
        void deliverDecided()
        {
            deliver(decided.value_or(EngineCreateOutcome::Cancelled),
                    decided ? decidedEngine : std::shared_ptr<TorrentEngine>{},
                    decided ? decidedError : std::string{});
        }
        void abandon()
        {
            done.store(true);
            callback = nullptr;
            decidedEngine.reset();
        }
    };

    struct RemoveCompletion final {
        EngineRemoveCallback callback;
        std::atomic_bool done{false};
        bool result = true;

        void deliver(bool value)
        {
            if (done.exchange(true)) return;
            auto target = std::move(callback);
            callback = nullptr;
            if (target) target(value);
        }
        void abandon()
        {
            done.store(true);
            callback = nullptr;
        }
    };

    using Terminal = std::variant<std::shared_ptr<Completion>, std::shared_ptr<RemoveCompletion>>;

    struct PendingCreate final {
        EngineCreateRequest request;
        EngineRequestToken token = 0;
        std::shared_ptr<Completion> completion;
    };

    struct CacheWatch final {
        TorrentFile file;
        double denominator = 0;
        std::set<std::size_t> missing;
        std::optional<std::string> destination;
        bool initialized = false;
        bool completed = false;
    };

    struct Entry final {
        ports::EngineGeneration generation = 0;
        std::shared_ptr<TorrentEngine> engine;
        // Callbacks subscribed with M172's EngineFS.once("engine-ready:" + hash).
        std::vector<std::shared_ptr<Completion>> waiters;
        // Every completion issued for this generation, for Removed decisions.
        std::vector<std::shared_ptr<Completion>> requests;
        // One ready pair per create: M172 registers one e.ready callback each time.
        std::size_t readyRegistrations = 0;
        bool pairsScheduled = false;
        bool terminalDelivered = false;
        std::map<std::size_t,std::shared_ptr<CacheWatch>> cacheWatches;
    };

    explicit Impl(EngineRegistryConfig registryConfig)
        : config(std::move(registryConfig)),
          appThread(std::this_thread::get_id()),
          app(std::make_shared<AppLane>(config.callbackExecutor)),
          workers(std::make_shared<WorkerHolder>())
    {}

    template <typename Function>
    void postApp(Function function)
    {
        std::weak_ptr<Impl> weak = weak_from_this();
        static_cast<void>(app->post([weak, function = std::move(function)]() mutable {
            const auto self = weak.lock();
            if (self && !self->finalized) function(*self);
        }));
    }

    void emit(EngineEventType type, const std::string &key,
              ports::EngineGeneration generation, Value options = Value::missing(),
              std::string error = {}, std::optional<std::size_t> fileIndex = {})
    {
        if (config.onEvent)
            config.onEvent({type, key, generation, std::move(options), std::move(error), fileIndex});
    }

    void lifecyclePair(const CounterEvent &event, bool file, bool active)
    {
        if (shutdownRequested.load() || finalized) return;
        const auto found = entries.find(event.hash);
        if (!active && found == entries.end()) return;
        const auto generation = active ? event.generation : found->second.generation;
        const auto index = file && event.index
            ? std::optional<std::size_t>(static_cast<std::size_t>(*event.index)) : std::nullopt;
        const auto generic = file
            ? (active ? EngineEventType::StreamActive : EngineEventType::StreamInactive)
            : (active ? EngineEventType::EngineActive : EngineEventType::EngineInactive);
        const auto scoped = file
            ? (active ? EngineEventType::ScopedStreamActive : EngineEventType::ScopedStreamInactive)
            : (active ? EngineEventType::ScopedEngineActive : EngineEventType::ScopedEngineInactive);
        emit(generic, event.hash, generation, Value::missing(), {}, index);
        if (shutdownRequested.load() || finalized) return;
        emit(scoped, event.hash, generation, Value::missing(), {}, index);
    }

    void initializeStreamCounters()
    {
        if (fileCounter) return;
        if (!config.after) throw std::logic_error("stream lifecycle requires an app-lane one-shot scheduler");
        std::weak_ptr<Impl> weak = weak_from_this();
        auto make = [&](bool file) {
            EngineCounterConfig counter;
            counter.key = [file](const CounterEvent &event) {
                return file ? event.hash + ":" + std::to_string(event.index.value_or(-1)) : event.hash;
            };
            counter.onPositive = [weak, file](const CounterEvent &event) {
                if (const auto self = weak.lock()) self->lifecyclePair(event, file, true);
            };
            counter.onZero = [weak, file](const CounterEvent &event) {
                if (const auto self = weak.lock()) self->lifecyclePair(event, file, false);
            };
            counter.timeoutMs = [weak, file] {
                const auto self = weak.lock();
                return self ? (file ? self->config.streamTimeoutMs : self->config.engineTimeoutMs) : 0;
            };
            counter.after = config.after;
            return std::make_unique<EngineCounter>(std::move(counter));
        };
        auto files = make(true);
        auto engines = make(false);
        EngineCounterConfig idle;
        idle.key=[](const CounterEvent &event){return event.hash;};
        idle.onPositive=[](const CounterEvent &){};
        idle.onZero=[weak](const CounterEvent &event){
            const auto self=weak.lock();
            if(!self || self->finalized || self->shutdownRequested.load()) return;
            const auto found=self->entries.find(event.hash);
            if(found==self->entries.end()) return;
            const auto generation=found->second.generation;
            self->emit(EngineEventType::EngineIdle,event.hash,generation);
            if(self->finalized || self->shutdownRequested.load()) return;
            self->emit(EngineEventType::ScopedEngineIdle,event.hash,generation);
        };
        idle.timeoutMs=[weak]{const auto self=weak.lock();return self ? self->config.streamTimeoutMs : 0;};
        idle.after=config.after;
        auto uncached=std::make_unique<EngineCounter>(std::move(idle));
        fileCounter = std::move(files);
        engineCounter = std::move(engines);
        uncachedCounter = std::move(uncached);
    }

    bool cacheCurrent(const std::string &key,ports::EngineGeneration generation) const
    { return !shutdownRequested.load() && !finalized && current(key,generation); }

    void cacheEvent(EngineEventType type,const std::string &key,ports::EngineGeneration generation,
                    std::size_t index,Value data,std::optional<std::string> path={})
    {
        if(config.onEvent) config.onEvent({type,key,generation,Value::missing(),{},index,std::move(data),std::move(path)});
    }

    static Value cacheFileValue(const TorrentFile &file)
    {
        return Value::object({{"name",Value::string(file.name)},{"path",Value::string(file.path)},
            {"length",Value::number(static_cast<double>(file.length))},
            {"offset",Value::number(static_cast<double>(file.offset))},{"__cacheEvents",Value::boolean(true)}});
    }

    void cacheProgress(const std::string &key,ports::EngineGeneration generation,std::size_t index,
                       const std::shared_ptr<CacheWatch> &watch)
    {
        if(!cacheCurrent(key,generation) || finalized || !watch->initialized || watch->completed) return;
        const auto progress=(watch->denominator-static_cast<double>(watch->missing.size()))/watch->denominator;
        cacheEvent(EngineEventType::ScopedStreamProgress,key,generation,index,Value::number(progress));
        if(!cacheCurrent(key,generation) || finalized || watch->completed || !watch->missing.empty()) return;
        watch->completed=true;
        const auto file=cacheFileValue(watch->file);
        cacheEvent(EngineEventType::ScopedStreamCached,key,generation,index,file,watch->destination);
        if(!cacheCurrent(key,generation) || finalized) return;
        uncachedCounter->decrement({key,static_cast<std::int64_t>(index),generation});
        if(!cacheCurrent(key,generation) || finalized) return;
        cacheEvent(EngineEventType::StreamCached,key,generation,index,file,watch->destination);
    }

    void beginCacheWatch(const std::string &key,ports::EngineGeneration generation,std::size_t index)
    {
        if(!cacheCurrent(key,generation) || finalized) return;
        auto &entry=entries.at(key);
        if(entry.cacheWatches.count(index)) return;
        const auto engine=entry.engine;
        auto snapshot=engine->fileCacheSnapshot(index);
        if(!snapshot) return;
        auto watch=std::make_shared<CacheWatch>();
        watch->file=snapshot->file;
        entry.cacheWatches.emplace(index,watch); // source __cacheEvents marker precedes callbacks
        uncachedCounter->increment({key,static_cast<std::int64_t>(index),generation});
        cacheEvent(EngineEventType::StreamCreated,key,generation,index,cacheFileValue(watch->file));
        if(!cacheCurrent(key,generation) || finalized) return;
        snapshot=engine->fileCacheSnapshot(index); // observers may have advanced committed facts
        if(!snapshot) return;
        watch->destination=snapshot->destination;
        watch->denominator=std::ceil(static_cast<double>(watch->file.length)/snapshot->pieceLength);
        const auto length=static_cast<std::int64_t>(snapshot->pieceLength);
        const auto first=watch->file.offset/length;
        const auto last=(watch->file.offset+watch->file.length-1)/length;
        const std::set<std::size_t> committed(snapshot->committedPieces.begin(),snapshot->committedPieces.end());
        for(auto piece=first;piece<=last;++piece)
            if(!committed.count(static_cast<std::size_t>(piece))) watch->missing.insert(static_cast<std::size_t>(piece));
        watch->initialized=true;
        cacheProgress(key,generation,index,watch);
        if(cacheCurrent(key,generation) && !finalized) engine->selectCacheFile(index);
    }

    void verified(const std::string &key,ports::EngineGeneration generation,std::size_t piece)
    {
        if(!cacheCurrent(key,generation) || finalized) return;
        // A callback may remove the entry or open another file. Snapshot shared
        // watchers without retaining a map iterator across external callbacks.
        const auto watches=entries.at(key).cacheWatches;
        for(const auto &[index,watch]:watches) {
            if(!cacheCurrent(key,generation) || finalized) return;
            if(watch->initialized && !watch->completed && watch->missing.erase(piece))
                cacheProgress(key,generation,index,watch);
        }
    }

    void closeStream(std::uint64_t token)
    {
        const auto found = streamLeases.find(token);
        if (found == streamLeases.end() || finalized) return;
        const auto lease = found->second;
        streamLeases.erase(found); // retire before callbacks or timer factories reenter
        std::exception_ptr failure;
        try {
            if (lease.fileAcquired) fileCounter->decrement(lease.event);
        } catch (...) { failure = std::current_exception(); }
        // A rejected file timer must not leak the independent engine obligation.
        if (!shutdownRequested.load() && !finalized) {
            try {
                if (lease.engineAcquired) engineCounter->decrement(lease.event);
            } catch (...) { if (!failure) failure = std::current_exception(); }
        }
        if (failure) std::rethrow_exception(failure);
    }

    [[nodiscard]] bool current(const std::string &key, ports::EngineGeneration generation) const
    {
        const auto found = entries.find(key);
        return found != entries.end() && found->second.generation == generation;
    }

    void decideAndDeliver(const std::shared_ptr<Completion> &completion,
                          EngineCreateOutcome outcome,
                          std::shared_ptr<TorrentEngine> engine = {},
                          std::string error = {})
    {
        completion->decide(outcome, engine, error);
        postApp([completion, outcome, engine, error](Impl &) mutable {
            completion->deliver(outcome, std::move(engine), std::move(error));
        });
    }

    void applyCreate(const PendingCreate &pending)
    {
        if (shutdownRequested.load() || finalized) return;
        const auto key = pending.request.sourceKey;
        if (!validV1Key(key)) {
            decideAndDeliver(pending.completion, EngineCreateOutcome::InvalidSource, {},
                             "source key must be a 40-character v1 info hash");
            return;
        }

        // M172: util._extend(getDefaults(hash), options); path fallback; emit
        // engine-create; then options.id = spoofedPeerId().
        Value options = shallowExtend(defaultsFor(key), pending.request.options);
        const std::string path = effectivePath(options, config.cacheRoot, key);
        options = shallowExtend(options, Value::object({{"path", Value::string(path)}}));
        const Value emitted = options;
        options = shallowExtend(options, Value::object({
            {"id", Value::string("-TS0008-native-" + std::to_string(pending.token))},
        }));

        auto found = entries.find(key);
        const bool isNew = found == entries.end();
        const auto generation = isNew ? nextGeneration++ : found->second.generation;
        postApp([key, generation, emitted](Impl &self) {
            if (self.current(key, generation))
                self.emit(EngineEventType::Create, key, generation, emitted);
        });

        if (isNew) {
            std::weak_ptr<Impl> weak = weak_from_this();
            // One strand per source key: a recreated generation's disk work queues
            // behind the previous generation's store close.
            auto strand = strands[key].lock();
            if (!strand) {
                strand = std::make_shared<Strand>(config.workExecutor, workers, appThread);
                strands[key] = strand;
            }
            auto lane = app;
            TorrentEngine::Wiring wiring;
            wiring.sourceKey = key;
            wiring.generation = generation;
            wiring.options = options;
            wiring.cachePath = std::filesystem::path(path);
            wiring.infoHash = hashBytes(key);
            wiring.source = sourceFor(options);
            wiring.transportFactory = config.transportFactory;
            wiring.workPost = [strand](EngineContinuation task) { return strand->post(std::move(task)); };
            wiring.appPost = [lane](EngineContinuation task) { return lane->post(std::move(task)); };
            wiring.repeat = config.repeat;
            wiring.clock = config.monotonicClock;
            wiring.trace = config.trace;
            wiring.onReady = [weak, key, generation] {
                if (const auto self = weak.lock()) self->engineReady(key, generation);
            };
            wiring.onFailed = [weak, key, generation] {
                if (const auto self = weak.lock()) self->engineFailed(key, generation);
            };
            wiring.onVerified = [weak,key,generation](std::size_t piece){
                if(const auto self=weak.lock()) self->verified(key,generation,piece);
            };
            std::shared_ptr<TorrentEngine> engine;
            try {
                engine = TorrentEngine::make(std::move(wiring));
            } catch (const std::exception &error) {
                decideAndDeliver(pending.completion, EngineCreateOutcome::SourceError, {},
                                 error.what());
                return;
            }
            Entry entry;
            entry.generation = generation;
            entry.engine = std::move(engine);
            found = entries.emplace(key, std::move(entry)).first;
            ++constructionCount;
            postApp([key, generation](Impl &self) {
                if (self.current(key, generation))
                    self.emit(EngineEventType::Created, key, generation);
            });
        }

        auto &entry = found->second;
        entry.requests.push_back(pending.completion);
        entry.engine->resume(options);
        if (entry.engine->failed()) {
            decideAndDeliver(pending.completion, EngineCreateOutcome::SourceError, entry.engine,
                             entry.engine->sourceError());
            return;
        }
        entry.waiters.push_back(pending.completion);
        ++entry.readyRegistrations;
        if (entry.engine->ready())
            scheduleReadyPairs(key, entry);
    }

    void scheduleReadyPairs(const std::string &key, Entry &entry)
    {
        if (entry.pairsScheduled || entry.readyRegistrations == 0) return;
        entry.pairsScheduled = true;
        const auto generation = entry.generation;
        postApp([key, generation](Impl &self) { self.emitReadyPairs(key, generation); });
    }

    // M172: each create's e.ready callback emits engine-ready:<hash> and then
    // engine-ready. Create callbacks are once-listeners on the scoped event, so
    // the first scoped emission after a callback subscribes delivers it.
    void emitReadyPairs(const std::string &key, ports::EngineGeneration generation)
    {
        if (!current(key, generation)) return;
        entries.at(key).pairsScheduled = false;
        while (true) {
            auto found = entries.find(key);
            if (found == entries.end() || found->second.generation != generation) return;
            auto &entry = found->second;
            if (!entry.engine->ready() || entry.engine->closed() || entry.readyRegistrations == 0)
                return;
            --entry.readyRegistrations;
            auto waiters = std::move(entry.waiters);
            entry.waiters.clear();
            const auto engine = entry.engine;
            emit(EngineEventType::ScopedReady, key, generation);
            for (auto &completion : waiters) {
                if (!current(key, generation)) break;
                completion->deliver(EngineCreateOutcome::Ready, engine);
            }
            if (!current(key, generation)) return;
            emit(EngineEventType::Ready, key, generation);
        }
    }

    void engineReady(const std::string &key, ports::EngineGeneration generation)
    {
        const auto found = entries.find(key);
        if (found == entries.end() || found->second.generation != generation) return;
        scheduleReadyPairs(key, found->second);
    }

    void engineFailed(const std::string &key, ports::EngineGeneration generation)
    {
        const auto found = entries.find(key);
        if (found == entries.end() || found->second.generation != generation) return;
        auto &entry = found->second;
        if (entry.terminalDelivered) return;
        entry.terminalDelivered = true;
        auto waiters = std::move(entry.waiters);
        entry.waiters.clear();
        entry.readyRegistrations = 0;
        const auto engine = entry.engine;
        const auto error = engine->sourceError();
        for (auto &completion : waiters)
            completion->decide(EngineCreateOutcome::SourceError, engine, error);
        postApp([key, generation, waiters = std::move(waiters), error](Impl &self) mutable {
            if (!self.current(key, generation) || !self.entries.at(key).engine->failed()) return;
            self.emit(EngineEventType::ScopedError, key, generation, Value::missing(), error);
            if (!self.current(key, generation)) return;
            self.emit(EngineEventType::Error, key, generation, Value::missing(), error);
            if (!self.current(key, generation)) return;
            const auto engine = self.entries.at(key).engine;
            for (auto &completion : waiters)
                completion->deliver(EngineCreateOutcome::SourceError, engine, error);
        });
    }

    // Registry teardown. deliver=true runs remaining terminal completions now,
    // on the app lane, after all registry state is gone; otherwise they are
    // abandoned because the app lane is unreachable.
    void finalize(bool deliver)
    {
        if (finalized) return;
        finalized = true;
        if (fileCounter) fileCounter->close();
        if (engineCounter) engineCounter->close();
        if (uncachedCounter) uncachedCounter->close();
        streamLeases.clear();
        auto engines = std::move(entries);
        entries.clear();
        for (auto &[key, entry] : engines) {
            static_cast<void>(key);
            entry.engine->close();
        }
        engines.clear();
        app->shut();
        auto terminals = std::move(ledger);
        ledger.clear();
        for (auto &terminal : terminals) {
            if (const auto *create = std::get_if<std::shared_ptr<Completion>>(&terminal)) {
                if (deliver) (*create)->deliverDecided();
                else (*create)->abandon();
            } else {
                const auto &removal = std::get<std::shared_ptr<RemoveCompletion>>(terminal);
                if (deliver) removal->deliver(removal->result);
                else removal->abandon();
            }
        }
    }

    void pruneLedger()
    {
        if (ledger.size() < 256) return;
        ledger.erase(std::remove_if(ledger.begin(), ledger.end(), [](const Terminal &terminal) {
            return std::visit([](const auto &item) { return item->done.load(); }, terminal);
        }), ledger.end());
    }

    EngineRegistryConfig config;
    std::thread::id appThread;
    std::shared_ptr<AppLane> app;
    std::shared_ptr<WorkerHolder> workers;
    std::map<std::string, Entry> entries;
    std::map<std::string, std::weak_ptr<Strand>> strands;
    std::vector<Terminal> ledger;
    std::unique_ptr<EngineCounter> fileCounter;
    std::unique_ptr<EngineCounter> engineCounter;
    std::unique_ptr<EngineCounter> uncachedCounter;
    struct StreamObligations {
        CounterEvent event;
        bool fileAcquired = false;
        bool engineAcquired = false;
    };
    std::map<std::uint64_t, StreamObligations> streamLeases;
    std::uint64_t nextStreamLease = 1;
    EngineRequestToken nextToken = 1;
    ports::EngineGeneration nextGeneration = 1;
    std::size_t constructionCount = 0;
    std::atomic_bool shutdownRequested{false};
    bool finalized = false;
};

EngineRegistry::EngineRegistry(EngineRegistryConfig config)
    : impl_(std::make_shared<Impl>(std::move(config)))
{}

EngineRegistry::~EngineRegistry()
{
    const auto impl = impl_;
    impl->shutdownRequested.store(true);
    if (impl->config.callbackExecutor
        && impl->app->postThroughExecutor([impl] { impl->finalize(true); }))
        return;
    impl->finalize(std::this_thread::get_id() == impl->appThread);
}

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
    impl_->pruneLedger();
    impl_->ledger.emplace_back(completion);
    auto pending = std::make_shared<Impl::PendingCreate>(
        Impl::PendingCreate{request, token, completion});
    auto gate = std::make_shared<HookGate>();
    std::weak_ptr<Impl> weakImpl = impl_;
    auto schedule = [weakImpl, pending] {
        if (const auto state = weakImpl.lock())
            state->postApp([pending](Impl &owner) { owner.applyCreate(*pending); });
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
        impl_->decideAndDeliver(completion, EngineCreateOutcome::SourceError, {}, error.what());
    }
    if (scheduleAfterReturn) schedule();
    return token;
}

std::shared_ptr<TorrentEngine> EngineRegistry::get(const std::string &sourceKey) const
{
    const auto found = impl_->entries.find(canonicalKey(sourceKey));
    return found == impl_->entries.end() ? std::shared_ptr<TorrentEngine>{}
                                         : found->second.engine;
}

bool EngineRegistry::exists(const std::string &sourceKey) const
{
    return !!get(sourceKey);
}

bool EngineRegistry::remove(const std::string &sourceKey,
                            EngineRemoveCallback callback)
{
    const auto key = canonicalKey(sourceKey);
    std::shared_ptr<Impl::RemoveCompletion> removal;
    if (callback) {
        removal = std::make_shared<Impl::RemoveCompletion>();
        removal->callback = std::move(callback);
        impl_->pruneLedger();
        impl_->ledger.emplace_back(removal);
    }
    const auto found = impl_->entries.find(key);
    if (found == impl_->entries.end()) {
        if (removal) {
            removal->result = false;
            impl_->postApp([removal](Impl &) { removal->deliver(false); });
        }
        return false;
    }

    auto engine = found->second.engine;
    const auto generation = found->second.generation;
    auto requests = std::move(found->second.requests);
    for (auto &completion : requests)
        completion->decide(EngineCreateOutcome::Removed, engine);
    impl_->entries.erase(found);
    std::weak_ptr<Impl> weak = impl_;
    // M172 removeEngine: engine.destroy(cb) then engine-destroyed, delete, cb().
    engine->close([weak, key, generation, engine, requests = std::move(requests), removal]() mutable {
        if (const auto self = weak.lock(); self && !self->finalized)
            self->emit(EngineEventType::Destroyed, key, generation);
        for (auto &completion : requests)
            completion->deliver(EngineCreateOutcome::Removed, engine);
        if (removal) removal->deliver(true);
    });
    return true;
}

std::vector<std::string> EngineRegistry::list() const
{
    std::vector<std::string> result;
    result.reserve(impl_->entries.size());
    // M172 Object.keys retains creation order for the canonical 40-character
    // hash keys. Generations are assigned monotonically when an entry is made;
    // reusing an entry retains its generation, recreating it appends naturally.
    std::vector<std::pair<ports::EngineGeneration, std::string>> ordered;
    ordered.reserve(impl_->entries.size());
    for (const auto &[key, entry] : impl_->entries)
        ordered.emplace_back(entry.generation, key);
    std::sort(ordered.begin(), ordered.end());
    for (const auto &entry : ordered) result.push_back(entry.second);
    return result;
}

EngineStreamLease::~EngineStreamLease() { close(); }
void EngineStreamLease::close() {
    if (closed_.exchange(true)) return;
    if (close_) close_();
}
std::shared_ptr<EngineStreamLease> EngineRegistry::openStream(const std::string &sourceKey, std::size_t fileIndex) {
    const auto self = impl_;
    if (std::this_thread::get_id() != self->appThread)
        throw std::logic_error("openStream requires the registry app lane");
    if (self->shutdownRequested.load() || self->finalized) return {};
    const auto key = canonicalKey(sourceKey);
    const auto found = self->entries.find(key);
    if (found == self->entries.end() || !found->second.engine->ready()
        || found->second.engine->closed() || fileIndex >= found->second.engine->fileCount()) return {};
    self->initializeStreamCounters();
    const auto token = self->nextStreamLease++;
    const CounterEvent event{key, static_cast<std::int64_t>(fileIndex), found->second.generation};
    self->streamLeases.emplace(token, Impl::StreamObligations{event});
    std::weak_ptr<Impl> weak = self;
    auto lease = std::shared_ptr<EngineStreamLease>(new EngineStreamLease([weak, token] {
        if (const auto owner = weak.lock())
            owner->postApp([token](Impl &state) { state.closeStream(token); });
    }));
    self->postApp([key,generation=event.generation,fileIndex](Impl &owner){owner.beginCacheWatch(key,generation,fileIndex);});
    self->fileCounter->increment(event);
    if (self->shutdownRequested.load() || self->finalized) { lease->close(); return lease; }
    self->streamLeases.at(token).fileAcquired = true;
    self->engineCounter->increment(event);
    if (self->shutdownRequested.load() || self->finalized) { lease->close(); return lease; }
    self->streamLeases.at(token).engineAcquired = true;
    return lease;
}

void EngineRegistry::poll()
{
    std::vector<std::shared_ptr<TorrentEngine>> engines;
    engines.reserve(impl_->entries.size());
    for (const auto &[key, entry] : impl_->entries) {
        static_cast<void>(key);
        engines.push_back(entry.engine);
    }
    for (const auto &engine : engines)
        engine->pollTransport();
}

void EngineRegistry::dispatch()
{
    const auto core = impl_;
    auto tasks = core->app->take();
    std::exception_ptr failure;
    for (auto &task : tasks) {
        try { if (task) task(); }
        catch (...) { if (!failure) failure = std::current_exception(); }
    }
    // A failed callback cannot discard unrelated queued cleanup obligations.
    if (failure) std::rethrow_exception(failure);
}

std::size_t EngineRegistry::constructionCount() const noexcept
{
    return impl_->constructionCount;
}

} // namespace server1::policy
