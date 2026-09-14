#include "server1/policy/Scheduler.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace server1::policy {

Scheduler::Scheduler(std::size_t pieceCount)
    : piecePending_(pieceCount, true)
    , critical_(pieceCount, false)
{
}

double Scheduler::priorityNumber(const Value &priority)
{
    if (priority.kind() == Value::Kind::Boolean) {
        return priority.asBoolean() ? 1.0 : 0.0;
    }
    if (priority.kind() == Value::Kind::Missing || priority.kind() == Value::Kind::Null) {
        return 0.0;
    }
    const double number = jsNumber(priority);
    return std::isnan(number) ? 0.0 : number;
}

SelectionId Scheduler::select(std::size_t from,
                              std::size_t to,
                              const Value &priority,
                              std::optional<std::size_t> selectTo,
                              std::optional<std::size_t> readFrom)
{
    if (from > to || to >= piecePending_.size()) {
        throw std::out_of_range("selection outside piece geometry");
    }
    const std::size_t boundedSelectTo = selectTo.value_or(to);
    const std::size_t boundedReadFrom = readFrom.value_or(from);
    if (boundedReadFrom < from || boundedReadFrom > boundedSelectTo || boundedSelectTo > to) {
        throw std::out_of_range("selection read window outside selection");
    }

    const SelectionId id = nextSelectionId_++;
    selections_.push_back({id,
                           from,
                           to,
                           0,
                           boundedSelectTo,
                           boundedReadFrom,
                           priorityNumber(priority)});
    std::stable_sort(selections_.begin(), selections_.end(), [](const auto &left, const auto &right) {
        return left.priority > right.priority;
    });
    idleEmitted_ = false;
    emitInterestIfChanged(true);
    return id;
}

bool Scheduler::deselect(SelectionId id)
{
    const auto it = std::find_if(selections_.begin(), selections_.end(),
                                 [id](const auto &selection) { return selection.id == id; });
    if (it == selections_.end()) {
        return false;
    }
    selections_.erase(it);
    if (selections_.empty()) {
        emitInterestIfChanged(false);
        if (!idleEmitted_) {
            events_.push_back({SchedulerEventType::Idle, 0});
            idleEmitted_ = true;
        }
    }
    return true;
}

bool Scheduler::updateReadWindow(SelectionId id,
                                 std::size_t readFrom,
                                 std::size_t selectTo)
{
    const auto it = std::find_if(selections_.begin(), selections_.end(),
                                 [id](const auto &selection) { return selection.id == id; });
    if (it == selections_.end() || readFrom < it->from || readFrom > selectTo || selectTo > it->to) {
        return false;
    }
    it->readFrom = readFrom;
    it->selectTo = selectTo;
    return true;
}

const std::vector<SchedulerSelection> &Scheduler::selections() const noexcept
{
    return selections_;
}

std::optional<SchedulerSelection> Scheduler::find(SelectionId id) const
{
    const auto it = std::find_if(selections_.begin(), selections_.end(),
                                 [id](const auto &selection) { return selection.id == id; });
    if (it == selections_.end()) {
        return std::nullopt;
    }
    return *it;
}

void Scheduler::markPieceComplete(std::size_t piece)
{
    if (piece >= piecePending_.size()) {
        throw std::out_of_range("piece outside geometry");
    }
    piecePending_[piece] = false;
}

void Scheduler::resetPiece(std::size_t piece)
{
    if (piece >= piecePending_.size()) {
        throw std::out_of_range("piece outside geometry");
    }
    piecePending_[piece] = true;
    critical_[piece] = false;
}

void Scheduler::collectGarbage()
{
    for (std::size_t index = 0; index < selections_.size();) {
        auto &selection = selections_[index];
        const std::size_t oldOffset = selection.offset;
        while (selection.from + selection.offset < selection.to
               && !piecePending_[selection.from + selection.offset]) {
            ++selection.offset;
        }
        if (oldOffset != selection.offset) {
            events_.push_back({SchedulerEventType::SelectionNotify, selection.id});
        }
        const std::size_t current = selection.from + selection.offset;
        if (current == selection.to && !piecePending_[current]) {
            events_.push_back({SchedulerEventType::SelectionNotify, selection.id});
            selections_.erase(selections_.begin() + static_cast<std::ptrdiff_t>(index));
            emitInterestIfChanged(!selections_.empty());
            continue;
        }
        ++index;
    }
    if (selections_.empty() && !idleEmitted_) {
        events_.push_back({SchedulerEventType::Idle, 0});
        idleEmitted_ = true;
    }
}

std::optional<std::size_t> Scheduler::choosePiece(const std::vector<bool> &peerPieces,
                                                  std::uint64_t downloadedBytes,
                                                  bool requestsEmpty) const
{
    if (downloadedBytes == 0) {
        if (!requestsEmpty) {
            return std::nullopt;
        }
        for (auto selection = selections_.rbegin(); selection != selections_.rend(); ++selection) {
            for (std::size_t piece = selection->selectTo + 1;
                 piece-- > selection->from + selection->offset;) {
                if (piece < peerPieces.size() && peerPieces[piece] && piecePending_[piece]) {
                    return piece;
                }
            }
        }
        return std::nullopt;
    }

    for (const auto &selection : selections_) {
        for (std::size_t piece = selection.from + selection.offset; piece <= selection.selectTo; ++piece) {
            if (piece < peerPieces.size() && peerPieces[piece] && piecePending_[piece]) {
                return piece;
            }
        }
    }
    return std::nullopt;
}

void Scheduler::setCritical(std::size_t piece, std::size_t width)
{
    for (std::size_t offset = 0; offset < width && piece + offset < critical_.size(); ++offset) {
        critical_[piece + offset] = true;
    }
}

bool Scheduler::isCritical(std::size_t piece) const noexcept
{
    return piece < critical_.size() && critical_[piece];
}

std::vector<SchedulerEvent> Scheduler::takeEvents()
{
    std::vector<SchedulerEvent> result;
    result.swap(events_);
    return result;
}

void Scheduler::emitInterestIfChanged(bool interested)
{
    if (interested_ == interested) {
        return;
    }
    interested_ = interested;
    events_.push_back({interested ? SchedulerEventType::Interested
                                  : SchedulerEventType::Uninterested,
                       0});
}

} // namespace server1::policy
