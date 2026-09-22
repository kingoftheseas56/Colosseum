#include "server1/policy/EngineRegistry.h"

#include "server1/discovery/PeerSearch.h"
#include "server1/policy/CircularPieceStore.h"
#include "server1/policy/PieceBuffer.h"
#include "server1/policy/PieceStore.h"
#include "server1/policy/Scheduler.h"
#include "server1/policy/SchedulerActions.h"
#include "server1/policy/SwarmCaps.h"
#include "server1/policy/SwarmPolicy.h"
#include "server1/policy/TorrentMetadata.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace server1::policy {
namespace {

// M814 starts its repeating rechoke interval in ontorrent with
// setInterval(..., 1e4) and clears it in destroy.
constexpr std::uint64_t kRechokeIntervalMs = 10000;
// Engine-allocated handles for discovered peers stay clear of caller-owned
// connectSourcePeer handles.
constexpr ports::PeerHandle kFirstDiscoveredPeer = ports::PeerHandle{1} << 32U;

class DefaultRepeatTimer final : public EngineTimer {
public:
    DefaultRepeatTimer(std::uint64_t intervalMs, EngineContinuation callback)
        : id_(nextId_.fetch_add(1, std::memory_order_relaxed))
        , interval_(std::max<std::uint64_t>(1, intervalMs))
        , callback_(std::move(callback))
        , thread_([this] { run(); })
    {}

    ~DefaultRepeatTimer() override { cancel(); }
    void cancel() noexcept override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            active_ = false;
        }
        condition_.notify_all();
        if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id())
            thread_.join();
    }
    bool active() const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }
    std::uint64_t id() const noexcept override { return id_; }

private:
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while (active_) {
            if (condition_.wait_for(lock, std::chrono::milliseconds(interval_),
                                    [this] { return !active_; }))
                break;
            auto callback = callback_;
            lock.unlock();
            if (callback) callback();
            lock.lock();
        }
    }
    static std::atomic<std::uint64_t> nextId_;
    std::uint64_t id_;
    std::uint64_t interval_;
    EngineContinuation callback_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool active_ = true;
    std::thread thread_;
};

std::atomic<std::uint64_t> DefaultRepeatTimer::nextId_{1};

struct StageResult final {
    bool complete = false;
    bool success = false;
    bool advertise = false;
    std::size_t start = 0;
    std::size_t endExclusive = 0;
    std::string error;
    bool retryable = false;
    std::vector<std::size_t> evicted;
};

// Work-lane only. The app lane never touches a StoreBackend; it keeps a
// committed-piece mirror that changes only when a commit result returns.
class StoreBackend {
public:
    virtual ~StoreBackend() = default;
    virtual StageResult stage(std::size_t piece, ByteBuffer bytes, std::uint64_t nowMs) = 0;
    virtual std::optional<ByteBuffer> read(std::size_t piece, std::uint64_t nowMs,
                                           std::string *error) = 0;
    // Only durably committed persistent bytes may be uploaded, and only when
    // every virtual component [groupStart, groupEnd) of the real piece is committed.
    virtual std::optional<ByteBuffer> uploadRead(std::size_t piece, std::size_t groupStart,
                                                 std::size_t groupEnd) = 0;
    [[nodiscard]] virtual std::vector<std::size_t> restored(std::size_t count) const = 0;
    virtual void close() = 0;
};

class PersistentBackend final : public StoreBackend {
public:
    PersistentBackend(std::filesystem::path root, const TorrentMetadata &metadata)
        : store_(std::move(root),
                 static_cast<std::size_t>(metadata.geometry().virtualPieceLength()),
                 static_cast<std::size_t>(metadata.length()),
                 static_cast<std::size_t>(metadata.pieceLength()),
                 makeFiles(metadata.files()),
                 metadata.pieces())
    {}

    StageResult stage(std::size_t piece, ByteBuffer bytes, std::uint64_t) override
    {
        store_.stage(piece, std::move(bytes));
        auto verified = store_.verify(piece);
        if (!verified.complete && restageRestored(piece, verified.start, verified.endExclusive))
            verified = store_.verify(piece);
        StageResult result;
        result.start = verified.start;
        result.endExclusive = verified.endExclusive;
        if (!verified.complete)
            return result;
        result.complete = true;
        if (!verified.success) {
            result.error = "SHA-1 verification failed";
            result.retryable = true;
            return result;
        }
        const auto committed = store_.commit(verified.start, verified.endExclusive);
        result.success = committed.state == CommitState::Committed;
        result.advertise = result.success && !committed.noNotifyHave;
        result.error = committed.error;
        return result;
    }
    // K06 verifies only fully staged groups, and restore leaves surviving
    // components committed but unstaged. When those survivors are the only
    // missing members, stage their durable bytes so the whole real piece is
    // re-hashed and re-committed as one group. Returns whether any were staged.
    bool restageRestored(std::size_t piece, std::size_t start, std::size_t endExclusive)
    {
        std::vector<std::pair<std::size_t, ByteBuffer>> survivors;
        for (std::size_t item = start; item < endExclusive; ++item) {
            if (item == piece || store_.isAssembled(item)) continue;
            if (!store_.isCommitted(item)) return false;
            auto bytes = store_.read(item);
            if (!bytes) return false;
            survivors.emplace_back(item, std::move(*bytes));
        }
        for (auto &[item, bytes] : survivors)
            store_.stage(item, std::move(bytes));
        return !survivors.empty();
    }

    std::optional<ByteBuffer> read(std::size_t piece, std::uint64_t, std::string *error) override
    {
        if (!store_.isCommitted(piece)) {
            if (error) *error = "piece is not committed";
            return std::nullopt;
        }
        return store_.read(piece, error);
    }
    std::optional<ByteBuffer> uploadRead(std::size_t piece, std::size_t groupStart,
                                         std::size_t groupEnd) override
    {
        for (std::size_t item = groupStart; item < groupEnd; ++item)
            if (!store_.isCommitted(item)) return std::nullopt;
        return store_.isCommitted(piece) ? store_.read(piece) : std::nullopt;
    }
    std::vector<std::size_t> restored(std::size_t count) const override
    {
        std::vector<std::size_t> result;
        for (std::size_t piece = 0; piece < count; ++piece)
            if (store_.isCommitted(piece)) result.push_back(piece);
        return result;
    }
    void close() override { store_.close(); }

private:
    static std::vector<StoreFile> makeFiles(const std::vector<TorrentFile> &files)
    {
        std::vector<StoreFile> result;
        result.reserve(files.size());
        for (const auto &file : files)
            result.push_back({static_cast<std::size_t>(file.offset),
                              static_cast<std::size_t>(file.length)});
        return result;
    }

    PersistentPieceStore store_;
};

class CircularBackend final : public StoreBackend {
public:
    CircularBackend(std::filesystem::path root,
                    std::size_t sizeBytes,
                    std::size_t pieceLength,
                    TorrentMetadata metadata)
        : store_(std::move(root), CircularStoreMode::Memory,
                 std::max(sizeBytes, pieceLength), pieceLength)
        , metadata_(std::move(metadata))
    {}

