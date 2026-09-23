#pragma once

#include "server1/policy/EngineRegistry.h"
#include "server1/policy/TorrentMetadata.h"
#include "server1/policy/Value.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// K12 EngineLifecycleContract: the EngineFS counters, stream cache events,
// statistics, removal and keepConcurrency of M172 (with M662 Counter), and the
// M564 entrypoint timeout values. Everything here runs on the registry's app
// lane; nothing blocks.
namespace server1::policy {

// M172 L18112 defaults, replaced by the M564 entrypoint (L46884) before any
// counter fires. Counters read the value when a count reaches zero, as the
// source's timeout() callbacks do.
constexpr std::uint64_t kSourceStreamTimeoutMs = 30000;
constexpr std::uint64_t kSourceEngineTimeoutMs = 60000;
constexpr std::uint64_t kEntrypointStreamTimeoutMs = 20000;
constexpr std::uint64_t kEntrypointEngineTimeoutMs = 120000;

// A one-shot timer (the source's setTimeout). The continuation must not run
// after the returned timer is cancelled.
using LifecycleTimeout = std::function<std::shared_ptr<EngineTimer>(
    std::uint64_t delayMs, EngineContinuation)>;

// One EngineFS emit: the event name as emitted and its arguments. Emit(args)
// in M172 produces two of these: the plain name with arguments, then the
// colon-joined name with none.
struct LifecycleEvent final {
    std::string name;
    Value::Array args;
};

// M662 Counter. Ids are strings; counts follow JavaScript arithmetic, so a
// decrement of an unknown id stores NaN and wedges that id, and a count can go
// negative. Both are source behaviour and are kept.
class EngineCounter final {
public:
    using IdFn = std::function<std::string(const std::string &hash, std::size_t idx)>;
    using Hook = std::function<void(const std::string &hash, std::size_t idx)>;
    using TimeoutFn = std::function<std::uint64_t()>;

    struct Slot final {
        bool present = false;
        bool nan = false;
        std::int64_t count = 0;
        bool timerPending = false;
    };

    EngineCounter(IdFn id, Hook onPositive, Hook onZero, TimeoutFn timeout,
                  LifecycleTimeout setTimeout);
    ~EngineCounter();
    EngineCounter(const EngineCounter &) = delete;
    EngineCounter &operator=(const EngineCounter &) = delete;

    void increment(const std::string &hash, std::size_t idx);
    void decrement(const std::string &hash, std::size_t idx);
    [[nodiscard]] Slot slot(const std::string &id) const;
    // Cancels every pending timer without firing it (teardown).
    void cancelAll() noexcept;

private:
    struct Entry;
    IdFn id_;
    Hook onPositive_;
    Hook onZero_;
    TimeoutFn timeout_;
    LifecycleTimeout setTimeout_;
    std::shared_ptr<std::map<std::string, Entry>> entries_;
};

// What the M172 stream-open handler reads from an engine once it is ready.
struct LifecycleTorrentView final {
    std::int64_t pieceLength = 0;
    std::optional<std::int64_t> realPieceLength;
    std::optional<std::int64_t> verificationLen;
    std::vector<TorrentFile> files;
};

// The engine surface the stream-open handler needs. ready() defers its
// continuation to a later turn when metadata is present (M814 uses
// process.nextTick) and otherwise until metadata arrives.
class LifecycleEngine {
public:
    virtual ~LifecycleEngine() = default;
    [[nodiscard]] virtual std::uint64_t instance() const noexcept = 0;
    virtual void ready(EngineContinuation continuation) = 0;
    [[nodiscard]] virtual const LifecycleTorrentView *torrent() const = 0;
    [[nodiscard]] virtual bool havePiece(std::int64_t piece) const = 0;
    [[nodiscard]] virtual bool buffer() const = 0;
    // e.store.getDest(fileIndex); nullopt when the store has no getDest.
    [[nodiscard]] virtual std::optional<std::string> storeDest(std::size_t fileIndex) const = 0;
    // e.select(from, to, false)
    virtual void select(double from, double to) = 0;
};

// The engine table as M172 sees it. Keys are lowercase infohashes.
class EngineCatalog {
public:
    virtual ~EngineCatalog() = default;
    // Object.keys(engines): creation order.
    [[nodiscard]] virtual std::vector<std::string> list() const = 0;
    [[nodiscard]] virtual bool exists(const std::string &hash) const = 0;
    [[nodiscard]] virtual std::size_t selectionCount(const std::string &hash) const = 0;
    // destroy, then delete; done runs after deletion.
    virtual void destroy(const std::string &hash, std::function<void()> done) = 0;
    // nullptr when the engine is unknown or exposes no lifecycle surface.
    [[nodiscard]] virtual LifecycleEngine *engine(const std::string &hash) = 0;
};

// EngineCatalog over the K11 EngineRegistry. Creation order is not available
// from EngineRegistry::list() (it is sorted), so the composer forwards every
// EngineEvent to observe(). engine() returns nullptr: TorrentEngine exposes no
// torrent view, bitfield, verify events or select (see WIRING-REQUEST.json).
class RegistryEngineCatalog final : public EngineCatalog {
public:
    explicit RegistryEngineCatalog(EngineRegistry &registry);
    void observe(const EngineEvent &event);

