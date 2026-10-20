#pragma once

#include "server1/policy/PieceBuffer.h"

#include <cstddef>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace server1::policy {

struct StoreFile final {
    std::size_t offset = 0;
    std::size_t length = 0;
};

struct VerifyResult final {
    bool complete = false;
    bool success = false;
    std::size_t start = 0;
    std::size_t endExclusive = 0;
};

enum class CommitState { Committed, Queued, Error };

struct CommitResult final {
    CommitState state = CommitState::Error;
    std::size_t callbackCount = 0;
    bool noNotifyHave = false;
    std::string error;
};

class VerificationBitmap final {
public:
    VerificationBitmap(std::size_t count, std::filesystem::path persistPath = {});
    [[nodiscard]] bool get(std::size_t index) const;
    void set(std::size_t index, bool value = true);
    void persist() const;
    void invalidateMissing(const std::vector<bool> &filePresent);

private:
    std::vector<bool> bits_;
    std::filesystem::path persistPath_;
};

class PersistentPieceStore final {
public:
    PersistentPieceStore(std::filesystem::path root, std::size_t pieceLength,
                         std::size_t totalLength, std::size_t verificationLength,
                         std::vector<StoreFile> files,
                         std::vector<std::string> verificationHashes);

    void setDestination(std::size_t fileIndex, std::filesystem::path path);
    [[nodiscard]] std::filesystem::path destination(std::size_t fileIndex) const;
    void stage(std::size_t piece, ByteBuffer bytes);
    [[nodiscard]] std::optional<ByteBuffer> read(std::size_t piece,
                                                  std::string *error = nullptr) const;
    VerifyResult verify(std::size_t piece);
    CommitResult commit(std::size_t start, std::size_t endExclusive);

    void pauseWrites() noexcept;
    void resumeWrites();
    void close();
    void failNextWrite(std::string error,
                       std::size_t successfulWritesBeforeFailure = 0);

    [[nodiscard]] bool isAssembled(std::size_t piece) const;
    [[nodiscard]] bool isVerified(std::size_t piece) const;
    [[nodiscard]] bool isCommitted(std::size_t piece) const;
    [[nodiscard]] bool closeQueued() const noexcept;
    [[nodiscard]] bool closed() const noexcept;
    [[nodiscard]] const std::vector<std::string> &ledger() const noexcept;

private:
    struct PendingCommit final { std::size_t start; std::size_t endExclusive; };
    CommitResult commitNow(std::size_t start, std::size_t endExclusive);
    bool writePiece(std::size_t piece, std::string *error);
    [[nodiscard]] std::size_t pieceCount() const noexcept;
    [[nodiscard]] std::size_t pieceSize(std::size_t piece) const;
    void resetRange(std::size_t start, std::size_t endExclusive);

    std::filesystem::path root_;
    std::size_t pieceLength_ = 0;
    std::size_t totalLength_ = 0;
    std::size_t verificationLength_ = 0;
    std::vector<StoreFile> files_;
    std::vector<std::string> verificationHashes_;
    std::map<std::size_t, std::filesystem::path> destinations_;
    std::vector<std::optional<ByteBuffer>> staged_;
    std::vector<bool> assembled_;
    std::vector<bool> verified_;
    std::vector<bool> committed_;
    std::optional<VerificationBitmap> verificationBitmap_;
    std::vector<PendingCommit> pending_;
    std::optional<std::string> nextWriteError_;
    std::size_t nextWriteFailureCountdown_ = 0;
    std::vector<std::string> ledger_;
    bool paused_ = false;
    bool closeQueued_ = false;
    bool closed_ = false;
};

} // namespace server1::policy