    StageResult stage(std::size_t piece, ByteBuffer bytes, std::uint64_t nowMs) override
    {
        StageResult result;
        const auto written = store_.write(piece, std::move(bytes), {}, {}, nowMs);
        if (!written.success) {
            result.complete = true;
            result.start = piece;
            result.endExclusive = piece + 1;
            result.error = written.error;
            return result;
        }
        if (written.resetPiece) {
            committed_.erase(*written.resetPiece);
            staged_.erase(*written.resetPiece);
            result.evicted.push_back(*written.resetPiece);
        }
        const auto &coordinate = metadata_.geometry().virtualPieces().at(piece);
        const auto verification = coordinate.verificationIndex.value;
        const auto &verificationCoordinate =
            metadata_.geometry().verificationPieces().at(verification);
        const auto virtualLength = metadata_.geometry().virtualPieceLength();
        result.start = static_cast<std::size_t>(verificationCoordinate.offset / virtualLength);
        result.endExclusive = static_cast<std::size_t>(
            (verificationCoordinate.offset + verificationCoordinate.length + virtualLength - 1)
            / virtualLength);
        staged_.insert(piece);
        for (std::size_t item = result.start; item < result.endExclusive; ++item)
            if (staged_.count(item) == 0)
                return result;
        result.complete = true;
        const auto committed = store_.commit(result.start, result.endExclusive - 1,
                                             metadata_.pieces().at(verification));
        if (!committed.success) {
            for (std::size_t item = result.start; item < result.endExclusive; ++item) {
                staged_.erase(item);
                committed_.erase(item);
            }
            result.error = committed.error;
            result.retryable = true;
            return result;
        }
        for (std::size_t item = result.start; item < result.endExclusive; ++item) {
            committed_.insert(item);
            staged_.erase(item);
        }
        result.success = true;
        return result;
    }
    std::optional<ByteBuffer> read(std::size_t piece, std::uint64_t nowMs,
                                   std::string *error) override
    {
        auto bytes = committed_.count(piece) ? store_.read(piece, nowMs) : std::nullopt;
        if (!bytes && error) *error = "piece is unavailable";
        return bytes;
    }
    std::optional<ByteBuffer> uploadRead(std::size_t, std::size_t, std::size_t) override
    {
        return std::nullopt;
    }
    std::vector<std::size_t> restored(std::size_t) const override { return {}; }
    void close() override { store_.close(); }

private:
    CircularPieceStore store_;
    TorrentMetadata metadata_;
    std::set<std::size_t> staged_;
    std::set<std::size_t> committed_;
};

std::size_t optionSize(const Value &options, std::string_view key, std::size_t fallback)
{
    const auto *value = options.find(key);
    if (!value)
        return fallback;
    return checkedSize(*value, std::numeric_limits<std::size_t>::max()).value_or(fallback);
}

std::optional<double> optionNumber(const Value &object, std::string_view key)
{
    const auto *value = object.find(key);
    if (!value || value->kind() != Value::Kind::Number) return std::nullopt;
    return value->asNumber();
}

std::vector<std::string> configuredPeerSources(const Value &options)
{
    const auto *peerSearch = options.find("peerSearch");
    if (!peerSearch || !jsTruthy(*peerSearch) || peerSearch->kind() != Value::Kind::Object)
        return {};
    const auto *sources = peerSearch->find("sources");
    if (!sources || sources->kind() != Value::Kind::Array)
        return {};
    std::vector<std::string> result;
    for (const auto &source : sources->asArray())
        if (source.kind() == Value::Kind::String)
            result.push_back(source.asString());
    return result;
}

std::optional<std::size_t> configuredBound(const Value &options, std::string_view key)
{
    const auto *peerSearch = options.find("peerSearch");
    if (!peerSearch || peerSearch->kind() != Value::Kind::Object)
        return std::nullopt;
    const auto *value = peerSearch->find(key);
    if (!value)
        return std::nullopt;
    return checkedSize(*value, std::numeric_limits<std::size_t>::max());
}

// M814: rechokeSlots = uploads === false || uploads === 0 ? 0 : +uploads || 5.
std::size_t uploadSlots(const Value &options)
{
    const auto *uploads = options.find("uploads");
    if (!uploads)
        return 5;
    if (uploads->kind() == Value::Kind::Boolean && !uploads->asBoolean())
        return 0;
    if (uploads->kind() == Value::Kind::Number && uploads->asNumber() == 0)
        return 0;
    const auto number = jsNumber(*uploads);
    if (std::isnan(number) || number == 0)
        return 5;
    if (number < 0)
        return 0;
    if (number > 1024)
        return 1024;
    return static_cast<std::size_t>(std::ceil(number));
}

std::string hashHex(const ports::V1InfoHash &hash)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(40);
    for (const auto byte : hash) {
        result.push_back(digits[(byte >> 4U) & 0x0fU]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

bool validIpv4(const std::string &host)
{
    std::size_t octets = 0;
    std::size_t position = 0;
    while (position <= host.size()) {
        const auto dot = host.find('.', position);
        const auto part = host.substr(position, dot == std::string::npos ? std::string::npos
                                                                          : dot - position);
        if (part.empty() || part.size() > 3
            || !std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; })
            || std::stoi(part) > 255)
            return false;
        ++octets;
        if (dot == std::string::npos) break;
        position = dot + 1;
    }
    return octets == 4;
}

bool validIpv6(const std::string &host)
{
    return !host.empty() && host.size() <= 45
        && std::count(host.begin(), host.end(), ':') >= 2
        && std::all_of(host.begin(), host.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                   || (c >= 'A' && c <= 'F') || c == ':' || c == '.';
           });
}

// M612 sources emit "host:port" (IPv6 hosts may be bracketed). Anything that is
// not a literal address with a port in 1..65535 is rejected before it can reach
// the transport.
std::optional<std::pair<std::string, std::uint16_t>> parsePeerAddress(const std::string &address)
{
    std::string host;
    std::string port;
    if (!address.empty() && address.front() == '[') {
        const auto close = address.find(']');
        if (close == std::string::npos || close + 1 >= address.size() || address[close + 1] != ':')
            return std::nullopt;
        host = address.substr(1, close - 1);
        port = address.substr(close + 2);
        if (!validIpv6(host)) return std::nullopt;
    } else {
        const auto colon = address.rfind(':');
        if (colon == std::string::npos) return std::nullopt;
        host = address.substr(0, colon);
        port = address.substr(colon + 1);
        if (!validIpv4(host) && !validIpv6(host)) return std::nullopt;
    }
    if (port.empty() || port.size() > 5
        || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return std::nullopt;
    const auto number = std::stoul(port);
    if (number == 0 || number > 65535) return std::nullopt;
    return std::make_pair(host, static_cast<std::uint16_t>(number));
}

// Work-lane state. Only tasks on the engine's serialized work lane touch the
// transport handle and store here; the app lane reads only `closing`.
struct WorkState final {
    std::shared_ptr<ports::TorrentTransport> transport;
    std::unique_ptr<StoreBackend> store;
    std::atomic_bool closing{false};
};

struct TraceSink final {
    EngineTraceCallback callback;
    std::string sourceKey;
    ports::EngineGeneration generation = 0;

    void operator()(std::string kind, std::uint64_t piece = 0, std::uint64_t start = 0,
                    std::uint64_t end = 0, std::uint64_t length = 0) const
    {
        if (callback)
            callback({sourceKey, generation, std::move(kind), piece, start, end, length,
                      std::this_thread::get_id()});
    }
};

} // namespace

struct TorrentEngine::Impl final : std::enable_shared_from_this<TorrentEngine::Impl> {
    class ReaderSource final : public FileReaderSource {
    public:
        explicit ReaderSource(Impl &owner) : owner_(owner) {}
        bool hasPiece(std::size_t piece) const override
        {
            return owner_.committed.count(piece) != 0;
        }
        void readPiece(std::uint64_t token, std::size_t piece, Completion completion) override
        {
            owner_.postRead(token, piece, std::move(completion));
        }
        bool cancelRead(std::uint64_t) override { return false; }

