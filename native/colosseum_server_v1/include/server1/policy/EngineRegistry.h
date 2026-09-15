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
#include <vector>

namespace server1::policy {

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
    [[nodiscard]] std::uint64_t timerOwner() const noexcept;
    [[nodiscard]] ports::TransportStatistics transportStatistics() const;
    [[nodiscard]] bool connectSourcePeer(ports::PeerHandle peer,
                                         std::string address,
                                         std::uint16_t port);

private:
    struct Impl;
    explicit TorrentEngine(std::shared_ptr<Impl> impl);

    void resume(Value options);
    std::vector<ports::TorrentObservation> pollTransport();
    bool acceptMetadata(const ports::MetadataReadyObservation &metadata,
                        std::string *error);
    void acceptSourceFailure(const ports::SourceFailureObservation &failure);
    void acceptRuntimeObservation(const ports::TorrentObservation &observation);
    void close();

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

    // poll() translates queued source observations. dispatch() is the explicit
    // next-turn boundary for events and completion callbacks.
    void poll();
    void dispatch();

    [[nodiscard]] std::size_t constructionCount() const noexcept;

private:
    struct Impl;
    static std::shared_ptr<TorrentEngine>
    makeEngine(std::string sourceKey,
               ports::EngineGeneration generation,
               Value options,
               std::filesystem::path cachePath,
               std::unique_ptr<ports::TorrentTransport> transport,
               EnginePost workPost,
               EngineRepeat repeat,
               EngineClock clock);
    std::shared_ptr<Impl> impl_;
};

} // namespace server1::policy
