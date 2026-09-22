#pragma once

#include "server1/policy/FileReader.h"
#include "server1/policy/Value.h"
#include "server1/ports/TorrentTransport.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace server1::policy {

// Threading contract.
//
// The app lane is the thread that constructs the registry and calls create,
// remove, get, exists, list, poll and dispatch. Every EngineEvent, create/remove
// completion, TorrentEngine method, FileReader method and FileReaderSource
// completion runs on that lane. When callbackExecutor is set it must post to the
// app lane; otherwise dispatch() is the explicit next-turn boundary.
//
// Peer and disk work runs only on a per-engine serialized work lane: transport
// open, connect and close; metadata install and the persistent restore scan;
// stage, verify and commit; cache and upload reads; store close. workExecutor
// supplies the work lane when it accepts a task without running it inline and
// does not run it on the app thread. Otherwise the registry uses its own worker
// thread. Work never runs on the app lane, and its results re-enter the app lane
// through the callback boundary.
//
// Terminal completions (create callbacks and remove callbacks) are delivered
// exactly once through the callback boundary. Registry destruction posts the
// remaining terminal completions through callbackExecutor. Without an executor,
// destruction on the app lane is the final dispatch turn: terminal completions
// are delivered after all registry state is torn down. A destroying thread that
// is not the app lane never runs terminal callbacks.

using EngineRequestToken = std::uint64_t;

struct EngineCreateRequest final {
    std::string sourceKey;
    Value options = Value::object({});
};

enum class EngineEventType {
    Create,
    Created,
    ScopedReady,
    Ready,
    ScopedError,
    Error,
    Destroyed,
};

struct EngineEvent final {
    EngineEventType type = EngineEventType::Create;
    std::string sourceKey;
    ports::EngineGeneration generation = 0;
    Value options = Value::missing();
    std::string error;
};

enum class EngineCreateOutcome {
    Ready,
    SourceError,
    Removed,
    Cancelled,
    InvalidSource,
};

class TorrentEngine;

struct EngineCreateResult final {
    EngineCreateOutcome outcome = EngineCreateOutcome::Cancelled;
    std::shared_ptr<TorrentEngine> engine;
    std::string error;
};

// Diagnostic observation of engine-internal work. kind is one of: open,
// metadata-install, restore, stage, verify-incomplete, verify-success,
// verify-failure, commit, read, upload-read, connect, transport-close,
// store-close, timer-start, timer-tick, timer-cancel, timer-tick-ignored.
// It may be invoked on the work lane or the app lane and must not reenter the
// registry.
struct EngineTrace final {
    std::string sourceKey;
    ports::EngineGeneration generation = 0;
    std::string kind;
    std::uint64_t piece = 0;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::uint64_t length = 0;
    std::thread::id thread;
};

using EngineContinuation = std::function<void()>;
using EnginePost = std::function<bool(EngineContinuation)>;
using EngineClock = std::function<std::uint64_t()>;

class EngineTimer {
public:
    virtual ~EngineTimer() = default;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual bool active() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t id() const noexcept = 0;
};

using EngineRepeat = std::function<std::shared_ptr<EngineTimer>(
    std::uint64_t intervalMs, EngineContinuation)>;
using BeforeCreateEngine =
    std::function<void(const EngineCreateRequest &, EngineContinuation)>;
using EngineCreateCallback = std::function<void(EngineCreateResult)>;
using EngineRemoveCallback = std::function<void(bool)>;
using EngineEventCallback = std::function<void(const EngineEvent &)>;
using EngineTraceCallback = std::function<void(const EngineTrace &)>;
using EngineTransportFactory =
    std::function<std::unique_ptr<ports::TorrentTransport>(
        const ports::TorrentOpenRequest &)>;

struct EngineRegistryConfig final {
    std::filesystem::path cacheRoot;
    BeforeCreateEngine beforeCreate;
    EngineEventCallback onEvent;
    EngineTransportFactory transportFactory;
    EnginePost workExecutor;
    EnginePost callbackExecutor;
    EngineRepeat repeat;
    EngineClock monotonicClock;
    EngineTraceCallback trace;
};

// Peer-discovery accounting for M612 "peer" results. queued counts peers whose
// ConnectAction was issued but that have not yet been observed on a wire.
struct EnginePeerDiscovery final {
    std::size_t queued = 0;
    std::size_t connectsSubmitted = 0;
    std::size_t connectsRejected = 0;
    std::size_t malformedRejected = 0;
    std::size_t duplicatesSuppressed = 0;
};