    private:
        Impl &owner_;
    };

    std::string sourceKey;
    ports::EngineGeneration generation = 0;
    Value options = Value::object({});
    Value creationOptions = Value::object({});
    std::filesystem::path cachePath;
    ports::V1InfoHash infoHash{};
    ports::TorrentSource source;
    EngineTransportFactory transportFactory;
    EnginePost workPost;
    EnginePost appPost;
    EngineRepeat repeat;
    EngineClock clock;
    TraceSink trace;
    std::function<void()> onReady;
    std::function<void()> onFailed;
    std::shared_ptr<WorkState> work = std::make_shared<WorkState>();

    std::shared_ptr<ports::TorrentTransport> transport;
    std::optional<TorrentMetadata> metadata;
    std::unique_ptr<Scheduler> scheduler;
    std::unique_ptr<SchedulerActionContract> schedulerActions;
    std::unique_ptr<ReaderSource> readerSource;
    std::unique_ptr<discovery::PeerSearch> peerSearch;
    std::unique_ptr<SwarmPolicy> rechoke;
    std::optional<SwarmCapOptions> swarmCap;
    std::shared_ptr<EngineTimer> timer;
    std::vector<std::weak_ptr<FileReader>> readers;
    std::map<std::size_t, std::unique_ptr<PieceBuffer>> pieceBuffers;
    std::map<std::uint64_t, RequestIdentity> activeRequests;
    std::map<ports::PeerHandle, std::vector<bool>> available;
    std::map<ports::PeerHandle, std::vector<std::uint32_t>> advertisedByPeer;
    std::map<ports::PeerHandle, ports::PeerObservation> peers;
    std::map<ports::PeerHandle, std::uint64_t> peerDownloaded;
    std::set<std::size_t> demandedPieces;
    std::set<std::size_t> committed;
    std::set<std::size_t> staging;
    std::set<std::size_t> advertisedVerificationPieces;
    std::map<std::size_t, SelectionId> groupSelections;
    std::set<ports::PeerHandle> interestedPeers;
    std::vector<ports::TorrentObservation> deferred;
    std::vector<ports::ConnectAction> pendingConnects;
    std::map<std::string, ports::PeerHandle> discoveredAddresses;
    std::set<ports::PeerHandle> queuedPeers;
    ports::PeerHandle nextPeerHandle = kFirstDiscoveredPeer;
    EnginePeerDiscovery discovery;
    std::optional<bool> submittedPause;
    bool swarmPaused = false;
    std::string sourceError;
    std::size_t resumeCount = 0;
    std::size_t slots = 5;
    bool metadataPending = false;
    bool ready = false;
    bool failed = false;
    bool closed = false;
    bool circular = false;

    [[nodiscard]] std::uint64_t now() const { return clock(); }

    void post(EngineContinuation task)
    {
        if (workPost) static_cast<void>(workPost(std::move(task)));
    }

    // Returns a task that re-enters this engine on the app lane.
    template <typename Function>
    EngineContinuation onApp(Function function)
    {
        std::weak_ptr<Impl> weak = weak_from_this();
        return [weak, function = std::move(function)]() mutable {
            if (const auto self = weak.lock()) function(*self);
        };
    }

    void toApp(EngineContinuation task) const
    {
        if (appPost) static_cast<void>(appPost(std::move(task)));
    }

    void start()
    {
        const auto *cap = creationOptions.find("swarmCap");
        if (cap && jsTruthy(*cap) && cap->kind() == Value::Kind::Object) {
            SwarmCapOptions parsed;
            parsed.maxSpeed = optionNumber(*cap, "maxSpeed");
            parsed.maxBuffer = optionNumber(*cap, "maxBuffer");
            if (const auto min = optionNumber(*cap, "minPeers"); min && *min >= 0)
                parsed.minPeers = static_cast<std::size_t>(*min);
            swarmCap = parsed;
        }
        slots = uploadSlots(creationOptions);
        const auto *peerSearchOption = creationOptions.find("peerSearch");
        if (peerSearchOption && jsTruthy(*peerSearchOption)) {
            // M172 uses the parsed torrent's announce list when the torrent option
            // carries one; magnets and bare hashes use the configured sources.
            std::vector<std::string> announce;
            if (const auto *metainfo = std::get_if<ports::MetainfoSource>(&source)) {
                if (auto parsed = TorrentMetadata::parse(metainfo->bytes, nullptr))
                    announce = parsed->announce();
            }
            peerSearch = std::make_unique<discovery::PeerSearch>(
                discovery::PeerSearch::selectSources(announce, configuredPeerSources(creationOptions),
                                                     sourceKey),
                configuredBound(creationOptions, "min"), configuredBound(creationOptions, "max"),
                now());
        }

        const ports::TorrentOpenRequest request(generation, infoHash, source, cachePath.string());
        auto state = work;
        auto factory = transportFactory;
        auto sink = trace;
        std::weak_ptr<Impl> weak = weak_from_this();
        auto app = appPost;
        post([state, factory, request, sink, weak, app]() {
            if (state->closing.load()) return;
            std::shared_ptr<ports::TorrentTransport> opened;
            std::string error;
            try {
                auto created = factory ? factory(request) : ports::openTorrentTransport(request);
                if (!created) {
                    error = "torrent transport could not be opened";
                } else if (!created->configureAutonomy(discovery::AutonomyPolicy{})) {
                    created->close();
                    error = "torrent transport rejected external scheduler ownership";
                } else {
                    opened = std::move(created);
                }
            } catch (const std::exception &exception) {
                error = exception.what();
            }
            sink("open");
            state->transport = opened;
            if (app) static_cast<void>(app([weak, opened, error] {
                if (const auto self = weak.lock()) self->attach(opened, error);
            }));
        });
    }

    void attach(const std::shared_ptr<ports::TorrentTransport> &opened, const std::string &error)
    {
        if (closed) return;
        if (!opened) {
            fail(error);
            return;
        }
        transport = opened;
        if (submittedPause.value_or(false) != swarmPaused)
            setSwarmPaused(swarmPaused);
        auto queued = std::move(pendingConnects);
        pendingConnects.clear();
        for (auto &action : queued)
            submitConnect(std::move(action));
    }

    void fail(const std::string &message)
    {
        if (closed || ready || failed) return;
        failed = true;
        sourceError = message.empty() ? "torrent source failed" : message;
        if (onFailed) onFailed();
    }

    void pollTransport()
    {
        if (closed || !transport) return;
        if (ready) {
            pruneCanceledRequests();
            pumpRequests();
        }
        for (auto &observation : transport->poll()) {
            if (closed) return;
            route(std::move(observation));
        }
        if (peerSearch && !closed && !peerSearch->closed()) {
            peerSearch->tick(now());
            drainPeerAdds();
        }
    }

    void route(ports::TorrentObservation observation)
    {
        if (const auto *metadataReady = std::get_if<ports::MetadataReadyObservation>(&observation)) {
            acceptMetadata(*metadataReady);
        } else if (const auto *failure = std::get_if<ports::SourceFailureObservation>(&observation)) {
            if (!ready && failure->generation == generation
                && hashHex(failure->infoHash) == sourceKey)
                fail(failure->error);
        } else if (std::holds_alternative<ports::ClosedObservation>(observation)) {
            if (!ready) fail("torrent transport closed before metadata");
        } else if (const auto *peer = std::get_if<ports::PeerObservation>(&observation)) {
            acceptPeer(*peer);
        } else if (failed) {
            return;
        } else if (!ready) {
            deferred.push_back(std::move(observation));
        } else {
            acceptRuntime(observation);
        }
    }

