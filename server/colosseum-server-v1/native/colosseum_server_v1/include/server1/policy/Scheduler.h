#pragma once

#include "server1/policy/Value.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace server1::policy {

using SelectionId = std::uint64_t;

struct SchedulerSelection final {
    SelectionId id = 0;
    std::size_t from = 0;
    std::size_t to = 0;
    std::size_t offset = 0;
    std::size_t selectTo = 0;
    std::size_t readFrom = 0;
    double priority = 0.0;
};

enum class SchedulerEventType {
    Interested,
    Uninterested,
    SelectionNotify,
    Idle,
};

struct SchedulerEvent final {
    SchedulerEventType type = SchedulerEventType::Idle;
    SelectionId selectionId = 0;
};

struct HotswapCandidate final {
    std::size_t reservation = 0;
    double bytesPerSecond = 0.0;
    bool active = true;
};

enum class PulseDisposition {
    Immediate,
    Debounced,
};

class Scheduler final {
public:
    explicit Scheduler(std::size_t pieceCount);

    [[nodiscard]] SelectionId select(std::size_t from,
                                     std::size_t to,
                                     const Value &priority,
                                     std::optional<std::size_t> selectTo = std::nullopt,
                                     std::optional<std::size_t> readFrom = std::nullopt);
    [[nodiscard]] bool deselect(SelectionId id);
    [[nodiscard]] bool updateReadWindow(SelectionId id,
                                        std::size_t readFrom,
                                        std::size_t selectTo);
    [[nodiscard]] const std::vector<SchedulerSelection> &selections() const noexcept;
    [[nodiscard]] std::optional<SchedulerSelection> find(SelectionId id) const;
    [[nodiscard]] bool rotatePriorityAfterBudgetFill(SelectionId id);

    void markPieceComplete(std::size_t piece);
    void resetPiece(std::size_t piece);
    void collectGarbage();
    [[nodiscard]] std::optional<std::size_t> choosePiece(const std::vector<bool> &peerPieces,
                                                         std::uint64_t downloadedBytes,
                                                         bool requestsEmpty) const;

    void setCritical(std::size_t piece, std::size_t width = 1);
    [[nodiscard]] bool isCritical(std::size_t piece) const noexcept;
    [[nodiscard]] std::vector<SchedulerEvent> takeEvents();

    [[nodiscard]] static int requestBudget(std::size_t unchokedPeers) noexcept;
    [[nodiscard]] static std::optional<std::size_t>
    hotswapVictim(double requesterBytesPerSecond,
                  const std::vector<HotswapCandidate> &candidates) noexcept;
    [[nodiscard]] static PulseDisposition pulseDisposition(std::uint64_t downloadedBytes,
                                                            std::uint64_t floodAt,
                                                            double aggregateBytesPerSecond,
                                                            double pulseBytesPerSecond) noexcept;

private:
    [[nodiscard]] static double priorityNumber(const Value &priority);
    void emitInterestIfChanged(bool interested);

    std::vector<SchedulerSelection> selections_;
    std::vector<bool> piecePending_;
    std::vector<bool> critical_;
    std::vector<SchedulerEvent> events_;
    SelectionId nextSelectionId_ = 1;
    bool interested_ = false;
    bool idleEmitted_ = false;
};

} // namespace server1::policy
