#pragma once

#include "server1/policy/Scheduler.h"
#include "server1/policy/TorrentMetadata.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace server1::policy {

struct FileReadOptions final {
    std::size_t start = 0;
    std::optional<std::size_t> end;
    Value priority = Value::boolean(true);
    std::size_t bufferBytes = 0;
    std::uint64_t generation = 0;
};

class FileReaderSource {
public:
    using Completion = std::function<void(std::uint64_t,
                                          std::size_t,
                                          ByteBuffer,
                                          std::string)>;

    virtual ~FileReaderSource() = default;
    [[nodiscard]] virtual bool hasPiece(std::size_t piece) const = 0;
    virtual void readPiece(std::uint64_t requestToken,
                           std::size_t piece,
                           Completion completion) = 0;
    virtual bool cancelRead(std::uint64_t requestToken) = 0;
};

class FileReader final {
public:
    using Refresh = std::function<void()>;

    FileReader(Scheduler &scheduler,
               FileReaderSource &source,
               TorrentFile file,
               std::size_t pieceLength,
               FileReadOptions options = {},
               Refresh refresh = {});
    ~FileReader();

    FileReader(const FileReader &) = delete;
    FileReader &operator=(const FileReader &) = delete;
    FileReader(FileReader &&) = delete;
    FileReader &operator=(FileReader &&) = delete;

    void request(std::size_t bytes);
    void notifyPiece(std::size_t piece);
    [[nodiscard]] std::vector<ByteBuffer> takeData();
    [[nodiscard]] std::optional<std::string> takeError();
    void close();

    [[nodiscard]] std::size_t length() const noexcept;
    [[nodiscard]] std::size_t startPiece() const noexcept;
    [[nodiscard]] std::size_t endPiece() const noexcept;
    [[nodiscard]] std::size_t bufferPieces() const noexcept;
    [[nodiscard]] std::size_t criticalWidth() const noexcept;
    [[nodiscard]] SelectionId selectionId() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;
    [[nodiscard]] std::size_t pendingReads() const noexcept;
    [[nodiscard]] const std::set<std::size_t> &lockedPieces() const noexcept;
    [[nodiscard]] bool hasActiveSelection() const;
    [[nodiscard]] bool eof() const noexcept;
    [[nodiscard]] bool closed() const noexcept;

private:
    struct State;
    static void pump(const std::shared_ptr<State> &state);
    static void complete(const std::shared_ptr<State> &state,
                         std::uint64_t requestToken,
                         std::size_t piece,
                         ByteBuffer bytes,
                         std::string error);
    static void closeState(const std::shared_ptr<State> &state, bool destroyed);

    std::shared_ptr<State> state_;
};

} // namespace server1::policy