    void acceptMetadata(const ports::MetadataReadyObservation &observation)
    {
        if (closed || ready || failed || metadataPending
            || observation.generation != generation
            || hashHex(observation.infoHash) != sourceKey)
            return;
        metadataPending = true;
        ByteBuffer torrent{'d', '4', ':', 'i', 'n', 'f', 'o'};
        torrent.insert(torrent.end(), observation.infoSection.begin(),
                       observation.infoSection.end());
        torrent.push_back('e');
        auto state = work;
        auto sink = trace;
        auto key = sourceKey;
        auto construction = creationOptions;
        auto root = cachePath;
        std::weak_ptr<Impl> weak = weak_from_this();
        auto app = appPost;
        post([state, torrent = std::move(torrent), sink, key, construction, root, weak, app]() {
            if (state->closing.load()) return;
            const auto deliver = [&](EngineContinuation task) {
                if (app) static_cast<void>(app(std::move(task)));
            };
            std::string error;
            auto parsed = TorrentMetadata::parse(torrent, &error);
            if (!parsed || parsed->infoHash() != key) {
                const auto message = parsed ? std::string("metadata info hash mismatch") : error;
                deliver([weak, message] {
                    if (const auto self = weak.lock()) self->installFailed(message);
                });
                return;
            }
            try {
                const auto pieceLength =
                    static_cast<std::size_t>(parsed->geometry().virtualPieceLength());
                const auto *circularOption = construction.find("circularBuffer");
                const bool circular = circularOption && jsTruthy(*circularOption);
                std::unique_ptr<StoreBackend> store;
                if (circular) {
                    const auto *bufferOption = construction.find("buffer");
                    if (!bufferOption || !jsTruthy(*bufferOption))
                        throw std::runtime_error("circularBuffer can only be used with buffer");
                    store = std::make_unique<CircularBackend>(
                        root, optionSize(construction, "buffer", pieceLength * 4U), pieceLength,
                        *parsed);
                } else {
                    store = std::make_unique<PersistentBackend>(root, *parsed);
                }
                auto restored = store->restored(parsed->geometry().virtualPieces().size());
                sink("metadata-install", 0, 0, parsed->geometry().virtualPieces().size());
                sink("restore", 0, 0, 0, restored.size());
                state->store = std::move(store);
                deliver([weak, metadata = std::move(*parsed), restored = std::move(restored),
                         circular]() mutable {
                    if (const auto self = weak.lock())
                        self->install(std::move(metadata), std::move(restored), circular);
                });
            } catch (const std::exception &exception) {
                const std::string message = exception.what();
                deliver([weak, message] {
                    if (const auto self = weak.lock()) self->installFailed(message);
                });
            }
        });
    }

    void installFailed(const std::string &message)
    {
        metadataPending = false;
        fail(message);
    }

    void install(TorrentMetadata installed, std::vector<std::size_t> restored, bool isCircular)
    {
        metadataPending = false;
        if (closed || failed || ready) return;
        metadata = std::move(installed);
        const auto pieces = metadata->geometry().virtualPieces().size();
        scheduler = std::make_unique<Scheduler>(pieces);
        schedulerActions = std::make_unique<SchedulerActionContract>();
        readerSource = std::make_unique<ReaderSource>(*this);
        circular = isCircular;
        rechoke = std::make_unique<SwarmPolicy>(0, slots);
        std::weak_ptr<Impl> weak = weak_from_this();
        auto app = appPost;
        auto tick = [weak, app] {
            if (app) static_cast<void>(app([weak] {
                if (const auto self = weak.lock()) self->rechokeTick();
            }));
        };
        try {
            timer = repeat ? repeat(kRechokeIntervalMs, std::move(tick))
                           : std::make_shared<DefaultRepeatTimer>(kRechokeIntervalMs, std::move(tick));
        } catch (const std::exception &exception) {
            fail(exception.what());
            return;
        }
        if (!timer) {
            fail("engine repeat timer could not be created");
            return;
        }
        trace("timer-start", 0, 0, 0, kRechokeIntervalMs);
        ready = true;
        for (const auto &[peer, observation] : peers) {
            static_cast<void>(observation);
            rechokeUpsert(peer);
        }
        // K06 restores virtual pieces one by one. Record every restored piece
        // before any advertisement decision so a real piece is advertised only
        // when all of its virtual components survived.
        committed.insert(restored.begin(), restored.end());
        for (const auto piece : restored)
            notifyCommitted(piece, piece + 1, !circular);
        if (onReady) onReady();
        auto replay = std::move(deferred);
        deferred.clear();
        for (const auto &observation : replay) {
            if (closed) return;
            acceptRuntime(observation);
        }
        pumpRequests();
    }

    void acceptPeer(const ports::PeerObservation &observation)
    {
        const bool newWire = peers.find(observation.peer) == peers.end();
        peers[observation.peer] = observation;
        if (queuedPeers.erase(observation.peer) != 0)
            discovery.queued = queuedPeers.size();
        rechokeUpsert(observation.peer);
        if (newWire) {
            // M612's update and M172's swarm-cap updater both listen on "wire",
            // in that registration order.
            peerSearchUpdate();
            updateSwarmCap();
        }
        if (ready) pumpRequests();
    }

    void acceptRuntime(const ports::TorrentObservation &observation)
    {
        if (closed || !ready) return;
        if (const auto *availability = std::get_if<ports::AvailablePiecesObservation>(&observation)) {
            acceptAvailability(*availability);
        } else if (const auto *block = std::get_if<ports::BlockObservation>(&observation)) {
            acceptBlock(*block);
        } else if (const auto *failure = std::get_if<ports::FailureObservation>(&observation)) {
            acceptFailure(*failure);
        } else if (const auto *upload = std::get_if<ports::UploadRequestObservation>(&observation)) {
            acceptUpload(*upload);
        }
    }

    void acceptAvailability(const ports::AvailablePiecesObservation &observation)
    {
        if (observation.generation != generation) return;
        auto &snapshot = available[observation.peer];
        snapshot.assign(metadata->geometry().virtualPieces().size(), false);
        auto &advertised = advertisedByPeer[observation.peer];
        advertised.clear();
        for (const auto piece : observation.pieces) {
            if (piece >= metadata->geometry().verificationPieces().size()) continue;
            advertised.push_back(piece);
            for (const auto &coordinate : metadata->geometry().virtualPieces())
                if (coordinate.verificationIndex.value == piece)
                    snapshot[coordinate.piece.value] = true;
        }
        rechokeUpsert(observation.peer);
        pumpRequests();
    }

    // M814 checkseeder compares the wire bitfield (verification pieces) with the
    // engine piece count (virtual pieces); a virtualized torrent never marks a
    // peer as a seeder.
    [[nodiscard]] bool isSeeder(ports::PeerHandle peer) const
    {
        if (!metadata) return false;
        const auto found = advertisedByPeer.find(peer);
        const auto verificationCount = metadata->geometry().verificationPieces().size();
        return found != advertisedByPeer.end()
            && verificationCount == metadata->geometry().virtualPieces().size()
            && found->second.size() == verificationCount;
    }

