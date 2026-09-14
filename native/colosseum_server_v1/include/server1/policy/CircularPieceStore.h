#pragma once

#include "server1/policy/PieceStore.h"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace server1::policy {
enum class CircularStoreMode { Memory, Filesystem };
struct CircularWriteResult final { bool success=false; std::optional<std::size_t> resetPiece; std::string error; };
struct CircularCommitResult final {
    bool success = false;
    VerifyResult verification;
    bool noNotifyHave = true;
    std::vector<std::uint64_t> spillTokens;
    std::string error;
};
class CircularPieceStore final {
public:
    using SpillToken=std::uint64_t;
    CircularPieceStore(std::filesystem::path root, CircularStoreMode mode,
                       std::size_t sizeBytes, std::size_t pieceLength);
    [[nodiscard]] std::size_t capacity() const noexcept;
    CircularWriteResult write(std::size_t index, ByteBuffer buffer,
                              const std::set<std::size_t> &selected,
                              const std::set<std::size_t> &locked, std::uint64_t now);
    [[nodiscard]] std::optional<ByteBuffer> read(std::size_t index, std::uint64_t now);
    CircularCommitResult commit(std::size_t start, std::size_t end, bool verificationSuccess=true);
    bool cancelSpill(SpillToken token);
    bool completeSpill(SpillToken token, bool success);
    void close();
    [[nodiscard]] const std::vector<std::size_t> &resetEvents() const noexcept;
private:
    struct Slot final { std::size_t index; std::optional<ByteBuffer> buffer; bool committed;
        bool spilled; std::uint64_t accessedAt; std::uint64_t generation; };
    struct Spill final { std::size_t piece; std::uint64_t generation; bool canceled; };
    [[nodiscard]] Slot *find(std::size_t index);
    [[nodiscard]] std::filesystem::path piecePath(std::size_t piece) const;
    [[nodiscard]] std::string fullError(const std::set<std::size_t> &selected) const;
    std::filesystem::path root_; CircularStoreMode mode_; std::size_t capacity_;
    std::vector<Slot> slots_; std::map<SpillToken,Spill> spills_;
    std::vector<std::size_t> resetEvents_; SpillToken nextToken_=1; bool closed_=false;
};
}
