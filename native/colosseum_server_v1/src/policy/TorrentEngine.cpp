#include "server1/policy/EngineRegistry.h"

#include "server1/discovery/PeerSearch.h"
#include "server1/policy/CircularPieceStore.h"
#include "server1/policy/PieceBuffer.h"
#include "server1/policy/PieceStore.h"
#include "server1/policy/Scheduler.h"
#include "server1/policy/SchedulerActions.h"
#include "server1/policy/SwarmCaps.h"
#include "server1/policy/TorrentMetadata.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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

class ReaderStore : public FileReaderSource {
public:
    struct Commit final {
        bool complete = false;
        bool success = false;
        bool advertise = false;
        std::size_t start = 0;
        std::size_t endExclusive = 0;
        std::string error;
        bool retryable = false;
    };
    ~ReaderStore() override = default;
    virtual Commit stage(std::size_t piece, ByteBuffer bytes, std::uint64_t nowMs) = 0;
    [[nodiscard]] virtual std::vector<std::size_t> restored(std::size_t count) const = 0;
    [[nodiscard]] virtual std::optional<ByteBuffer> upload(std::size_t piece) = 0;
    [[nodiscard]] virtual bool uploadsAllowed() const noexcept = 0;
    virtual void close() = 0;
};

class PersistentReaderStore final : public ReaderStore {
public:
    PersistentReaderStore(std::filesystem::path root,
                          const TorrentMetadata &metadata)
        : store_(std::move(root),
                 static_cast<std::size_t>(metadata.geometry().virtualPieceLength()),
                 static_cast<std::size_t>(metadata.length()),
                 static_cast<std::size_t>(metadata.pieceLength()),
                 makeFiles(metadata.files()),
                 metadata.pieces())
    {}

    bool hasPiece(std::size_t piece) const override
    {
        return store_.isCommitted(piece);
    }

    void readPiece(std::uint64_t token,
                   std::size_t piece,
                   Completion completion) override
    {
        std::string error;
        auto bytes = store_.read(piece, &error);
        completion(token, piece, bytes.value_or(ByteBuffer{}),
                   bytes ? std::string{} : std::move(error));
    }

    bool cancelRead(std::uint64_t) override { return false; }
    Commit stage(std::size_t piece, ByteBuffer bytes, std::uint64_t) override
    {
        store_.stage(piece, std::move(bytes));
        const auto verified = store_.verify(piece);
        if (!verified.complete)
            return {false, false, false, verified.start, verified.endExclusive, {}};
        if (!verified.success)
            return {true, false, false, verified.start, verified.endExclusive,
                    "SHA-1 verification failed", true};
        const auto committed = store_.commit(verified.start, verified.endExclusive);
        return {true, committed.state == CommitState::Committed,
                committed.state == CommitState::Committed && !committed.noNotifyHave,
                verified.start, verified.endExclusive, committed.error};
    }
    std::vector<std::size_t> restored(std::size_t count) const override
    {
        std::vector<std::size_t> result;
        for (std::size_t piece = 0; piece < count; ++piece)
            if (store_.isCommitted(piece)) result.push_back(piece);
        return result;
    }
    std::optional<ByteBuffer> upload(std::size_t piece) override
    {
        return store_.isCommitted(piece) ? store_.read(piece) : std::nullopt;
    }
    bool uploadsAllowed() const noexcept override { return true; }
    void close() override { store_.close(); }

private:
    static std::vector<StoreFile> makeFiles(const std::vector<TorrentFile> &files)
    {
        std::vector<StoreFile> result;
        result.reserve(files.size());
        for (const auto &file : files) {
            result.push_back({static_cast<std::size_t>(file.offset),
                              static_cast<std::size_t>(file.length)});
        }
        return result;
    }

    PersistentPieceStore store_;
};

class CircularReaderStore final : public ReaderStore {
public:
    CircularReaderStore(std::filesystem::path root,
                        std::size_t sizeBytes,
                        std::size_t pieceLength,
                        const TorrentMetadata &metadata)
        : store_(std::move(root), CircularStoreMode::Memory,
                 std::max(sizeBytes, pieceLength), pieceLength)
        , metadata_(metadata)
    {}