    void rechokeUpsert(ports::PeerHandle peer)
    {
        if (!rechoke) return;
        const auto observed = peers.find(peer);
        if (observed == peers.end()) return;
        PeerState state;
        state.id = std::to_string(peer);
        try {
            state.amChoking = rechoke->peer(state.id).amChoking;
        } catch (const std::out_of_range &) {
            state.amChoking = true;
        }
        state.peerChoking = observed->second.choking;
        state.amInterested = observed->second.interested;
        state.isSeeder = isSeeder(peer);
        state.downloadSpeed = static_cast<std::uint64_t>(
            std::max(0.0, observed->second.downloadBytesPerSecond));
        state.uploadSpeed = static_cast<std::uint64_t>(
            std::max(0.0, observed->second.uploadBytesPerSecond));
        state.salt = peer;
        rechoke->addPeer(std::move(state));
    }

    void rechokeTick()
    {
        if (closed) {
            trace("timer-tick-ignored");
            return;
        }
        trace("timer-tick");
        if (!rechoke || !transport) return;
        for (const auto &action : rechoke->rechoke(now())) {
            const auto peer = static_cast<ports::PeerHandle>(std::stoull(action.peerId));
            static_cast<void>(transport->submit(ports::ChokeAction{peer, action.choke}));
        }
    }

    void peerSearchUpdate()
    {
        if (peerSearch && !peerSearch->closed())
            peerSearch->onSwarmState(queuedPeers.size(), swarmPaused, now());
    }

    void updateSwarmCap()
    {
        if (!swarmCap || !transport || closed) return;
        const auto stats = transport->statistics();
        std::vector<BufferSelection> selections;
        if (scheduler)
            for (const auto &selection : scheduler->selections())
                selections.push_back({selection.from, selection.offset,
                                      selection.readFrom, selection.selectTo});
        setSwarmPaused(SwarmCaps::shouldPause(stats.unchokedPeers, stats.downloadBytesPerSecond,
                                              selections, *swarmCap));
    }

    void setSwarmPaused(bool paused)
    {
        if (closed) return;
        swarmPaused = paused;
        if (transport && submittedPause.value_or(false) != paused
            && transport->submit(ports::PauseAction{generation, paused}))
            submittedPause = paused;
        // pws emits "pause"/"resume"; M612 listens on both.
        peerSearchUpdate();
    }

    void drainPeerAdds()
    {
        if (!peerSearch) return;
        for (auto &address : peerSearch->takePeerAdds()) {
            const auto parsed = parsePeerAddress(address);
            if (!parsed) {
                ++discovery.malformedRejected;
                continue;
            }
            const auto key = parsed->first + "|" + std::to_string(parsed->second);
            if (discoveredAddresses.count(key) != 0) {
                ++discovery.duplicatesSuppressed;
                continue;
            }
            const auto peer = nextPeerHandle++;
            discoveredAddresses.emplace(key, peer);
            queuedPeers.insert(peer);
            discovery.queued = queuedPeers.size();
            submitConnect(ports::ConnectAction{generation, peer, parsed->first, parsed->second});
        }
    }

    void submitConnect(ports::ConnectAction action)
    {
        if (closed) return;
        if (!transport) {
            pendingConnects.push_back(std::move(action));
            return;
        }
        ++discovery.connectsSubmitted;
        auto state = work;
        auto sink = trace;
        auto rejected = onApp([peer = action.peer](Impl &self) { self.connectRejected(peer); });
        auto app = appPost;
        // ConnectAction blocks on a native event-loop barrier, so it is peer work.
        post([state, action = std::move(action), sink, rejected, app]() {
            if (state->closing.load() || !state->transport) return;
            const bool accepted = state->transport->submit(action);
            sink("connect", action.peer, action.port, accepted ? 1 : 0);
            if (!accepted && app) static_cast<void>(app(rejected));
        });
    }

    void connectRejected(ports::PeerHandle peer)
    {
        ++discovery.connectsRejected;
        if (queuedPeers.erase(peer) != 0)
            discovery.queued = queuedPeers.size();
    }

    void processSchedulerEvents()
    {
        if (!scheduler || !transport) return;
        for (const auto &event : scheduler->takeEvents()) {
            if (event.type == SchedulerEventType::Interested) {
                for (const auto &[peer, snapshot] : available) {
                    static_cast<void>(snapshot);
                    if (interestedPeers.insert(peer).second)
                        static_cast<void>(transport->submit(ports::InterestAction{peer, true}));
                }
            } else if (event.type == SchedulerEventType::Uninterested) {
                for (const auto peer : interestedPeers)
                    static_cast<void>(transport->submit(ports::InterestAction{peer, false}));
                interestedPeers.clear();
            }
        }
    }

    [[nodiscard]] ports::BlockSpan wireBlock(const RequestIdentity &request) const
    {
        const auto &coordinate = metadata->geometry().virtualPieces().at(request.piece);
        const auto offset = coordinate.verificationOffset + request.offset;
        return {static_cast<std::uint32_t>(coordinate.verificationIndex.value),
                static_cast<std::uint32_t>(offset / ports::kWireBlockLength),
                static_cast<std::uint32_t>(offset),
                static_cast<std::uint32_t>(request.length)};
    }

    [[nodiscard]] std::optional<ports::TorrentAction>
    wireAction(const SchedulerAction &action) const
    {
        auto result = ports::toTorrentAction(action);
        if (!result) return std::nullopt;
        const auto block = wireBlock(action.request);
        if (!ports::isValidBlock(block)) return std::nullopt;
        if (auto *request = std::get_if<ports::RequestAction>(&*result))
            request->block = block;
        else if (auto *cancel = std::get_if<ports::CancelAction>(&*result))
            cancel->block = block;
        return result;
    }

    [[nodiscard]] std::size_t outstandingFor(ports::PeerHandle peer) const
    {
        return static_cast<std::size_t>(std::count_if(activeRequests.begin(), activeRequests.end(),
            [peer](const auto &entry) { return entry.second.peer == peer; }));
    }

    [[nodiscard]] std::uint64_t downloadedFrom(ports::PeerHandle peer) const
    {
        std::uint64_t result = 0;
        if (const auto found = peerDownloaded.find(peer); found != peerDownloaded.end())
            result = found->second;
        if (const auto found = peers.find(peer); found != peers.end())
            result = std::max(result, found->second.downloadedBytes);
        return result;
    }