class TorrentEngine final {
public:
    ~TorrentEngine();

    TorrentEngine(const TorrentEngine &) = delete;
    TorrentEngine &operator=(const TorrentEngine &) = delete;
    TorrentEngine(TorrentEngine &&) = delete;
    TorrentEngine &operator=(TorrentEngine &&) = delete;

    [[nodiscard]] std::string sourceKey() const;
    [[nodiscard]] ports::EngineGeneration generation() const noexcept;
    [[nodiscard]] Value options() const;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool failed() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] std::string sourceError() const;
    [[nodiscard]] std::size_t resumeCount() const noexcept;
    [[nodiscard]] std::size_t fileCount() const noexcept;
    [[nodiscard]] std::vector<TorrentFile> files() const;
    [[nodiscard]] std::shared_ptr<FileReader>
    createReader(std::size_t fileIndex, FileReadOptions options = {});
    [[nodiscard]] std::size_t readerCount() const noexcept;
    [[nodiscard]] std::size_t selectionCount() const noexcept;
    [[nodiscard]] bool hasPersistentStore() const noexcept;
    [[nodiscard]] bool hasCircularStore() const noexcept;
    [[nodiscard]] bool hasPeerSearch() const noexcept;
    [[nodiscard]] bool peerSearchRunning() const noexcept;
    [[nodiscard]] std::vector<discovery::PeerSourceStats> peerSearchStats() const;
    [[nodiscard]] EnginePeerDiscovery peerDiscovery() const noexcept;
    [[nodiscard]] std::uint64_t timerOwner() const noexcept;
    [[nodiscard]] ports::TransportStatistics transportStatistics() const;
    // Admits a caller-owned connect to the work lane. Returns false when the
    // engine is closed or the endpoint is invalid.
    [[nodiscard]] bool connectSourcePeer(ports::PeerHandle peer,
                                         std::string address,
                                         std::uint16_t port);
    // A peer-search source result, the translation of M612's per-source "peer"
    // event. Returns false when the engine is closed, has no peer search or the
    // source index is out of range. Malformed and duplicate results are
    // counted in peerDiscovery() and never become ConnectActions.
    bool discoverPeer(std::size_t sourceIndex, std::string address);

private:
    struct Impl;
    struct Wiring final {
        std::string sourceKey;
        ports::EngineGeneration generation = 0;
        Value options = Value::object({});
        std::filesystem::path cachePath;
        ports::V1InfoHash infoHash{};
        ports::TorrentSource source;
        EngineTransportFactory transportFactory;
        EnginePost workPost;
        EnginePost appPost;
        EngineRepeat repeat;
        EngineClock clock;
        EngineTraceCallback trace;
        std::function<void()> onReady;
        std::function<void()> onFailed;
    };
    explicit TorrentEngine(std::shared_ptr<Impl> impl);
    static std::shared_ptr<TorrentEngine> make(Wiring wiring);

    void resume(Value options);
    void pollTransport();
    void close(std::function<void()> onClosed = {});

    std::shared_ptr<Impl> impl_;
    friend class EngineRegistry;
};

class EngineRegistry final {
public:
    explicit EngineRegistry(EngineRegistryConfig config = {});
    ~EngineRegistry();

    EngineRegistry(const EngineRegistry &) = delete;
    EngineRegistry &operator=(const EngineRegistry &) = delete;
    EngineRegistry(EngineRegistry &&) = delete;
    EngineRegistry &operator=(EngineRegistry &&) = delete;

    EngineRequestToken create(EngineCreateRequest request,
                              EngineCreateCallback callback = {});
    [[nodiscard]] std::shared_ptr<TorrentEngine>
    get(const std::string &sourceKey) const;
    [[nodiscard]] bool exists(const std::string &sourceKey) const;
    bool remove(const std::string &sourceKey,
                EngineRemoveCallback callback = {});
    [[nodiscard]] std::vector<std::string> list() const;

    // poll() drains transport mailboxes and translates source observations on
    // the app lane. dispatch() runs the app-lane tasks queued before the call:
    // events, completion callbacks and work-lane results.
    void poll();
    void dispatch();

    [[nodiscard]] std::size_t constructionCount() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace server1::policy