    bool hasPiece(std::size_t piece) const override
    {
        return committed_.count(piece) != 0;
    }

    void readPiece(std::uint64_t token,
                   std::size_t piece,
                   Completion completion) override
    {
        auto bytes = committed_.count(piece) ? store_.read(piece, nowMs_) : std::nullopt;
        completion(token, piece, bytes.value_or(ByteBuffer{}),
                   bytes ? std::string{} : "piece is unavailable");
    }

    bool cancelRead(std::uint64_t) override { return false; }
    Commit stage(std::size_t piece, ByteBuffer bytes, std::uint64_t nowMs) override
    {
        nowMs_ = nowMs;
        const auto written = store_.write(piece, std::move(bytes), {}, {}, nowMs);
        if (!written.success)
            return {true, false, false, piece, piece + 1, written.error};
        if (written.resetPiece) committed_.erase(*written.resetPiece);
        const auto &coordinate = metadata_.geometry().virtualPieces().at(piece);
        const auto verification = coordinate.verificationIndex.value;
        const auto &verificationCoordinate = metadata_.geometry().verificationPieces().at(verification);
        const auto virtualLength = metadata_.geometry().virtualPieceLength();
        const auto start = static_cast<std::size_t>(verificationCoordinate.offset / virtualLength);
        const auto endExclusive = static_cast<std::size_t>(
            (verificationCoordinate.offset + verificationCoordinate.length + virtualLength - 1)
            / virtualLength);
        staged_.insert(piece);
        for (std::size_t item = start; item < endExclusive; ++item)
            if (item != piece && staged_.count(item) == 0)
                return {false, false, false, start, endExclusive, {}};
        const auto committed = store_.commit(start, endExclusive - 1,
                                             metadata_.pieces().at(verification));
        if (!committed.success) {
            for (std::size_t item = start; item < endExclusive; ++item) {
                staged_.erase(item);
                committed_.erase(item);
            }
            return {true, false, false, start, endExclusive, committed.error, true};
        }
        for (std::size_t item = start; item < endExclusive; ++item) {
            committed_.insert(item);
            staged_.erase(item);
        }
        return {true, true, false, start, endExclusive, {}};
    }
    std::vector<std::size_t> restored(std::size_t) const override { return {}; }
    std::optional<ByteBuffer> upload(std::size_t) override { return std::nullopt; }
    bool uploadsAllowed() const noexcept override { return false; }
    void close() override { store_.close(); }

private:
    mutable CircularPieceStore store_;
    const TorrentMetadata &metadata_;
    mutable std::uint64_t nowMs_ = 0;
    std::set<std::size_t> staged_;
    std::set<std::size_t> committed_;
};

std::size_t optionSize(const Value &options,
                       std::string_view key,
                       std::size_t fallback)
{
    const auto *value = options.find(key);
    if (!value)
        return fallback;
    const auto checked = checkedSize(*value, std::numeric_limits<std::size_t>::max());
    return checked.value_or(fallback);
}