    // M814 onupdatewire: a wire that has downloaded nothing issues one request
    // from the end of the selections; afterwards it fills its per-wire budget.
    void pumpRequests()
    {
        if (closed || !ready || !scheduler || !schedulerActions || !transport || !metadata)
            return;
        processSchedulerEvents();
        const auto unchoked = std::max<std::size_t>(1, transport->statistics().unchokedPeers);
        for (const auto &[peer, pieces] : available) {
            const auto state = peers.find(peer);
            if (state != peers.end() && state->second.choking)
                continue;
            auto eligible = pieces;
            for (std::size_t piece = 0; piece < eligible.size(); ++piece)
                eligible[piece] = eligible[piece] && demandedPieces.count(piece) != 0
                    && staging.count(piece) == 0;
            const auto peerState = state == peers.end() ? ports::PeerObservation{} : state->second;
            while (!closed) {
                const auto outstanding = outstandingFor(peer);
                const auto selected = scheduler->choosePiece(eligible, downloadedFrom(peer),
                                                             outstanding == 0);
                if (!selected) break;
                const auto &coordinate = metadata->geometry().virtualPieces().at(*selected);
                auto &buffer = pieceBuffers[*selected];
                if (!buffer)
                    buffer = std::make_unique<PieceBuffer>(
                        static_cast<std::size_t>(coordinate.length), generation);
                const auto block = buffer->reserve();
                if (block == PieceBuffer::kNoReservation) {
                    eligible[*selected] = false;
                    continue;
                }
                const auto selection = std::find_if(
                    scheduler->selections().begin(), scheduler->selections().end(),
                    [&](const auto &entry) { return *selected >= entry.from && *selected <= entry.to; });
                if (selection == scheduler->selections().end()) {
                    buffer->cancel(static_cast<std::size_t>(block));
                    break;
                }
                const NormalRequestCandidate candidate{
                    selection->id, generation, *selected, static_cast<std::size_t>(block),
                    buffer->offset(static_cast<std::size_t>(block)),
                    buffer->size(static_cast<std::size_t>(block)), true, false};
                const RequestDecisionContext context{peer, generation, unchoked, outstanding,
                                                     peerState.downloadBytesPerSecond};
                const auto actions = schedulerActions->decide(context, {candidate}, {});
                if (actions.empty()) {
                    buffer->cancel(static_cast<std::size_t>(block));
                    break;
                }
                bool submitted = false;
                for (const auto &action : actions) {
                    const auto wire = wireAction(action);
                    if (!wire || !transport->submit(*wire)) {
                        schedulerActions->finish(action.request.requestId, generation,
                                                 RequestOutcome::Failed);
                        buffer->cancel(action.request.block);
                        continue;
                    }
                    if (action.type == SchedulerActionType::Request) {
                        activeRequests.emplace(action.request.requestId, action.request);
                        submitted = true;
                    }
                }
                if (!submitted) break;
            }
        }
    }

    void pruneCanceledRequests()
    {
        if (!schedulerActions || !scheduler || !transport) return;
        for (auto current = activeRequests.begin(); current != activeRequests.end();) {
            if (scheduler->find(current->second.selectionId)) {
                ++current;
                continue;
            }
            const auto request = current->second;
            schedulerActions->finish(request.requestId, generation, RequestOutcome::Canceled);
            if (const auto wire = wireAction(SchedulerAction{
                    SchedulerActionType::Cancel, request, true}))
                static_cast<void>(transport->submit(*wire));
            const auto buffer = pieceBuffers.find(request.piece);
            if (buffer != pieceBuffers.end()) buffer->second->cancel(request.block);
            current = activeRequests.erase(current);
        }
    }

    // Virtual pieces [first, second) that make up one real verification piece.
    [[nodiscard]] std::pair<std::size_t, std::size_t> realPieceGroup(std::size_t verification) const
    {
        const auto &coordinate = metadata->geometry().verificationPieces().at(verification);
        const auto virtualLength = metadata->geometry().virtualPieceLength();
        return {static_cast<std::size_t>(coordinate.offset / virtualLength),
                static_cast<std::size_t>((coordinate.offset + coordinate.length + virtualLength - 1)
                                         / virtualLength)};
    }

    // P08-T5/K10-H: a real piece is advertised or served only when every virtual
    // component is durably committed.
    [[nodiscard]] bool realPieceCommitted(std::size_t verification) const
    {
        const auto [start, end] = realPieceGroup(verification);
        for (std::size_t piece = start; piece < end; ++piece)
            if (committed.count(piece) == 0) return false;
        return start < end;
    }

    // Committed-only visibility makes a reader depend on its whole real piece,
    // while its scheduler selection may start or end inside the group (M846
    // selects virtual pieces; M814 reads a written piece before its group
    // commits). Missing group members outside every selection get one
    // engine-owned selection, released when the real piece commits.
    void ensureGroupSelection(std::size_t verification)
    {
        if (!scheduler || groupSelections.count(verification) != 0) return;
        const auto [start, end] = realPieceGroup(verification);
        for (std::size_t piece = start; piece < end; ++piece) {
            if (committed.count(piece) != 0) continue;
            const bool covered = std::any_of(
                scheduler->selections().begin(), scheduler->selections().end(),
                [piece](const auto &selection) {
                    return piece >= selection.from + selection.offset && piece <= selection.selectTo;
                });
            if (!covered) {
                groupSelections[verification] =
                    scheduler->select(start, end - 1, Value::boolean(true));
                return;
            }
        }
    }

    void releaseGroupSelection(std::size_t verification)
    {
        const auto found = groupSelections.find(verification);
        if (found == groupSelections.end()) return;
        static_cast<void>(scheduler->deselect(found->second));
        groupSelections.erase(found);
    }

    void notifyCommitted(std::size_t start, std::size_t endExclusive, bool advertise)
    {
        for (std::size_t piece = start; piece < endExclusive; ++piece) {
            demandedPieces.erase(piece);
            scheduler->markPieceComplete(piece);
            const auto verification = metadata->geometry().virtualPieces().at(piece)
                                          .verificationIndex.value;
            if (realPieceCommitted(verification)) releaseGroupSelection(verification);
            if (advertise) {
                if (realPieceCommitted(verification)
                    && advertisedVerificationPieces.insert(verification).second)
                    static_cast<void>(transport->submit(ports::AdvertisePieceAction{
                        generation, static_cast<std::uint32_t>(verification)}));
            }
            for (auto &weakReader : readers)
                if (auto reader = weakReader.lock()) reader->notifyPiece(piece);
            if (closed) return;
        }
        scheduler->collectGarbage();
        processSchedulerEvents();
    }

    void demand(const std::shared_ptr<FileReader> &reader)
    {
        if (!reader || !metadata || closed) return;
        for (std::size_t piece = reader->startPiece(); piece <= reader->endPiece(); ++piece) {
            if (committed.count(piece) == 0) {
                const auto verification = metadata->geometry().virtualPieces().at(piece)
                                              .verificationIndex.value;
                for (const auto &coordinate : metadata->geometry().virtualPieces())
                    if (coordinate.verificationIndex.value == verification
                        && committed.count(coordinate.piece.value) == 0)
                        demandedPieces.insert(coordinate.piece.value);
                ensureGroupSelection(verification);
                break;
            }
        }
        pumpRequests();
    }

    void postRead(std::uint64_t token, std::size_t piece, FileReaderSource::Completion completion)
    {
        if (closed) return;
        auto state = work;
        auto sink = trace;
        auto clockCopy = clock;
        std::weak_ptr<Impl> weak = weak_from_this();
        auto app = appPost;
        post([state, token, piece, completion = std::move(completion), sink, clockCopy, weak,
              app]() mutable {
            if (state->closing.load() || !state->store) return;
            std::string error;
            auto bytes = state->store->read(piece, clockCopy(), &error);
            sink("read", piece, 0, 0, bytes ? bytes->size() : 0);
            if (app) static_cast<void>(app([weak, token, piece, completion = std::move(completion),
                                            bytes = std::move(bytes), error]() mutable {
                const auto self = weak.lock();
                if (!self || self->closed) return;
                completion(token, piece, bytes.value_or(ByteBuffer{}),
                           bytes ? std::string{} : error);
            }));
        });
    }

