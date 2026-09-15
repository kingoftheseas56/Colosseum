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

struct EngineRegistry::Impl final {
    struct Completion final {
        EngineCreateCallback callback;
        bool done = false;

        void finish(EngineCreateOutcome outcome,
                    std::shared_ptr<TorrentEngine> engine = {},
                    std::string error = {})
        {
            if (done)
                return;
            done = true;
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
        : config(std::move(registryConfig)), queue(std::make_shared<Queue>())
    {}

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(queue->mutex);
            queue->accepting = false;
            queue->tasks.clear();
        }
        for (auto &[key, entry] : entries) {
            static_cast<void>(key);
            entry.engine->close();
        }
        for (auto &completion : completions)
            completion->finish(EngineCreateOutcome::Cancelled);
    }

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
            pending.completion->finish(EngineCreateOutcome::InvalidSource, {},
                                       "source key must be a 40-character v1 info hash");
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
        emit(EngineEventType::Create, key, generation, options);

        if (isNew) {
            ports::TorrentOpenRequest openRequest(
                generation, hashBytes(key), sourceFor(options), path);
            std::unique_ptr<ports::TorrentTransport> transport;
            try {
                transport = config.transportFactory
                    ? config.transportFactory(openRequest)
                    : ports::openTorrentTransport(openRequest);
            } catch (const std::exception &error) {
                pending.completion->finish(EngineCreateOutcome::SourceError, {}, error.what());
                return;
            }
            if (!transport) {
                pending.completion->finish(EngineCreateOutcome::SourceError, {},
                                           "torrent transport could not be opened");
                return;
            }
            std::shared_ptr<TorrentEngine> engine;
            try {
                engine = EngineRegistry::makeEngine(
                    key, generation, options, std::filesystem::path(path), std::move(transport));
            } catch (const std::exception &error) {
                pending.completion->finish(EngineCreateOutcome::SourceError, {}, error.what());
                return;
            }
            current = entries.emplace(key, Entry{generation, std::move(engine), {}, {}, false}).first;
            ++constructionCount;
            emit(EngineEventType::Created, key, generation);
        }

        auto &entry = current->second;
        entry.requests.push_back(pending.completion);
        entry.engine->resume(options);
        if (entry.engine->ready()) {
            enqueueReady(key, entry.generation, pending.completion);
        } else if (entry.engine->failed()) {
            const auto error = entry.engine->sourceError();
            queue->push([key, generation = entry.generation,
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
        queue->push([key, generation, completion = std::move(completion)](Impl &self) mutable {
            const auto current = self.entries.find(key);
            if (current == self.entries.end() || current->second.generation != generation
                || !current->second.engine->ready())
                return;
            self.emit(EngineEventType::ScopedReady, key, generation);
            self.emit(EngineEventType::Ready, key, generation);
            completion->finish(EngineCreateOutcome::Ready, current->second.engine);
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
        queue->push([key, generation, pending = std::move(pending), error](Impl &self) mutable {
            const auto current = self.entries.find(key);
            if (current == self.entries.end() || current->second.generation != generation
                || !current->second.engine->failed())
                return;
            self.emit(EngineEventType::ScopedError, key, generation, Value::missing(), error);
            self.emit(EngineEventType::Error, key, generation, Value::missing(), error);
            for (auto &completion : pending)
                completion->finish(EngineCreateOutcome::SourceError,
                                   current->second.engine, error);
        });
    }

    EngineRegistryConfig config;
    std::shared_ptr<Queue> queue;
    std::map<std::string, Entry> entries;
    std::vector<std::shared_ptr<Completion>> completions;
    EngineRequestToken nextToken = 1;
    ports::EngineGeneration nextGeneration = 1;
    std::size_t constructionCount = 0;
};

EngineRegistry::EngineRegistry(EngineRegistryConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{}

EngineRegistry::~EngineRegistry() = default;

EngineRequestToken EngineRegistry::create(EngineCreateRequest request,
                                          EngineCreateCallback callback)
{
    request.sourceKey = canonicalKey(std::move(request.sourceKey));
    request.options = normalizeInputOptions(std::move(request.options));
    const auto token = impl_->nextToken++;
    auto completion = std::make_shared<Impl::Completion>();
    completion->callback = std::move(callback);
    impl_->completions.push_back(completion);
    auto pending = std::make_shared<Impl::PendingCreate>(
        Impl::PendingCreate{request, token, completion});
    auto continued = std::make_shared<std::atomic_bool>(false);
    std::weak_ptr<Impl::Queue> weakQueue = impl_->queue;
    EngineContinuation continuation = [weakQueue, pending, continued]() mutable {
        if (continued->exchange(true))
            return;
        *continued = true;
        if (auto queue = weakQueue.lock()) {
            queue->push([pending](Impl &state) mutable {
                state.applyCreate(std::move(*pending));
            });
        }
    };

    if (impl_->config.beforeCreate) {
        try {
            impl_->config.beforeCreate(request, std::move(continuation));
        } catch (const std::exception &error) {
            completion->finish(EngineCreateOutcome::SourceError, {}, error.what());
        }
    } else {
        continuation();
    }
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
            impl_->queue->push([callback = std::move(callback)](Impl &) mutable { callback(false); });
        return false;
    }

    auto engine = current->second.engine;
    auto requests = std::move(current->second.requests);
    impl_->entries.erase(current);
    engine->close();
    impl_->queue->push([engine = std::move(engine), requests = std::move(requests),
                        callback = std::move(callback)](Impl &) mutable {
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
            }
        }
    }
}

void EngineRegistry::dispatch()
{
    auto tasks = impl_->queue->take();
    for (auto &task : tasks)
        task(*impl_);
}

std::size_t EngineRegistry::constructionCount() const noexcept
{
    return impl_->constructionCount;
}

} // namespace server1::policy