std::optional<double> optionNumber(const Value &options,
                                   std::string_view objectKey,
                                   std::string_view key)
{
    const auto *object = options.find(objectKey);
    if (!object || object->kind() != Value::Kind::Object) return std::nullopt;
    const auto *value = object->find(key);
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

std::optional<std::size_t> configuredBound(const Value &options,
                                           std::string_view key)
{
    const auto *peerSearch = options.find("peerSearch");
    if (!peerSearch || peerSearch->kind() != Value::Kind::Object)
        return std::nullopt;
    const auto *value = peerSearch->find(key);
    if (!value)
        return std::nullopt;
    return checkedSize(*value, std::numeric_limits<std::size_t>::max());
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

} // namespace

struct TorrentEngine::Impl final : std::enable_shared_from_this<TorrentEngine::Impl> {
    std::string sourceKey;
    ports::EngineGeneration generation = 0;
    Value options = Value::object({});
    std::filesystem::path cachePath;
    std::unique_ptr<ports::TorrentTransport> transport;
    std::optional<TorrentMetadata> metadata;
    std::unique_ptr<Scheduler> scheduler;
    std::unique_ptr<SchedulerActionContract> schedulerActions;
    std::unique_ptr<ReaderStore> store;
    std::unique_ptr<discovery::PeerSearch> peerSearch;
    std::shared_ptr<EngineTimer> timer;
    EnginePost workPost;
    EngineClock clock;
    std::vector<std::weak_ptr<FileReader>> readers;
    std::map<std::size_t, std::unique_ptr<PieceBuffer>> pieceBuffers;
    std::map<std::uint64_t, RequestIdentity> activeRequests;
    std::map<ports::PeerHandle, std::vector<bool>> available;
    std::map<ports::PeerHandle, ports::PeerObservation> peers;
    std::set<std::size_t> demandedPieces;
    std::set<std::size_t> advertisedVerificationPieces;
    std::set<ports::PeerHandle> interestedPeers;
    std::optional<bool> submittedPause;
    std::string sourceError;
    std::size_t resumeCount = 0;
    bool ready = false;
    bool failed = false;
    bool closed = false;
    bool circular = false;

    void tick()
    {
        if (closed) return;
        const auto now = clock ? clock() : 0;
        if (scheduler && transport) {
            const auto stats = transport->statistics();
            SwarmCapOptions caps;
            caps.maxSpeed = optionNumber(options, "swarmCap", "maxSpeed");
            caps.maxBuffer = optionNumber(options, "swarmCap", "maxBuffer");
            if (const auto min = optionNumber(options, "swarmCap", "minPeers");
                min && *min >= 0)
                caps.minPeers = static_cast<std::size_t>(*min);
            std::vector<BufferSelection> selections;
            for (const auto &selection : scheduler->selections())
                selections.push_back({selection.from, selection.offset,
                                      selection.readFrom, selection.selectTo});
            const auto *swarmCap = options.find("swarmCap");
            const bool paused = swarmCap && jsTruthy(*swarmCap)
                && SwarmCaps::shouldPause(stats.unchokedPeers,
                                          stats.downloadBytesPerSecond,
                                          selections, caps);
            if (!submittedPause || *submittedPause != paused) {
                if (transport->submit(ports::PauseAction{generation, paused}))
                    submittedPause = paused;
            }
        }
        if (peerSearch) {
            const auto stats = transport ? transport->statistics()
                                         : ports::TransportStatistics{};
            peerSearch->onSwarmState(stats.connectedPeers, stats.paused, now);
            peerSearch->tick(now);
        }
        pruneCanceledRequests();
        processSchedulerEvents();
        pumpRequests();
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

    void pumpRequests()
    {
        if (closed || !ready || !scheduler || !schedulerActions || !transport || !metadata)
            return;
        processSchedulerEvents();
        for (const auto &[peer, pieces] : available) {
            const auto state = peers.find(peer);
            if (state != peers.end() && state->second.choking)
                continue;
            auto eligible = pieces;
            for (std::size_t piece = 0; piece < eligible.size(); ++piece)
                eligible[piece] = eligible[piece] && demandedPieces.count(piece) != 0;
            auto selected = scheduler->choosePiece(eligible,
                                                   transport->statistics().downloadedBytes,
                                                   true);
            while (selected) {
            const auto &coordinate = metadata->geometry().virtualPieces().at(*selected);
            auto &buffer = pieceBuffers[*selected];
            if (!buffer)
                buffer = std::make_unique<PieceBuffer>(static_cast<std::size_t>(coordinate.length),
                                                       generation);
            const auto block = buffer->reserve();
            if (block == PieceBuffer::kNoReservation) {
                eligible[*selected] = false;
                selected = scheduler->choosePiece(eligible,
                                                  transport->statistics().downloadedBytes,
                                                  true);
                continue;
            }
            const auto selection = std::find_if(scheduler->selections().begin(),
                                                scheduler->selections().end(),
                [&](const auto &entry) { return *selected >= entry.from && *selected <= entry.to; });
            if (selection == scheduler->selections().end()) {
                buffer->cancel(static_cast<std::size_t>(block));
                break;
            }
            const NormalRequestCandidate candidate{
                selection->id, generation, *selected, static_cast<std::size_t>(block),
                buffer->offset(static_cast<std::size_t>(block)),
                buffer->size(static_cast<std::size_t>(block)), true, false};
            const auto peerState = state == peers.end() ? ports::PeerObservation{} : state->second;
            const RequestDecisionContext context{
                peer, generation, std::max<std::size_t>(1, transport->statistics().unchokedPeers),
                activeRequests.size(), peerState.downloadBytesPerSecond};
            auto actions = schedulerActions->decide(context, {candidate}, {});
            if (actions.empty()) {
                buffer->cancel(static_cast<std::size_t>(block));
                break;
            }
            for (const auto &action : actions) {
                const auto wire = wireAction(action);
                if (!wire || !transport->submit(*wire)) {
                    schedulerActions->finish(action.request.requestId, generation,
                                             RequestOutcome::Failed);
                    buffer->cancel(action.request.block);
                    continue;
                }
                if (action.type == SchedulerActionType::Request)
                    activeRequests.emplace(action.request.requestId, action.request);
            }
            break;
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

    void notifyCommitted(std::size_t start, std::size_t endExclusive, bool advertise)
    {
        for (std::size_t piece = start; piece < endExclusive; ++piece) {
            demandedPieces.erase(piece);
            scheduler->markPieceComplete(piece);
            if (advertise) {
                const auto verification = metadata->geometry().virtualPieces().at(piece)
                                              .verificationIndex.value;
                if (advertisedVerificationPieces.insert(verification).second)
                    static_cast<void>(transport->submit(ports::AdvertisePieceAction{
                        generation, static_cast<std::uint32_t>(verification)}));
            }
            for (auto &weakReader : readers)
                if (auto reader = weakReader.lock()) reader->notifyPiece(piece);
        }
        scheduler->collectGarbage();
        processSchedulerEvents();
    }

    void demand(const std::shared_ptr<FileReader> &reader)
    {
        if (!reader || !store) return;
        for (std::size_t piece = reader->startPiece(); piece <= reader->endPiece(); ++piece) {
            if (!store->hasPiece(piece)) {
                const auto verification = metadata->geometry().virtualPieces().at(piece)
                                              .verificationIndex.value;
                for (const auto &coordinate : metadata->geometry().virtualPieces())
                    if (coordinate.verificationIndex.value == verification
                        && !store->hasPiece(coordinate.piece.value))
                        demandedPieces.insert(coordinate.piece.value);
                break;
            }
        }
        pumpRequests();
    }

    void acceptBlock(const ports::BlockObservation &observation)
    {
        const auto found = activeRequests.find(observation.ownership.requestId);
        if (found == activeRequests.end()) return;
        const auto request = found->second;
        if (observation.ownership.generation != generation
            || observation.ownership.selectionId != request.selectionId
            || observation.peer != request.peer
            || observation.block.piece != wireBlock(request).piece
            || observation.block.blockOrdinal != wireBlock(request).blockOrdinal
            || observation.block.offset != wireBlock(request).offset
            || observation.block.length != wireBlock(request).length)
            return;
        activeRequests.erase(found);
        schedulerActions->finish(request.requestId, generation, RequestOutcome::Completed);
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
        const auto committed = store->stage(request.piece, std::move(*bytes),
                                            clock ? clock() : 0);
        if (!committed.complete) {
            pumpRequests();
            return;
        }
        if (!committed.success) {
            for (auto current = activeRequests.begin(); current != activeRequests.end();) {
                if (current->second.piece < committed.start
                    || current->second.piece >= committed.endExclusive) {
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
                committed.start, committed.endExclusive, generation);
            for (const auto piece : reset) {
                scheduler->resetPiece(piece);
                pieceBuffers.erase(piece);
            }
            if (!committed.retryable) {
                const auto reason = committed.error.empty() ? "piece store commit failed"
                                                            : committed.error;
                for (auto &weakReader : readers)
                    if (auto reader = weakReader.lock()) reader->fail(reason);
                return;
            }
            pumpRequests();
            return;
        }
        notifyCommitted(committed.start, committed.endExclusive, committed.advertise);
        pumpRequests();
    }

    void acceptFailure(const ports::FailureObservation &observation)
    {
        const auto found = activeRequests.find(observation.ownership.requestId);
        if (found == activeRequests.end()) return;
        const auto request = found->second;
        if (observation.ownership.generation != generation
            || observation.ownership.selectionId != request.selectionId
            || observation.peer != request.peer
            || observation.block.piece != wireBlock(request).piece
            || observation.block.blockOrdinal != wireBlock(request).blockOrdinal
            || observation.block.offset != wireBlock(request).offset
            || observation.block.length != wireBlock(request).length)
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

    void acceptUpload(const ports::UploadRequestObservation &observation)
    {
        if (observation.ownership.generation != generation || !transport || !store || !metadata)
            return;
        if (observation.block.piece >= metadata->geometry().verificationPieces().size()) {
            static_cast<void>(transport->submit(ports::UploadAbortAction{
                observation.ownership, observation.peer, observation.block}));
            return;
        }
        const auto &verification = metadata->geometry().verificationPieces().at(
            observation.block.piece);
        if (observation.block.offset > verification.length
            || observation.block.length > verification.length - observation.block.offset) {
            static_cast<void>(transport->submit(ports::UploadAbortAction{
                observation.ownership, observation.peer, observation.block}));
            return;
        }
        const auto global = verification.offset + observation.block.offset;
        const auto virtualLength = metadata->geometry().virtualPieceLength();
        const auto virtualPiece = static_cast<std::size_t>(global / virtualLength);
        const auto offset = static_cast<std::size_t>(global % virtualLength);
        const auto length = static_cast<std::size_t>(observation.block.length);
        auto bytes = virtualPiece < metadata->geometry().virtualPieces().size()
                && store->uploadsAllowed()
            ? store->upload(virtualPiece)
                                             : std::nullopt;
        if (!bytes || offset > bytes->size() || length > bytes->size() - offset) {
            static_cast<void>(transport->submit(ports::UploadAbortAction{
                observation.ownership, observation.peer, observation.block}));
            return;
        }
        ByteBuffer payload(bytes->begin() + static_cast<std::ptrdiff_t>(offset),
                           bytes->begin() + static_cast<std::ptrdiff_t>(offset + length));
        static_cast<void>(transport->submit(ports::UploadResponseAction{
            observation.ownership, observation.peer, observation.block, std::move(payload)}));
    }
};

TorrentEngine::TorrentEngine(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl))
{}

TorrentEngine::~TorrentEngine()
{
    close();
}

std::shared_ptr<TorrentEngine>
EngineRegistry::makeEngine(std::string sourceKey,
                           ports::EngineGeneration generation,
                           Value options,
                           std::filesystem::path cachePath,
                           std::unique_ptr<ports::TorrentTransport> transport,
                           EnginePost workPost,
                           EngineRepeat repeat,
                           EngineClock clock)
{
    if (!transport || !transport->configureAutonomy(discovery::AutonomyPolicy{}))
        throw std::runtime_error("torrent transport rejected external scheduler ownership");
    auto impl = std::make_shared<TorrentEngine::Impl>();
    impl->sourceKey = std::move(sourceKey);
    impl->generation = generation;
    impl->options = std::move(options);
    impl->cachePath = std::move(cachePath);
    impl->transport = std::move(transport);
    impl->workPost = std::move(workPost);
    impl->clock = clock ? std::move(clock) : EngineClock([] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    });
    std::weak_ptr<TorrentEngine::Impl> weakImpl = impl;
    auto tick = [weakImpl] {
        if (const auto locked = weakImpl.lock()) {
            auto task = [weakImpl] {
                if (const auto owner = weakImpl.lock()) owner->tick();
            };
            if (!locked->workPost || !locked->workPost(std::move(task))) {
                // A rejected executor never runs work inline on the timer lane.
            }
        }
    };
    impl->timer = repeat ? repeat(500, std::move(tick))
                         : std::make_shared<DefaultRepeatTimer>(500, std::move(tick));
    if (!impl->timer)
        throw std::runtime_error("engine repeat timer could not be created");
    return std::shared_ptr<TorrentEngine>(new TorrentEngine(std::move(impl)));
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
    return impl_->store && !impl_->circular;
}
bool TorrentEngine::hasCircularStore() const noexcept
{
    return impl_->store && impl_->circular;
}
bool TorrentEngine::hasPeerSearch() const noexcept { return !!impl_->peerSearch; }
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
    if (!impl_->ready || impl_->closed || !impl_->metadata || !impl_->scheduler || !impl_->store)
        throw std::logic_error("torrent engine is not ready");
    if (fileIndex >= impl_->metadata->files().size())
        throw std::out_of_range("torrent file index is outside metadata");
    if (options.generation == 0)
        options.generation = impl_->generation;
    std::weak_ptr<Impl> weakImpl = impl_;
    auto weakReader = std::make_shared<std::weak_ptr<FileReader>>();
    auto reader = std::make_shared<FileReader>(
        *impl_->scheduler, *impl_->store, impl_->metadata->files()[fileIndex],
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
    return !impl_->closed && impl_->transport
        && impl_->transport->submit(ports::ConnectAction{
            impl_->generation, peer, std::move(address), port});
}

void TorrentEngine::resume(Value options)
{
    if (impl_->closed)
        return;
    impl_->options = std::move(options);
    ++impl_->resumeCount;
}

std::vector<ports::TorrentObservation> TorrentEngine::pollTransport()
{
    if (impl_ && impl_->ready) {
        impl_->pruneCanceledRequests();
        impl_->pumpRequests();
    }
    return impl_->closed || !impl_->transport
        ? std::vector<ports::TorrentObservation>{}
        : impl_->transport->poll();
}

bool TorrentEngine::acceptMetadata(const ports::MetadataReadyObservation &metadata,
                                   std::string *error)
{
    if (impl_->closed || impl_->ready || impl_->failed
        || metadata.generation != impl_->generation
        || hashHex(metadata.infoHash) != impl_->sourceKey) {
        return false;
    }

    ByteBuffer torrent{'d', '4', ':', 'i', 'n', 'f', 'o'};
    torrent.insert(torrent.end(), metadata.infoSection.begin(), metadata.infoSection.end());
    torrent.push_back('e');
    std::string parseError;
    auto parsed = TorrentMetadata::parse(torrent, &parseError);
    if (!parsed || parsed->infoHash() != impl_->sourceKey) {
        impl_->failed = true;
        impl_->sourceError = parsed ? "metadata info hash mismatch" : std::move(parseError);
        if (error)
            *error = impl_->sourceError;
        return false;
    }

    try {
        impl_->metadata = std::move(*parsed);
        const auto pieceLength = static_cast<std::size_t>(impl_->metadata->geometry().virtualPieceLength());
        impl_->scheduler = std::make_unique<Scheduler>(impl_->metadata->geometry().virtualPieces().size());
        impl_->schedulerActions = std::make_unique<SchedulerActionContract>();
        const auto *circularOption = impl_->options.find("circularBuffer");
        impl_->circular = circularOption && jsTruthy(*circularOption);
        if (impl_->circular) {
            const auto *bufferOption = impl_->options.find("buffer");
            if (!bufferOption || !jsTruthy(*bufferOption))
                throw std::runtime_error("circularBuffer can only be used with buffer");
            impl_->store = std::make_unique<CircularReaderStore>(
                impl_->cachePath, optionSize(impl_->options, "buffer", pieceLength * 4U),
                pieceLength, *impl_->metadata);
        } else {
            impl_->store = std::make_unique<PersistentReaderStore>(impl_->cachePath, *impl_->metadata);
        }

        const auto *peerSearchOption = impl_->options.find("peerSearch");
        if (peerSearchOption && jsTruthy(*peerSearchOption)) {
            auto sources = discovery::PeerSearch::selectSources(
                metadata.trackers, configuredPeerSources(impl_->options), impl_->sourceKey);
            impl_->peerSearch = std::make_unique<discovery::PeerSearch>(
                std::move(sources), configuredBound(impl_->options, "min"),
                configuredBound(impl_->options, "max"), 0);
        }
        impl_->ready = true;
        for (const auto piece : impl_->store->restored(
                 impl_->metadata->geometry().virtualPieces().size()))
            impl_->notifyCommitted(piece, piece + 1, !impl_->circular);
        return true;
    } catch (const std::exception &exception) {
        impl_->failed = true;
        impl_->sourceError = exception.what();
        if (error)
            *error = impl_->sourceError;
        return false;
    }
}

void TorrentEngine::acceptSourceFailure(const ports::SourceFailureObservation &failure)
{
    if (impl_->closed || impl_->ready || impl_->failed
        || failure.generation != impl_->generation
        || hashHex(failure.infoHash) != impl_->sourceKey) {
        return;
    }
    impl_->failed = true;
    impl_->sourceError = failure.error.empty() ? "torrent source failed" : failure.error;
}

void TorrentEngine::acceptRuntimeObservation(const ports::TorrentObservation &observation)
{
    if (impl_->closed || !impl_->ready)
        return;
    if (const auto *available = std::get_if<ports::AvailablePiecesObservation>(&observation)) {
        if (available->generation != impl_->generation) return;
        auto &snapshot = impl_->available[available->peer];
        snapshot.assign(impl_->metadata->geometry().virtualPieces().size(), false);
        for (const auto piece : available->pieces) {
            if (piece >= impl_->metadata->geometry().verificationPieces().size()) continue;
            for (const auto &coordinate : impl_->metadata->geometry().virtualPieces())
                if (coordinate.verificationIndex.value == piece)
                    snapshot[coordinate.piece.value] = true;
        }
        impl_->pumpRequests();
    } else if (const auto *peer = std::get_if<ports::PeerObservation>(&observation)) {
        impl_->peers[peer->peer] = *peer;
        impl_->pumpRequests();
    } else if (const auto *block = std::get_if<ports::BlockObservation>(&observation)) {
        impl_->acceptBlock(*block);
    } else if (const auto *failure = std::get_if<ports::FailureObservation>(&observation)) {
        impl_->acceptFailure(*failure);
    } else if (const auto *upload = std::get_if<ports::UploadRequestObservation>(&observation)) {
        impl_->acceptUpload(*upload);
    }
}

void TorrentEngine::close()
{
    if (!impl_ || impl_->closed)
        return;
    impl_->closed = true;
    if (impl_->timer) impl_->timer->cancel();
    if (impl_->schedulerActions) {
        for (const auto &[id, request] : impl_->activeRequests) {
            static_cast<void>(id);
            impl_->schedulerActions->finish(request.requestId, impl_->generation,
                                            RequestOutcome::Canceled);
        }
        impl_->activeRequests.clear();
    }
    for (auto &weakReader : impl_->readers)
        if (auto reader = weakReader.lock())
            reader->close();
    impl_->readers.clear();
    if (impl_->transport)
        impl_->transport->close();
    if (impl_->peerSearch)
        impl_->peerSearch->close();
    if (impl_->store)
        impl_->store->close();
    impl_->peerSearch.reset();
    impl_->timer.reset();
    impl_->schedulerActions.reset();
    impl_->store.reset();
    impl_->scheduler.reset();
    impl_->transport.reset();
}

} // namespace server1::policy