    void acceptBlock(const ports::BlockObservation &observation)
    {
        const auto found = activeRequests.find(observation.ownership.requestId);
        if (found == activeRequests.end()) return;
        const auto request = found->second;
        const auto expected = wireBlock(request);
        if (observation.ownership.generation != generation
            || observation.ownership.selectionId != request.selectionId
            || observation.peer != request.peer
            || observation.block.piece != expected.piece
            || observation.block.blockOrdinal != expected.blockOrdinal
            || observation.block.offset != expected.offset
            || observation.block.length != expected.length)
            return;
        activeRequests.erase(found);
        schedulerActions->finish(request.requestId, generation, RequestOutcome::Completed);
        peerDownloaded[observation.peer] += observation.payload.size();
        auto &buffer = pieceBuffers.at(request.piece);
        if (!buffer->set(generation, request.block, observation.payload)) {
            pumpRequests();
            return;
        }
        auto bytes = buffer->flush();
        if (!bytes) {
            pumpRequests();
            return;
        }
        staging.insert(request.piece);
        postStage(request.piece, std::move(*bytes));
        pumpRequests();
    }

    void postStage(std::size_t piece, ByteBuffer bytes)
    {
        auto state = work;
        auto sink = trace;
        auto clockCopy = clock;
        std::weak_ptr<Impl> weak = weak_from_this();
        auto app = appPost;
        post([state, piece, bytes = std::move(bytes), sink, clockCopy, weak, app]() mutable {
            if (state->closing.load() || !state->store) return;
            const auto length = bytes.size();
            const auto result = state->store->stage(piece, std::move(bytes), clockCopy());
            sink("stage", piece, 0, 0, length);
            if (!result.complete)
                sink("verify-incomplete", piece, result.start, result.endExclusive);
            else if (!result.success && result.retryable)
                sink("verify-failure", piece, result.start, result.endExclusive);
            else
                sink("verify-success", piece, result.start, result.endExclusive);
            if (result.success)
                sink("commit", piece, result.start, result.endExclusive);
            if (app) static_cast<void>(app([weak, piece, result] {
                if (const auto self = weak.lock()) self->stageResult(piece, result);
            }));
        });
    }

    void stageResult(std::size_t piece, const StageResult &result)
    {
        if (closed || !ready) return;
        for (const auto evicted : result.evicted) {
            committed.erase(evicted);
            if (evicted < metadata->geometry().virtualPieces().size())
                scheduler->resetPiece(evicted);
            pieceBuffers.erase(evicted);
        }
        if (!result.complete) {
            static_cast<void>(piece);
            pumpRequests();
            return;
        }
        for (std::size_t item = result.start; item < result.endExclusive; ++item)
            staging.erase(item);
        staging.erase(piece);
        if (!result.success) {
            // K06 resets the whole group on a failed verify, including restaged
            // survivors, so none of it stays visible and all of it is fetched again.
            for (std::size_t item = result.start; item < result.endExclusive; ++item) {
                committed.erase(item);
                demandedPieces.insert(item);
            }
            if (result.start < result.endExclusive)
                ensureGroupSelection(metadata->geometry().virtualPieces().at(result.start)
                                         .verificationIndex.value);
            for (auto current = activeRequests.begin(); current != activeRequests.end();) {
                if (current->second.piece < result.start
                    || current->second.piece >= result.endExclusive) {
                    ++current;
                    continue;
                }
                const auto request = current->second;
                if (const auto wire = wireAction(SchedulerAction{
                        SchedulerActionType::Cancel, request, true}))
                    static_cast<void>(transport->submit(*wire));
                current = activeRequests.erase(current);
            }
            const auto reset = schedulerActions->invalidateGroup(
                result.start, result.endExclusive, generation);
            for (const auto item : reset) {
                scheduler->resetPiece(item);
                pieceBuffers.erase(item);
            }
            if (!result.retryable) {
                const auto reason = result.error.empty() ? "piece store commit failed"
                                                         : result.error;
                for (auto &weakReader : readers)
                    if (auto reader = weakReader.lock()) reader->fail(reason);
                return;
            }
            pumpRequests();
            return;
        }
        for (std::size_t item = result.start; item < result.endExclusive; ++item)
            committed.insert(item);
        notifyCommitted(result.start, result.endExclusive, result.advertise);
        if (closed) return;
        // M172 binds the swarm-cap updater to the engine "download" event.
        updateSwarmCap();
        pumpRequests();
    }

    void acceptFailure(const ports::FailureObservation &observation)
    {
        const auto found = activeRequests.find(observation.ownership.requestId);
        if (found == activeRequests.end()) return;
        const auto request = found->second;
        const auto expected = wireBlock(request);
        if (observation.ownership.generation != generation
            || observation.ownership.selectionId != request.selectionId
            || observation.peer != request.peer
            || observation.block.piece != expected.piece
            || observation.block.blockOrdinal != expected.blockOrdinal
            || observation.block.offset != expected.offset
            || observation.block.length != expected.length)
            return;
        activeRequests.erase(found);
        schedulerActions->finish(request.requestId, generation, RequestOutcome::Failed);
        if (const auto buffer = pieceBuffers.find(request.piece); buffer != pieceBuffers.end())
            buffer->second->cancel(request.block);
        if (!observation.retryable) {
            const auto reason = observation.error.empty() ? "torrent request failed"
                                                           : observation.error;
            for (auto &weakReader : readers)
                if (auto reader = weakReader.lock()) reader->fail(reason);
            return;
        }
        pumpRequests();
    }

    void abortUpload(const ports::UploadRequestObservation &observation)
    {
        static_cast<void>(transport->submit(ports::UploadAbortAction{
            observation.ownership, observation.peer, observation.block}));
    }

    void acceptUpload(const ports::UploadRequestObservation &observation)
    {
        if (observation.ownership.generation != generation || !transport || !metadata)
            return;
        if (observation.block.piece >= metadata->geometry().verificationPieces().size()) {
            abortUpload(observation);
            return;
        }
        const auto &verification = metadata->geometry().verificationPieces().at(
            observation.block.piece);
        if (observation.block.offset > verification.length
            || observation.block.length > verification.length - observation.block.offset) {
            abortUpload(observation);
            return;
        }
        const auto global = verification.offset + observation.block.offset;
        const auto virtualLength = metadata->geometry().virtualPieceLength();
        const auto virtualPiece = static_cast<std::size_t>(global / virtualLength);
        const auto offset = static_cast<std::size_t>(global % virtualLength);
        const auto length = static_cast<std::size_t>(observation.block.length);
        // Circular-cache bytes and uncommitted persistent bytes are never served.
        if (circular || virtualPiece >= metadata->geometry().virtualPieces().size()
            || !realPieceCommitted(observation.block.piece)) {
            abortUpload(observation);
            return;
        }
        auto state = work;
        auto sink = trace;
        std::weak_ptr<Impl> weak = weak_from_this();
        auto app = appPost;
        const auto group = realPieceGroup(observation.block.piece);
        post([state, observation, virtualPiece, group, offset, length, sink, weak, app]() {
            if (state->closing.load() || !state->store) return;
            auto bytes = state->store->uploadRead(virtualPiece, group.first, group.second);
            sink("upload-read", virtualPiece, offset, offset + length, bytes ? bytes->size() : 0);
            if (app) static_cast<void>(app([weak, observation, offset, length,
                                            bytes = std::move(bytes)] {
                const auto self = weak.lock();
                if (!self || self->closed || !self->transport) return;
                if (!bytes || offset > bytes->size() || length > bytes->size() - offset) {
                    self->abortUpload(observation);
                    return;
                }
                ByteBuffer payload(bytes->begin() + static_cast<std::ptrdiff_t>(offset),
                                   bytes->begin() + static_cast<std::ptrdiff_t>(offset + length));
                static_cast<void>(self->transport->submit(ports::UploadResponseAction{
                    observation.ownership, observation.peer, observation.block,
                    std::move(payload)}));
            }));
        });
    }

