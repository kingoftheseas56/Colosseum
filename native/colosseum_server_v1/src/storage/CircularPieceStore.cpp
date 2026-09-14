#include "server1/policy/PieceStore.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace server1::policy {

enum class CircularStoreMode {
    Memory,
    Filesystem,
};

struct CircularWriteResult final {
    bool success = false;
    std::optional<std::size_t> resetPiece;
    std::string error;
};

class CircularPieceStore final {
public:
    using SpillToken = std::uint64_t;

    CircularPieceStore(std::filesystem::path root,
                       CircularStoreMode mode,
                       std::size_t sizeBytes,
                       std::size_t pieceLength)
        : root_(std::move(root))
        , mode_(mode)
        , capacity_(pieceLength == 0 ? 0 : sizeBytes / pieceLength)
        , slots_(capacity_)
    {
        if (mode_ == CircularStoreMode::Filesystem && capacity_ != 0) {
            std::filesystem::create_directories(root_ / "pieces");
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    CircularWriteResult write(std::size_t index,
                              ByteBuffer buffer,
                              const std::set<std::size_t> &selected,
                              const std::set<std::size_t> &locked,
                              std::uint64_t now)
    {
        Slot *slot = find(index);
        if (!slot) {
            const auto empty = std::find_if(slots_.begin(), slots_.end(),
                                            [](const Slot &candidate) { return !candidate.buffer; });
            if (empty != slots_.end()) {
                slot = &*empty;
            }
        }

        if (!slot) {
            Slot *oldest = nullptr;
            for (auto &candidate : slots_) {
                if (candidate.buffer && candidate.committed
                    && selected.count(candidate.index) == 0
                    && locked.count(candidate.index) == 0
                    && (!oldest || oldest->accessedAt > candidate.accessedAt)) {
                    oldest = &candidate;
                }
            }
            if (!oldest) {
                return {false, std::nullopt, fullError(selected)};
            }
            const std::size_t reset = oldest->index;
            if (oldest->spilled) {
                std::error_code ignored;
                std::filesystem::remove(piecePath(reset), ignored);
            }
            slot = oldest;
            slot->generation++;
            slot->spilled = false;
            resetEvents_.push_back(reset);
            slot->index = index;
            slot->buffer = std::move(buffer);
            slot->committed = false;
            slot->accessedAt = now;
            return {true, reset, {}};
        }

        slot->generation++;
        slot->index = index;
        slot->buffer = std::move(buffer);
        slot->committed = false;
        slot->spilled = false;
        slot->accessedAt = now;
        return {true, std::nullopt, {}};
    }

    [[nodiscard]] std::optional<ByteBuffer> read(std::size_t index, std::uint64_t now)
    {
        Slot *slot = find(index);
        if (!slot || closed_) {
            return std::nullopt;
        }
        slot->accessedAt = now;
        if (!slot->spilled) {
            return slot->buffer;
        }
        std::ifstream input(piecePath(index), std::ios::binary);
        if (!input) {
            return std::nullopt;
        }
        return ByteBuffer(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    std::vector<SpillToken> commit(std::size_t start, std::size_t end)
    {
        std::vector<SpillToken> result;
        if (start > end) {
            return result;
        }
        for (std::size_t piece = start; piece <= end; ++piece) {
            Slot *slot = find(piece);
            if (!slot) {
                continue;
            }
            slot->committed = true;
            if (mode_ == CircularStoreMode::Filesystem) {
                const SpillToken token = nextToken_++;
                spills_[token] = {piece, slot->generation, false};
                result.push_back(token);
            }
        }
        return result;
    }

    bool cancelSpill(SpillToken token)
    {
        const auto it = spills_.find(token);
        if (it == spills_.end() || it->second.canceled) {
            return false;
        }
        it->second.canceled = true;
        return true;
    }

    bool completeSpill(SpillToken token, bool success)
    {
        const auto it = spills_.find(token);
        if (it == spills_.end()) {
            return false;
        }
        const Spill spill = it->second;
        spills_.erase(it);
        if (!success || spill.canceled || closed_) {
            return false;
        }
        Slot *slot = find(spill.piece);
        if (!slot || slot->generation != spill.generation || !slot->buffer) {
            return false;
        }
        std::ofstream output(piecePath(spill.piece), std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(reinterpret_cast<const char *>(slot->buffer->data()),
                     static_cast<std::streamsize>(slot->buffer->size()));
        if (!output) {
            return false;
        }
        slot->buffer = ByteBuffer{'f', 's'};
        slot->spilled = true;
        return true;
    }

    void close()
    {
        closed_ = true;
        spills_.clear();
        slots_.clear();
    }

    [[nodiscard]] const std::vector<std::size_t> &resetEvents() const noexcept
    {
        return resetEvents_;
    }

private:
    struct Slot final {
        std::size_t index = std::numeric_limits<std::size_t>::max();
        std::optional<ByteBuffer> buffer;
        bool committed = false;
        bool spilled = false;
        std::uint64_t accessedAt = 0;
        std::uint64_t generation = 0;
    };

    struct Spill final {
        std::size_t piece = 0;
        std::uint64_t generation = 0;
        bool canceled = false;
    };

    [[nodiscard]] Slot *find(std::size_t index)
    {
        const auto it = std::find_if(slots_.begin(), slots_.end(), [index](const Slot &slot) {
            return slot.buffer && slot.index == index;
        });
        return it == slots_.end() ? nullptr : &*it;
    }

    [[nodiscard]] std::filesystem::path piecePath(std::size_t piece) const
    {
        return root_ / "pieces" / std::to_string(piece);
    }

    [[nodiscard]] std::string fullError(const std::set<std::size_t> &selected) const
    {
        std::ostringstream message;
        message << "circular buf is full, unable to free; unfreeable pieces: ";
        bool first = true;
        for (const auto &slot : slots_) {
            if (!slot.buffer || (slot.committed && selected.count(slot.index) == 0)) {
                continue;
            }
            if (!first) {
                message << ", ";
            }
            first = false;
            message << slot.index;
            if (!slot.committed) {
                message << "uc";
            }
        }
        return message.str();
    }

    std::filesystem::path root_;
    CircularStoreMode mode_ = CircularStoreMode::Memory;
    std::size_t capacity_ = 0;
    std::vector<Slot> slots_;
    std::map<SpillToken, Spill> spills_;
    std::vector<std::size_t> resetEvents_;
    SpillToken nextToken_ = 1;
    bool closed_ = false;
};

} // namespace server1::policy