    [[nodiscard]] std::vector<std::string> list() const override;
    [[nodiscard]] bool exists(const std::string &hash) const override;
    [[nodiscard]] std::size_t selectionCount(const std::string &hash) const override;
    void destroy(const std::string &hash, std::function<void()> done) override;
    [[nodiscard]] LifecycleEngine *engine(const std::string &hash) override;

private:
    EngineRegistry &registry_;
    std::vector<std::string> order_;
};

struct EngineLifecycleConfig final {
    std::uint64_t streamTimeoutMs = kEntrypointStreamTimeoutMs;
    std::uint64_t engineTimeoutMs = kEntrypointEngineTimeoutMs;
    LifecycleTimeout setTimeout;
    std::function<void(const LifecycleEvent &)> onEvent;
    // Deferred work the source runs on a later turn (a pending keepConcurrency
    // resolution). Runs synchronously when unset.
    EnginePost nextTurn;
};

// The response side of one stream: finish and close both emit stream-close,
// once in total.
class LifecycleStream final {
public:
    LifecycleStream() = default;
    void finish();
    void close();
    [[nodiscard]] bool closed() const noexcept;

private:
    friend class EngineLifecycle;
    std::function<void()> emitClose_;
    bool closed_ = true;
};

class EngineLifecycle final {
public:
    EngineLifecycle(EngineCatalog &catalog, EngineLifecycleConfig config);
    ~EngineLifecycle();
    EngineLifecycle(const EngineLifecycle &) = delete;
    EngineLifecycle &operator=(const EngineLifecycle &) = delete;

    // EngineFS.STREAM_TIMEOUT / ENGINE_TIMEOUT assignment.
    void setStreamTimeoutMs(std::uint64_t value) noexcept;
    void setEngineTimeoutMs(std::uint64_t value) noexcept;
    [[nodiscard]] std::uint64_t streamTimeoutMs() const noexcept;
    [[nodiscard]] std::uint64_t engineTimeoutMs() const noexcept;

    // EngineFS.emit("stream-open", hash, idx) from the stream route, and the
    // matching close guard.
    [[nodiscard]] LifecycleStream openStream(const std::string &hash, std::size_t idx);
    // The engine emitted "verify" for a piece.
    void verify(const std::string &hash, std::int64_t piece);
    // The engine left the table; its per-file cache listeners go with it.
    void forget(const std::string &hash);

    // removeEngine(ih, cb): immediate callback when the engine is unknown.
    void remove(const std::string &hash, std::function<void()> done = {});
    // GET /removeAll: removes every listed engine without waiting.
    void removeAll();
    // EngineFS.keepConcurrency(hash, concurrency). done runs when the returned
    // promise would resolve: at once when nothing is removed, otherwise when
    // the last-indexed removal completes.
    void keepConcurrency(const std::string &hash, const Value &concurrency,
                         std::function<void()> done);

    [[nodiscard]] EngineCounter::Slot streamSlot(const std::string &hash, std::size_t idx) const;
    [[nodiscard]] EngineCounter::Slot engineSlot(const std::string &hash) const;
    [[nodiscard]] EngineCounter::Slot idleSlot(const std::string &hash) const;
    [[nodiscard]] std::size_t pendingCacheTrackers() const noexcept;
    // file.__cacheEvents: set on the torrent file object by the first
    // stream-open, and therefore visible in that engine's statistics.
    [[nodiscard]] bool cacheEventsEmitted(const std::string &hash, std::size_t idx) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

// ---- statistics (M172 getStatistics) ----

struct EngineWireRecord final {
    bool peerChoking = true;
    std::size_t requests = 0;
    Value address = Value::missing();
    bool amInterested = false;
    bool isSeeder = false;
    double downSpeed = 0.0;
    double upSpeed = 0.0;
};

// The engine state getStatistics reads. Fields the source reads through
// possibly-undefined properties are Values so absent keys stay absent.
struct EngineStatsSnapshot final {
    std::string infoHash;
    // e.torrent: absent => name and files serialize as null.
    bool hasTorrent = false;
    Value torrentName = Value::missing();
    Value torrentFiles = Value::missing();
    std::int64_t pieceLength = 0;
    std::vector<TorrentFile> fileGeometry;
    std::vector<bool> have;
    std::vector<EngineWireRecord> wires;
    Value queued = Value::missing();
    std::size_t uniquePeers = 0;
    Value connectionTries = Value::missing();
    bool swarmPaused = false;
    std::size_t swarmConnections = 0;
    Value swarmSize = Value::missing();
    Value selections = Value::array({});
    Value downloaded = Value::missing();
    Value uploaded = Value::missing();
    double swarmDownloadSpeed = 0.0;
    // Read but never serialized: the source assigns downloadSpeed() to both
    // downloadSpeed and uploadSpeed.
    double swarmUploadSpeed = 0.0;
    bool hasPeerSearch = false;
    Value peerSearchStats = Value::array({});
    bool peerSearchRunning = false;
    Value options = Value::missing();
};

// JSON.stringify(getStatistics(e, idx)). snapshot == nullptr is an unknown
// engine ("null"). idx == nullptr is an undefined index; any other Value is
// the route or API index. extras are appended after the source keys (the
// /create route adds guessedFileIdx).
[[nodiscard]] std::string serializeEngineStatistics(
    const EngineStatsSnapshot *snapshot, const Value *idx = nullptr,
    const Value::Object &extras = {});
// JSON.stringify of the GET /stats.json body: sys (when given) then every
// engine in creation order.
[[nodiscard]] std::string serializeAllEngineStatistics(
    const std::vector<std::pair<std::string, const EngineStatsSnapshot *>> &engines,
    const Value *sys = nullptr);

// JavaScript Number.prototype.toString() and JSON.stringify.
[[nodiscard]] std::string jsNumberToString(double value);
[[nodiscard]] std::string jsJsonStringify(const Value &value);

} // namespace server1::policy
