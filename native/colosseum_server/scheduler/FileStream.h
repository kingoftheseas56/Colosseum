#pragma once

#include "SchedulerSpine.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace colosseum::server::scheduler {

class IFilePieceStore {
public:
    using ReadToken = std::uint64_t;
    using ReadCallback = std::function<void(std::error_code,
                                            std::vector<std::byte>)>;

    virtual ~IFilePieceStore() = default;
    virtual ReadToken readPiece(std::size_t piece, ReadCallback callback) = 0;
    virtual void cancelRead(ReadToken token) = 0;
};

struct FileSpan final {
    std::size_t offset = 0;
    std::size_t length = 0;
};

struct FileStreamOptions final {
    std::size_t start = 0;
    std::optional<std::size_t> end;
    std::optional<bool> priority;
    std::optional<std::size_t> bufferBytes;
};

class FileStream final {
public:
    using ChunkObserver = std::function<void(const std::vector<std::byte> &)>;
    using ErrorObserver = std::function<void(const std::error_code &)>;
    using EndObserver = std::function<void()>;

    FileStream(SchedulerSpine &scheduler,
               IFilePieceStore &store,
               FileSpan file,
               std::size_t pieceLength,
               FileStreamOptions options = {});
    ~FileStream();

    FileStream(const FileStream &) = delete;
    FileStream &operator=(const FileStream &) = delete;

    void start();
    void notify();
    void destroy();

    void setChunkObserver(ChunkObserver observer)
    {
        chunkObserver_ = std::move(observer);
    }
    void setErrorObserver(ErrorObserver observer)
    {
        errorObserver_ = std::move(observer);
    }
    void setEndObserver(EndObserver observer)
    {
        endObserver_ = std::move(observer);
    }

    [[nodiscard]] std::size_t length() const noexcept { return length_; }
    [[nodiscard]] std::size_t startPiece() const noexcept { return startPiece_; }
    [[nodiscard]] std::size_t endPiece() const noexcept { return endPiece_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return remaining_; }
    [[nodiscard]] std::size_t inFlight() const noexcept { return inFlight_; }
    [[nodiscard]] bool waiting() const noexcept { return waiting_; }
    [[nodiscard]] bool ended() const noexcept { return ended_; }
    [[nodiscard]] bool destroyed() const noexcept { return destroyed_; }
    [[nodiscard]] const SelectionHandle &selection() const noexcept
    {
        return selection_;
    }

private:
    static constexpr std::size_t MaxReadsInFlight = 2;

    void pump();
    void scheduleRead(std::size_t piece);
    void updateMovingWindow();
    void drainOrdered();
    void fail(std::error_code error);
    void finish();
    [[nodiscard]] std::function<void()> notifyCallback();

    SchedulerSpine &scheduler_;
    IFilePieceStore &store_;
    FileSpan file_;
    std::size_t pieceLength_ = 0;
    std::size_t length_ = 0;
    std::size_t startPiece_ = 0;
    std::size_t endPiece_ = 0;
    std::size_t nextPiece_ = 0;
    std::size_t emitPiece_ = 0;
    std::size_t remaining_ = 0;
    std::size_t initialOffset_ = 0;
    std::size_t criticalWidth_ = 0;
    std::optional<std::size_t> bufferPieces_;
    std::size_t inFlight_ = 0;
    bool started_ = false;
    bool pumping_ = false;
    bool waiting_ = false;
    bool ended_ = false;
    bool destroyed_ = false;

    SelectionHandle selection_;
    std::vector<SelectionHandle> recoverySelections_;
    std::vector<std::size_t> inFlightPieces_;
    std::map<IFilePieceStore::ReadToken, std::size_t> readTokens_;
    std::map<std::size_t, std::vector<std::byte>> pendingPieces_;
    ChunkObserver chunkObserver_;
    ErrorObserver errorObserver_;
    EndObserver endObserver_;
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};

} // namespace colosseum::server::scheduler
