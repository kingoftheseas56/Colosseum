#include "server1/policy/EngineRegistry.h"
#include "server1/policy/EngineStatistics.h"
#include <QDateTime>

#include "server1/discovery/PeerSearch.h"
#include "server1/policy/CircularPieceStore.h"
#include "server1/policy/PieceBuffer.h"
#include "server1/policy/PieceStore.h"
#include "server1/policy/Scheduler.h"
#include "server1/policy/SchedulerActions.h"
#include "server1/policy/SwarmCaps.h"
#include "server1/policy/SwarmPolicy.h"
#include "server1/policy/SwarmConnections.h"
#include "server1/policy/SourcePeerAddress.h"
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
    virtual std::optional<std::string> destination(std::size_t) const { return std::nullopt; }
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
    std::optional<std::string> destination(std::size_t index) const override
    { return store_.destination(index).string(); }
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

// Work-lane state. Only tasks on the engine's serialized work lane touch the
// transport handle and store here. The app lane also admits/cancels read
// reservations under readMutex; it never accesses the store.
struct WorkState final {
    std::shared_ptr<ports::TorrentTransport> transport;
    std::unique_ptr<StoreBackend> store;
    std::atomic_bool closing{false};
    // Read admission is on the app lane; store access is on the work lane.
    // Keep reservations live (not a stage-post-time snapshot) until bytes have
    // been copied. Tokens distinguish sibling readers of the same piece.
    std::mutex readMutex;
    std::set<std::uint64_t> pendingReads;
    bool circular = false; // initialized and read on the serialized work lane
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
        bool cancelRead(std::uint64_t token) override
        {
            std::lock_guard<std::mutex> lock(owner_.work->readMutex);
            return owner_.work->pendingReads.erase(token) != 0;
        }

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
    std::function<std::shared_ptr<discovery::DiscoveryNetwork>()> discoveryNetworkFactory;
    EnginePost workPost;
    EnginePost appPost;
    EngineRepeat repeat;
    EngineClock clock;
    EngineSpeedometer::Tick speedometerTick;
    std::unique_ptr<EngineSpeedometer> downloadMeter;
    struct PeerMeter final {
        std::unique_ptr<EngineSpeedometer> down, up;
        std::uint64_t downloaded=0, uploaded=0;
    };
    std::map<std::uint64_t,PeerMeter> peerMeters;
    std::map<ports::PeerHandle,PeerMeter> dialingMeters;
    std::uint64_t measuredDownloaded=0;
    std::int64_t epochOffsetMs=0;