    void resume(Value effective)
    {
        if (closed) return;
        options = std::move(effective);
        ++resumeCount;
        // M172 createEngine calls e.swarm.resume() on every create.
        setSwarmPaused(false);
    }

    void close(std::function<void()> onClosed)
    {
        if (closed) return;
        closed = true;
        work->closing.store(true);
        if (timer) {
            timer->cancel();
            trace("timer-cancel");
            timer.reset();
        }
        if (schedulerActions) {
            for (const auto &[id, request] : activeRequests) {
                static_cast<void>(id);
                schedulerActions->finish(request.requestId, generation, RequestOutcome::Canceled);
            }
        }
        activeRequests.clear();
        for (auto &weakReader : readers)
            if (auto reader = weakReader.lock()) reader->close();
        readers.clear();
        if (peerSearch) peerSearch->close();
        pendingConnects.clear();
        queuedPeers.clear();
        discovery.queued = 0;
        transport.reset();
        deferred.clear();
        auto state = work;
        auto sink = trace;
        auto app = appPost;
        // M814 destroy: swarm.destroy, clearInterval, store.close(cb).
        post([state, sink, app, onClosed = std::move(onClosed)]() {
            if (state->transport) {
                state->transport->close();
                sink("transport-close");
            }
            if (state->store) {
                state->store->close();
                sink("store-close");
            }
            state->store.reset();
            state->transport.reset();
            if (onClosed && app) static_cast<void>(app(onClosed));
        });
    }
};

TorrentEngine::TorrentEngine(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl))
{}

TorrentEngine::~TorrentEngine()
{
    if (impl_) impl_->close({});
}

std::shared_ptr<TorrentEngine> TorrentEngine::make(Wiring wiring)
{
    auto impl = std::make_shared<Impl>();
    impl->sourceKey = std::move(wiring.sourceKey);
    impl->generation = wiring.generation;
    impl->options = wiring.options;
    impl->creationOptions = std::move(wiring.options);
    impl->cachePath = std::move(wiring.cachePath);
    impl->infoHash = wiring.infoHash;
    impl->source = std::move(wiring.source);
    impl->transportFactory = std::move(wiring.transportFactory);
    impl->workPost = std::move(wiring.workPost);
    impl->appPost = std::move(wiring.appPost);
    impl->repeat = std::move(wiring.repeat);
    impl->clock = wiring.clock ? std::move(wiring.clock) : EngineClock([] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    });
    impl->trace = TraceSink{std::move(wiring.trace), impl->sourceKey, impl->generation};
    impl->onReady = std::move(wiring.onReady);
    impl->onFailed = std::move(wiring.onFailed);
    std::shared_ptr<TorrentEngine> engine(new TorrentEngine(impl));
    impl->start();
    return engine;
}

std::string TorrentEngine::sourceKey() const { return impl_->sourceKey; }
ports::EngineGeneration TorrentEngine::generation() const noexcept { return impl_->generation; }
Value TorrentEngine::options() const { return impl_->options; }
bool TorrentEngine::ready() const noexcept { return impl_->ready; }
bool TorrentEngine::failed() const noexcept { return impl_->failed; }
bool TorrentEngine::closed() const noexcept { return impl_->closed; }
std::string TorrentEngine::sourceError() const { return impl_->sourceError; }
std::size_t TorrentEngine::resumeCount() const noexcept { return impl_->resumeCount; }
std::size_t TorrentEngine::fileCount() const noexcept
{
    return impl_->metadata ? impl_->metadata->files().size() : 0;
}
std::vector<TorrentFile> TorrentEngine::files() const
{
    return impl_->metadata ? impl_->metadata->files() : std::vector<TorrentFile>{};
}
std::size_t TorrentEngine::readerCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        impl_->readers.begin(), impl_->readers.end(),
        [](const auto &reader) { return !reader.expired(); }));
}
std::size_t TorrentEngine::selectionCount() const noexcept
{
    return impl_->scheduler ? impl_->scheduler->selections().size() : 0;
}
bool TorrentEngine::hasPersistentStore() const noexcept
{
    return impl_->ready && !impl_->circular;
}
bool TorrentEngine::hasCircularStore() const noexcept
{
    return impl_->ready && impl_->circular;
}
bool TorrentEngine::hasPeerSearch() const noexcept { return !!impl_->peerSearch; }
bool TorrentEngine::peerSearchRunning() const noexcept
{
    return impl_->peerSearch && !impl_->peerSearch->closed() && impl_->peerSearch->isRunning();
}
std::vector<discovery::PeerSourceStats> TorrentEngine::peerSearchStats() const
{
    return impl_->peerSearch ? impl_->peerSearch->stats()
                             : std::vector<discovery::PeerSourceStats>{};
}
EnginePeerDiscovery TorrentEngine::peerDiscovery() const noexcept { return impl_->discovery; }
std::uint64_t TorrentEngine::timerOwner() const noexcept
{
    return impl_->timer ? impl_->timer->id() : 0;
}
ports::TransportStatistics TorrentEngine::transportStatistics() const
{
    return impl_->transport ? impl_->transport->statistics()
                            : ports::TransportStatistics{};
}

std::shared_ptr<FileReader>
TorrentEngine::createReader(std::size_t fileIndex, FileReadOptions options)
{
    if (!impl_->ready || impl_->closed || !impl_->metadata || !impl_->scheduler
        || !impl_->readerSource)
        throw std::logic_error("torrent engine is not ready");
    if (fileIndex >= impl_->metadata->files().size())
        throw std::out_of_range("torrent file index is outside metadata");
    if (options.generation == 0)
        options.generation = impl_->generation;
    std::weak_ptr<Impl> weakImpl = impl_;
    auto weakReader = std::make_shared<std::weak_ptr<FileReader>>();
    auto reader = std::make_shared<FileReader>(
        *impl_->scheduler, *impl_->readerSource, impl_->metadata->files()[fileIndex],
        static_cast<std::size_t>(impl_->metadata->geometry().virtualPieceLength()),
        std::move(options), [weakImpl, weakReader] {
            if (const auto locked = weakImpl.lock()) {
                if (const auto reader = weakReader->lock()) locked->demand(reader);
            }
        });
    *weakReader = reader;
    impl_->readers.erase(std::remove_if(impl_->readers.begin(), impl_->readers.end(),
                                        [](const auto &item) { return item.expired(); }),
                         impl_->readers.end());
    impl_->readers.push_back(reader);
    return reader;
}

bool TorrentEngine::connectSourcePeer(ports::PeerHandle peer,
                                      std::string address,
                                      std::uint16_t port)
{
    if (impl_->closed || peer == 0 || port == 0 || address.empty())
        return false;
    impl_->queuedPeers.insert(peer);
    impl_->discovery.queued = impl_->queuedPeers.size();
    impl_->submitConnect(ports::ConnectAction{impl_->generation, peer, std::move(address), port});
    return true;
}

bool TorrentEngine::discoverPeer(std::size_t sourceIndex, std::string address)
{
    if (impl_->closed || !impl_->peerSearch || impl_->peerSearch->closed()
        || sourceIndex >= impl_->peerSearch->stats().size())
        return false;
    impl_->peerSearch->emitPeer(sourceIndex, std::move(address));
    impl_->drainPeerAdds();
    return true;
}

void TorrentEngine::resume(Value options)
{
    impl_->resume(std::move(options));
}

void TorrentEngine::pollTransport()
{
    impl_->pollTransport();
}

void TorrentEngine::close(std::function<void()> onClosed)
{
    impl_->close(std::move(onClosed));
}

} // namespace server1::policy