    TraceSink trace;
    std::function<void()> onReady;
    std::function<void()> onFailed;
    std::function<void(std::size_t)> onVerified;
    std::vector<std::optional<std::string>> destinations;
    std::map<SelectionId, std::pair<std::size_t,std::size_t>> cacheSelectionBounds;
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
    std::uint64_t stageEpoch = 0;
    std::set<ports::PeerHandle> interestedPeers;
    std::vector<ports::TorrentObservation> deferred;
    std::vector<ports::ConnectAction> pendingConnects;
    SwarmConnections connections;
    std::map<ports::PeerHandle,std::uint64_t> connectionIdentities;
    std::map<ports::PeerHandle,std::uint64_t> admittedAttempts;
    std::set<ports::PeerHandle> connectionTimeouts;
    ports::PeerHandle nextPeerHandle = kFirstDiscoveredPeer;
    EnginePeerDiscovery discovery;
    std::optional<bool> submittedPause;
    bool swarmPaused = false;
    std::string sourceError;
    std::size_t resumeCount = 0;
    std::size_t uploadSlotCount = 5;
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
        uploadSlotCount = uploadSlots(creationOptions);
        auto capacity=optionNumber(creationOptions,"connections");
        if(!capacity || *capacity==0) capacity=optionNumber(creationOptions,"size");
        auto handshake=optionNumber(creationOptions,"handshakeTimeout");
        connections=SwarmConnections(capacity && *capacity!=0 ? *capacity : 100,3000,
            handshake && *handshake>0 ? static_cast<std::uint64_t>(*handshake) : 25000);
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
                now(), discovery::AutonomyPolicy{},
                discoveryNetworkFactory ? discoveryNetworkFactory() : nullptr, sourceKey);
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

    void sampleStatistics(const ports::TransportStatistics &stats)
    {
        if (downloadMeter) {
            if (stats.wirePayloadBytesReceived>=measuredDownloaded)
                downloadMeter->sample(static_cast<double>(stats.wirePayloadBytesReceived-measuredDownloaded));
            measuredDownloaded=stats.wirePayloadBytesReceived;
        }
        std::set<std::uint64_t> active;
        for(const auto &wire:stats.peerTransfers) {
            active.insert(wire.connectionIdentity);
            auto &meter=peerMeters[wire.connectionIdentity];
            if(!meter.down) {
                meter.down=std::make_unique<EngineSpeedometer>(speedometerTick);
                meter.up=std::make_unique<EngineSpeedometer>(speedometerTick);
            }
            if(wire.downloadedBytes>=meter.downloaded)
                meter.down->sample(static_cast<double>(wire.downloadedBytes-meter.downloaded));
            if(wire.uploadedBytes>=meter.uploaded)
                meter.up->sample(static_cast<double>(wire.uploadedBytes-meter.uploaded));
            meter.downloaded=wire.downloadedBytes;
            meter.uploaded=wire.uploadedBytes;
        }
        for(auto it=peerMeters.begin();it!=peerMeters.end();)
            if(!active.count(it->first) && std::none_of(connectionIdentities.begin(),connectionIdentities.end(),
                [&](const auto &entry){return entry.second==it->first;})) it=peerMeters.erase(it); else ++it;
    }

    void pollTransport()
    {
        if (closed || !transport) return;
        sampleStatistics(transport->statistics());
        if (ready) {
            pruneCanceledRequests();
            pumpRequests();
        }
        for (auto &observation : transport->poll()) {
            if (closed) return;
            route(std::move(observation));
        }
        connections.advance(now());
        drainConnections();
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
        } else if (const auto *life = std::get_if<ports::PeerLifecycleObservation>(&observation)) {
            acceptLifecycle(*life);
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
                state->circular = circular;
                auto restored = store->restored(parsed->geometry().virtualPieces().size());
                std::vector<std::optional<std::string>> destinations;
                for (std::size_t index=0; index<parsed->files().size(); ++index)
                    destinations.push_back(store->destination(index));
                sink("metadata-install", 0, 0, parsed->geometry().virtualPieces().size());
                sink("restore", 0, 0, 0, restored.size());
                state->store = std::move(store);
                deliver([weak, metadata = std::move(*parsed), restored = std::move(restored),
                         circular, destinations = std::move(destinations)]() mutable {
                    if (const auto self = weak.lock())
                        self->install(std::move(metadata), std::move(restored), circular,
                                      std::move(destinations));
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

    void install(TorrentMetadata installed, std::vector<std::size_t> restored, bool isCircular,
                 std::vector<std::optional<std::string>> installedDestinations)
    {
        metadataPending = false;
        if (closed || failed || ready) return;
        metadata = std::move(installed);
        destinations = std::move(installedDestinations);
        const auto pieces = metadata->geometry().virtualPieces().size();
        scheduler = std::make_unique<Scheduler>(pieces);
        schedulerActions = std::make_unique<SchedulerActionContract>();
        readerSource = std::make_unique<ReaderSource>(*this);
        circular = isCircular;
        rechoke = std::make_unique<SwarmPolicy>(0, uploadSlotCount);
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
        connections.handshake(observation.peer);
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
            peerSearch->onSwarmState(connections.queued(), swarmPaused, now());
    }

    void updateSwarmCap()
    {
        if (!swarmCap || !transport || closed) return;
        const auto stats = transport->statistics();
        std::vector<BufferSelection> selections;
        if (scheduler)
            for (const auto &selection : scheduler->selections()) {
                // M172 background selections omit readFrom/selectTo. They do
                // not contribute to the reader-buffer cap calculation.
                if (cacheSelectionBounds.count(selection.id)) continue;
                selections.push_back({selection.from, selection.offset,
                                      selection.readFrom, selection.selectTo});
            }
        setSwarmPaused(SwarmCaps::shouldPause(stats.unchokedPeers, downloadMeter ? downloadMeter->sample() : 0,
                                              selections, *swarmCap));
    }

    void setSwarmPaused(bool paused)
    {
        if (closed) return;
        swarmPaused = paused;
        if(paused) connections.pause(); else connections.resume(now());
        if (transport && submittedPause.value_or(false) != paused
            && transport->submit(ports::PauseAction{generation, paused}))
            submittedPause = paused;
        drainConnections();
        // pws emits "pause"/"resume"; M612 listens on both.
        peerSearchUpdate();
    }

    void drainPeerAdds()
    {
        if (!peerSearch) return;
        for (auto &address : peerSearch->takePeerAdds()) {
            const auto parsed = parseSourcePeerAddress(address);
            if (!parsed) {
                ++discovery.malformedRejected;
                continue;
            }
            if (connections.findRaw(address)) {
                ++discovery.duplicatesSuppressed;
                continue;
            }
            const auto peer = nextPeerHandle++;
            connections.add(ports::ConnectAction{generation, peer, parsed->host, static_cast<std::uint16_t>(parsed->portNumber),parsed->raw,parsed->portText,0,std::floor(parsed->portNumber)!=parsed->portNumber},now());
            drainConnections();
        }
    }

    void drainConnections()
    {
        if(closed) return;
        discovery.queued=connections.queued();
        for(auto &action:connections.takeConnects()) {
            admittedAttempts[action.peer]=action.attempt;
            auto &meter=dialingMeters[action.peer];
            meter.down=std::make_unique<EngineSpeedometer>(speedometerTick);
            meter.up=std::make_unique<EngineSpeedometer>(speedometerTick);
            submitConnect(std::move(action));
        }
        for(auto peer:connections.takeDisconnects()) connectionTimeouts.insert(peer);
        if(transport) for(auto it=connectionTimeouts.begin();it!=connectionTimeouts.end();) {
            const auto identity=connectionIdentities.find(*it);
            transport->submit(ports::DisconnectAction{generation,*it,identity==connectionIdentities.end()?0:identity->second,admittedAttempts[*it]});
            it=connectionTimeouts.erase(it);
        }
    }

    void acceptLifecycle(const ports::PeerLifecycleObservation &event)
    {
        if(closed || event.generation!=generation) return;
        if(event.attempt && admittedAttempts[event.peer]!=event.attempt) return;
        if(event.kind==ports::PeerLifecycleKind::Started) {
            connectionIdentities[event.peer]=event.connectionIdentity;
            auto &meter=peerMeters[event.connectionIdentity];
            const auto dialing=dialingMeters.find(event.peer);
            if(dialing!=dialingMeters.end()){meter=std::move(dialing->second);dialingMeters.erase(dialing);}
            else {
                meter.down=std::make_unique<EngineSpeedometer>(speedometerTick);
                meter.up=std::make_unique<EngineSpeedometer>(speedometerTick);
            }
        } else {
            const auto current=connectionIdentities.find(event.peer);
            if(current==connectionIdentities.end() && event.connectionIdentity==0 && event.kind==ports::PeerLifecycleKind::Disconnected) {
                connections.disconnected(event.peer,now());connectionTimeouts.erase(event.peer);dialingMeters.erase(event.peer);drainConnections();return;
            }
            if(current==connectionIdentities.end() || current->second!=event.connectionIdentity) return;
            if(event.kind==ports::PeerLifecycleKind::Connected) connections.connected(event.peer);
            else if(event.kind==ports::PeerLifecycleKind::Ready) connections.handshake(event.peer);
            else {
                connections.disconnected(event.peer,now());
                connectionIdentities.erase(current);connectionTimeouts.erase(event.peer);
                peerMeters.erase(event.connectionIdentity);
                peers.erase(event.peer);available.erase(event.peer);advertisedByPeer.erase(event.peer);
                peerDownloaded.erase(event.peer);interestedPeers.erase(event.peer);
                if(rechoke) rechoke->removePeer(std::to_string(event.peer));
            }
        }
        drainConnections();
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
        auto rejected = onApp([peer = action.peer, attempt=action.attempt](Impl &self) { self.connectRejected(peer,attempt); });
        auto app = appPost;
        // ConnectAction blocks on a native event-loop barrier, so it is peer work.
        post([state, action = std::move(action), sink, rejected, app]() {
            if (state->closing.load() || !state->transport) return;
            const bool accepted = state->transport->submit(action);
            sink("connect", action.peer, action.port, accepted ? 1 : 0);
            if (!accepted && app) static_cast<void>(app(rejected));
        });
    }

    void connectRejected(ports::PeerHandle peer,std::uint64_t attempt)
    {
        if(admittedAttempts[peer]!=attempt) return;
        dialingMeters.erase(peer);
        ++discovery.connectsRejected;
        connections.disconnected(peer,now());
        drainConnections();
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
            if (onVerified) onVerified(piece);
            if (closed) return;
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
        std::unique_lock<std::mutex> admission(state->readMutex, std::defer_lock);
        if (circular) {
            admission.lock();
            state->pendingReads.insert(token);
        }
        post([state, token, piece, completion = std::move(completion), sink, clockCopy, weak,
              app]() mutable {
            std::unique_lock<std::mutex> reservation(state->readMutex, std::defer_lock);
            if (state->circular) {
                reservation.lock();
                if (state->pendingReads.erase(token) == 0) return;
            }
            if (state->closing.load() || !state->store) return;
            std::string error;
            std::optional<ByteBuffer> bytes;
            try {
                bytes = state->store->read(piece, clockCopy(), &error);
            } catch (const std::exception &failure) {
                error = failure.what();
            } catch (...) {
                error = "piece store read failed";
            }
            // A late app completion owns its copied bytes, not a store slot.
            if (reservation.owns_lock()) reservation.unlock();
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
        const auto group = realPieceGroup(
            metadata->geometry().virtualPieces().at(piece).verificationIndex.value);
        const auto epoch = stageEpoch;
        post([state, piece, group, epoch, bytes = std::move(bytes), sink, clockCopy, weak, app]() mutable {
            if (state->closing.load() || !state->store) return;
            std::unique_lock<std::mutex> reservation(state->readMutex, std::defer_lock);
            if (state->circular) reservation.lock();
            if (state->circular && !state->pendingReads.empty()) {
                // Stage may have been posted before a later reader was admitted.
                // Let its queued reads copy their bytes before any cache eviction.
                // Retain this received piece once; do not turn cache pressure into
                // a terminal failure or discard it and request the network again.
                reservation.unlock();
                if (app) static_cast<void>(app([weak, piece, epoch, bytes = std::move(bytes)]() mutable {
                    const auto self = weak.lock();
                    if (self && !self->closed && self->stageEpoch == epoch)
                        self->postStage(piece, std::move(bytes));
                }));
                return;
            }
            const auto length = bytes.size();
            StageResult result;
            try {
                result = state->store->stage(piece, std::move(bytes), clockCopy());
            } catch (const std::exception &failure) {
                result.complete = true;
                result.start = group.first;
                result.endExclusive = group.second;
                result.error = failure.what();
            } catch (...) {
                result.complete = true;
                result.start = group.first;
                result.endExclusive = group.second;
                result.error = "piece store stage failed";
            }
            if (reservation.owns_lock()) reservation.unlock();
            sink("stage", piece, 0, 0, length);
            if (!result.complete)
                sink("verify-incomplete", piece, result.start, result.endExclusive);
            else if (!result.success && result.retryable)
                sink("verify-failure", piece, result.start, result.endExclusive);
            else if (result.success)
                sink("verify-success", piece, result.start, result.endExclusive);
            else
                sink("store-error", piece, result.start, result.endExclusive);
            if (result.success)
                sink("commit", piece, result.start, result.endExclusive);
            if (app) static_cast<void>(app([weak, piece, epoch, result] {
                if (const auto self = weak.lock()) self->stageResult(piece, result, epoch);
            }));
        });
    }

    void stageResult(std::size_t piece, const StageResult &result, std::uint64_t epoch)
    {
        if (closed || !ready) return;
        // Valid already-queued commits remain usable. Old failures/incomplete
        // work must not resurrect demand canceled by a terminal store failure.
        if (epoch != stageEpoch && !result.success) return;
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
                // All readers below become terminal. Their engine-owned
                // verification-group selections must not outlive that demand.
                ++stageEpoch;
                staging.clear();
                demandedPieces.clear();
                for (const auto &[verification, selection] : groupSelections) {
                    static_cast<void>(verification);
                    static_cast<void>(scheduler->deselect(selection));
                }
                groupSelections.clear();
                for (const auto &[id, request] : activeRequests) {
                    static_cast<void>(id);
                    if (const auto wire = wireAction(SchedulerAction{
                            SchedulerActionType::Cancel, request, true}))
                        static_cast<void>(transport->submit(*wire));
                    schedulerActions->finish(request.requestId, generation, RequestOutcome::Canceled);
                    if (const auto buffer = pieceBuffers.find(request.piece); buffer != pieceBuffers.end())
                        buffer->second->cancel(request.block);
                }
                activeRequests.clear();
                pieceBuffers.clear();
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
            std::optional<ByteBuffer> bytes;
            try {
                bytes = state->store->uploadRead(virtualPiece, group.first, group.second);
            } catch (...) {
                // The existing app-lane path emits one ownership-matched abort.
                bytes.reset();
            }
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
        connections.close();
        connectionIdentities.clear();connectionTimeouts.clear();admittedAttempts.clear();dialingMeters.clear();
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
            {
                std::lock_guard<std::mutex> lock(state->readMutex);
                state->pendingReads.clear();
            }
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
    impl->discoveryNetworkFactory = std::move(wiring.discoveryNetworkFactory);
    impl->workPost = std::move(wiring.workPost);
    impl->appPost = std::move(wiring.appPost);
    impl->repeat = std::move(wiring.repeat);
    impl->speedometerTick=std::move(wiring.speedometerTick);
    impl->downloadMeter=std::make_unique<EngineSpeedometer>(impl->speedometerTick);
    impl->clock = wiring.clock ? std::move(wiring.clock) : EngineClock([] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    });
    impl->epochOffsetMs=QDateTime::currentMSecsSinceEpoch()-static_cast<std::int64_t>(impl->now());
    impl->trace = TraceSink{std::move(wiring.trace), impl->sourceKey, impl->generation};
    impl->onReady = std::move(wiring.onReady);
    impl->onFailed = std::move(wiring.onFailed);
    impl->onVerified = std::move(wiring.onVerified);
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
std::optional<EngineFileCacheSnapshot> TorrentEngine::fileCacheSnapshot(std::size_t index) const
{
    const auto self=impl_;
    if (self->closed || !self->ready || !self->metadata || index>=self->metadata->files().size()) return {};
    EngineFileCacheSnapshot result;
    result.file=self->metadata->files()[index];
    result.pieceLength=self->metadata->geometry().virtualPieceLength();
    result.verificationLength=self->metadata->geometry().verificationPieceLength();
    result.committedPieces.assign(self->committed.begin(),self->committed.end());
    const auto *buffer=self->creationOptions.find("buffer");
    result.buffered=buffer && jsTruthy(*buffer);
    if(index<self->destinations.size()) result.destination=self->destinations[index];
    return result;
}

void TorrentEngine::selectCacheFile(std::size_t index)
{
    const auto self=impl_;
    const auto snapshot=fileCacheSnapshot(index);
    if(!snapshot || snapshot->buffered || !self->scheduler) return;
    const auto &geometry=self->metadata->geometry();
    const auto count=geometry.virtualPieces().size();
    if(count==0) return;
    const auto verification=static_cast<std::int64_t>(snapshot->verificationLength);
    const auto ratio=snapshot->verificationLength/snapshot->pieceLength;
    const auto from=static_cast<std::size_t>(snapshot->file.offset/verification)*ratio;
    const auto sourceTo=static_cast<std::size_t>((snapshot->file.offset+snapshot->file.length-1)/verification+1)*ratio;
    if(from>=count) return;
    const auto to=std::min<std::size_t>(sourceTo,count-1);
    const auto id=self->scheduler->select(from,to,Value::boolean(false));
    // Preserve M172's inclusive source endpoint separately from safe physical
    // demand; the source can expose one-past-final-piece in its selection view.
    self->cacheSelectionBounds.emplace(id,std::make_pair(from,sourceTo));
    std::set<std::size_t> groups;
    for(std::size_t piece=from;piece<=to;++piece)
        if(!self->committed.count(piece)) groups.insert(geometry.virtualPieces()[piece].verificationIndex.value);
    for(const auto group:groups) {
        const auto [start,end]=self->realPieceGroup(group);
        for(auto piece=start;piece<end;++piece)
            if(!self->committed.count(piece)) self->demandedPieces.insert(piece);
        self->ensureGroupSelection(group);
    }
    self->pumpRequests();
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
    const bool added=impl_->connections.add(ports::ConnectAction{impl_->generation,peer,std::move(address),port},impl_->now());
    impl_->drainConnections();
    return added;
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

Value TorrentEngine::statistics(Value index) const
{
    const auto &self=*impl_;
    EngineStatisticsSnapshot snapshot;
    snapshot.infoHash=self.sourceKey;
    snapshot.options=self.options;
    snapshot.swarmPaused=self.swarmPaused;
    const auto *size=self.creationOptions.find("connections");
    if(!size || !jsTruthy(*size)) size=self.creationOptions.find("size");
    if(size && jsTruthy(*size)) snapshot.swarmSize=*size;
    if(self.metadata) {
        snapshot.name=self.metadata->name();
        snapshot.files=self.metadata->files();
        snapshot.pieceLength=self.metadata->geometry().virtualPieceLength();
        snapshot.availablePieces=self.committed;
    }
    const auto stats=transportStatistics();
    impl_->sampleStatistics(stats);
    snapshot.peers=stats.connectedPeers;
    snapshot.unchoked=stats.unchokedPeers;
    snapshot.queued=self.connections.queued();
    snapshot.unique=self.connections.unique();
    snapshot.connectionTries=self.connections.tries();
    snapshot.swarmConnections=self.connections.connections();
    snapshot.downloaded=stats.wirePayloadBytesReceived;
    snapshot.uploaded=stats.uploadedBytes;
    snapshot.downloadSpeed=self.downloadMeter ? self.downloadMeter->sample() : 0;
    Value::Array selections;
    if(self.scheduler) for(const auto &selection:self.scheduler->selections()) {
        // Internal verification-group scheduling has no JavaScript selection.
        if(std::any_of(self.groupSelections.begin(),self.groupSelections.end(),
            [&](const auto &entry){return entry.second==selection.id;})) continue;
        Value::Object entry{{"from",Value::number(selection.from)},
            {"to",Value::number(selection.to)},{"offset",Value::number(selection.offset)},
            {"priority",Value::number(selection.priority)}};
        bool buffered=false;
        for(const auto &weak:self.readers) if(auto reader=weak.lock())
            if(reader->selectionId()==selection.id && reader->bufferPieces()) buffered=true;
        if(buffered) {
            entry.emplace_back("selectTo",Value::number(selection.selectTo));
            entry.emplace_back("readFrom",Value::number(selection.readFrom));
        }
        selections.push_back(Value::object(std::move(entry)));
    }
    snapshot.selections=Value::array(std::move(selections));
    Value::Array wires;
    for(const auto &wire:stats.peerTransfers) if(!wire.peerChoking) {
        const auto meter=self.peerMeters.find(wire.connectionIdentity);
        const auto &address=wire.sourceAddress.empty() ? wire.address : wire.sourceAddress;
        wires.push_back(Value::object({{"requests",Value::number(wire.requests)},
            {"address",address.empty() ? Value::null() : Value::string(address)},
            {"amInterested",Value::boolean(wire.amInterested)},
            {"isSeeder",self.metadata ? Value::boolean(wire.isSeeder) : Value::missing()},
            {"downSpeed",Value::number(meter->second.down->sample())},
            {"upSpeed",Value::number(meter->second.up->sample())}}));
    }
    snapshot.wires=Value::array(std::move(wires));
    if(self.peerSearch) {
        Value::Array sources;
        for(const auto &source:self.peerSearch->stats()) sources.push_back(Value::object({
            {"numFound",Value::number(source.numFound)},
            {"numFoundUniq",Value::number(source.numFoundUniq)},
            {"numRequests",Value::number(source.numRequests)},
            {"url",Value::string(source.url)},
            {"lastStarted",Value::string(QDateTime::fromMSecsSinceEpoch(
                self.epochOffsetMs+static_cast<std::int64_t>(source.lastStartedMs),Qt::UTC)
                .toString(Qt::ISODateWithMs).toStdString())}}));
        snapshot.sources=Value::array(std::move(sources));
        snapshot.peerSearchRunning=Value::boolean(self.peerSearch->isRunning());
    }
    return projectEngineStatistics(snapshot,index);
}

} // namespace server1::policy
