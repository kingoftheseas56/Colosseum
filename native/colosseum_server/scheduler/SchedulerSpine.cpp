#include "SchedulerSpine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace colosseum::server::scheduler {

PieceBuffer::PieceBuffer(const std::size_t length)
    : length_(length),
      parts_((length + BlockSize - 1) / BlockSize),
      remainder_(length % BlockSize == 0 ? BlockSize : length % BlockSize),
      missing_(length)
{
    if (length == 0) {
        throw std::invalid_argument("piece length must be non-zero");
    }
}

std::size_t PieceBuffer::blockSize(const std::size_t index) const
{
    if (index >= parts_) {
        throw std::out_of_range("piece block index");
    }
    return index + 1 == parts_ ? remainder_ : BlockSize;
}

std::size_t PieceBuffer::blockOffset(const std::size_t index) const noexcept
{
    return BlockSize * index;
}

void PieceBuffer::ensureInitialized()
{
    if (flushed_ || initialized_) {
        return;
    }
    initialized_ = true;
    blocks_.resize(parts_);
}

int PieceBuffer::reserve()
{
    if (flushed_) {
        return -1;
    }
    ensureInitialized();
    if (!cancellations_.empty()) {
        const auto index = cancellations_.back();
        cancellations_.pop_back();
        return static_cast<int>(index);
    }
    if (reservations_ >= parts_) {
        return -1;
    }
    return static_cast<int>(reservations_++);
}

void PieceBuffer::cancel(const std::size_t index)
{
    if (flushed_) {
        return;
    }
    ensureInitialized();
    if (index >= parts_) {
        throw std::out_of_range("piece cancellation index");
    }
    if (!blocks_[index].has_value()) {
        cancellations_.push_back(index);
    }
}

bool PieceBuffer::set(const std::size_t index,
                      const std::vector<std::byte> &data)
{
    if (flushed_) {
        return false;
    }
    ensureInitialized();
    if (index >= parts_) {
        throw std::out_of_range("piece set index");
    }
    if (data.size() != blockSize(index)) {
        throw std::invalid_argument("piece block has unexpected length");
    }
    if (!blocks_[index].has_value()) {
        blocks_[index] = data;
        ++buffered_;
        missing_ -= data.size();
    }
    return buffered_ == parts_;
}

std::vector<std::byte> PieceBuffer::flush()
{
    if (flushed_ || !initialized_ || buffered_ != parts_) {
        return {};
    }
    std::vector<std::byte> result;
    result.reserve(length_);
    for (const auto &block : blocks_) {
        if (!block.has_value()) {
            return {};
        }
        result.insert(result.end(), block->begin(), block->end());
    }
    result.resize(length_);
    blocks_.clear();
    cancellations_.clear();
    flushed_ = true;
    return result;
}

std::size_t PeerState::activeRequestCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        requests.begin(), requests.end(),
        [](const OutstandingRequest &request) { return !request.completed; }));
}

void FloodPulse::setFlood(const std::uint64_t bytes,
                          const std::uint64_t downloaded) noexcept
{
    floodThreshold_ = bytes + downloaded;
}

void FloodPulse::setPulse(const double bytesPerSecond) noexcept
{
    pulse_ = bytesPerSecond;
}

void FloodPulse::setFloodedPulse(const std::uint64_t bytes,
                                 const double bytesPerSecond,
                                 const std::uint64_t downloaded) noexcept
{
    setFlood(bytes, downloaded);
    setPulse(bytesPerSecond);
}

void FloodPulse::flood() noexcept
{
    floodThreshold_ = 0;
    pulse_ = DisabledPulse;
}

int FloodPulse::updateDelayMs(const std::uint64_t downloaded,
                              const double bytesPerSecond) const noexcept
{
    return downloaded >= floodThreshold_ && bytesPerSecond > pulse_ ? 500 : 0;
}

RechokePolicy::RechokePolicy(const std::size_t slots,
                             OptimisticChooser chooser)
    : slots_(slots), chooser_(std::move(chooser))
{
    if (!chooser_) {
        chooser_ = [](const std::size_t) { return std::size_t{0}; };
    }
}

RechokeResult RechokePolicy::tick(std::vector<PeerState> &peers)
{
    if (optimisticTime_ > 0) {
        --optimisticTime_;
    } else {
        optimisticPeer_.reset();
    }

    RechokeResult result;
    struct Candidate final {
        std::size_t peerIndex = 0;
        double downSpeed = 0.0;
        double upSpeed = 0.0;
        double salt = 0.0;
        bool interested = false;
        bool wasChoked = true;
        bool isChoked = true;
    };

    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < peers.size(); ++i) {
        auto &peer = peers[i];
        if (peer.isSeeder) {
            if (!peer.amChoking) {
                peer.amChoking = true;
                result.choked.push_back(peer.id);
            }
            continue;
        }
        if (optimisticPeer_ && peer.id == *optimisticPeer_) {
            continue;
        }
        candidates.push_back({i,
                              peer.downloadSpeed,
                              peer.uploadSpeed,
                              peer.salt,
                              peer.peerInterested,
                              peer.amChoking,
                              true});
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate &a, const Candidate &b) {
        if (a.downSpeed != b.downSpeed) {
            return a.downSpeed > b.downSpeed;
        }
        if (a.upSpeed != b.upSpeed) {
            return a.upSpeed > b.upSpeed;
        }
        if (a.wasChoked != b.wasChoked) {
            return !a.wasChoked;
        }
        return a.salt < b.salt;
    });

    std::size_t i = 0;
    std::size_t unchokeInterested = 0;
    for (; i < candidates.size() && unchokeInterested < slots_; ++i) {
        candidates[i].isChoked = false;
        if (candidates[i].interested) {
            ++unchokeInterested;
        }
    }

    if (!optimisticPeer_ && i < candidates.size() && slots_ > 0) {
        std::vector<std::size_t> eligible;
        for (std::size_t j = i; j < candidates.size(); ++j) {
            if (candidates[j].interested) {
                eligible.push_back(j);
            }
        }
        if (!eligible.empty()) {
            const auto chosen = chooser_(eligible.size()) % eligible.size();
            auto &optimistic = candidates[eligible[chosen]];
            optimistic.isChoked = false;
            optimisticPeer_ = peers[optimistic.peerIndex].id;
            optimisticTime_ = 2;
        }
    }

    for (auto &candidate : candidates) {
        auto &peer = peers[candidate.peerIndex];
        if (candidate.wasChoked == candidate.isChoked) {
            continue;
        }
        peer.amChoking = candidate.isChoked;
        (candidate.isChoked ? result.choked : result.unchoked).push_back(peer.id);
    }
    result.optimisticPeer = optimisticPeer_;
    return result;
}

SchedulerSpine::SchedulerSpine(std::vector<std::size_t> pieceLengths,
                               PieceMapper pieceMapper,
                               WireMapper wireMapper)
    : pieceLengths_(std::move(pieceLengths)),
      available_(pieceLengths_.size(), false),
      critical_(pieceLengths_.size(), false),
      pieceMapper_(std::move(pieceMapper)),
      wireMapper_(std::move(wireMapper))
{
    if (pieceLengths_.empty()) {
        throw std::invalid_argument("scheduler requires at least one piece");
    }
    if (!pieceMapper_) {
        pieceMapper_ = [](const std::size_t piece) { return piece; };
    }
    if (!wireMapper_) {
        wireMapper_ = [](const std::size_t piece,
                         const std::size_t offset,
                         const std::size_t length) {
            return WireBlock{piece, offset, length};
        };
    }

    pieces_.reserve(pieceLengths_.size());
    blockOwners_.reserve(pieceLengths_.size());
    for (const auto length : pieceLengths_) {
        pieces_.push_back(std::make_unique<PieceBuffer>(length));
        blockOwners_.push_back(
            std::vector<std::optional<std::string>>(pieces_.back()->parts()));
    }
}

int SchedulerSpine::requestTarget(const std::size_t unchokedPeers) noexcept
{
    const double normalized = 1.0 - std::clamp(
        (static_cast<double>(unchokedPeers) - 1.0) / 29.0,
        0.0,
        1.0);
    return static_cast<int>(std::round(
        45.0 * std::pow(normalized, 4.0) + 5.0));
}

double SchedulerSpine::averageBufferProgress(
    const std::vector<SelectionHandle> &selections) noexcept
{
    double total = 0.0;
    std::size_t count = 0;
    for (const auto &selection : selections) {
        if (!selection->readFrom || !selection->selectTo
            || *selection->readFrom == 0 || *selection->selectTo == 0) {
            continue;
        }
        const auto bufferPieces =
            static_cast<double>(*selection->selectTo - *selection->readFrom);
        if (bufferPieces == 0.0) {
            continue;
        }
        total += static_cast<double>(selection->from + selection->offset
                                     - *selection->readFrom)
            / bufferPieces;
        ++count;
    }
    return count == 0 ? 0.0 : total / static_cast<double>(count);
}

bool SchedulerSpine::shouldPauseSwarm(
    const double downloadSpeed,
    const std::size_t unchokedPeers,
    const std::vector<SelectionHandle> &selections,
    const SwarmCapOptions &options) noexcept
{
    bool primaryCondition = true;
    if (options.maxSpeed) {
        primaryCondition = downloadSpeed > *options.maxSpeed;
    }
    if (options.maxBuffer) {
        primaryCondition = averageBufferProgress(selections) > *options.maxBuffer;
    }
    return primaryCondition && unchokedPeers > options.minPeers;
}

bool SchedulerSpine::shouldDestroyChokedPeer(
    const std::size_t queuedPeers,
    const std::size_t swarmSize,
    const std::size_t wireCount,
    const bool amInterested) noexcept
{
    const auto openSlots = swarmSize > wireCount ? swarmSize - wireCount : 0;
    return amInterested && queuedPeers > 2 * openSlots;
}

bool SchedulerSpine::isSeeder(const std::vector<bool> &peerPieces,
                              const std::size_t torrentPieces) noexcept
{
    return peerPieces.size() == torrentPieces
        && std::all_of(peerPieces.begin(), peerPieces.end(),
                       [](const bool piece) { return piece; });
}

SelectionHandle SchedulerSpine::select(const std::size_t from,
                                       const std::size_t to,
                                       const int priority,
                                       std::function<void()> notify)
{
    if (from > to || to >= pieceLengths_.size()) {
        throw std::out_of_range("scheduler selection range");
    }
    auto selection = std::make_shared<Selection>();
    selection->from = from;
    selection->to = to;
    selection->priority = priority;
    selection->notify = std::move(notify);
    selection->serial = nextSelectionSerial_++;
    selections_.push_back(selection);
    std::stable_sort(selections_.begin(), selections_.end(),
                     [](const auto &a, const auto &b) {
        return a->priority > b->priority;
    });
    refresh();
    return selection;
}

SelectionHandle SchedulerSpine::select(const std::size_t from,
                                       const std::size_t to,
                                       const bool priority,
                                       std::function<void()> notify)
{
    return select(from, to, priority ? 1 : 0, std::move(notify));
}

void SchedulerSpine::deselect(const SelectionHandle &selection)
{
    const auto found = std::find(selections_.begin(), selections_.end(), selection);
    if (found == selections_.end()) {
        return;
    }
    selections_.erase(found);
    refresh();
}

void SchedulerSpine::markPieceAvailable(const std::size_t piece)
{
    if (piece >= available_.size()) {
        throw std::out_of_range("scheduler piece index");
    }
    available_[piece] = true;
    critical_[piece] = false;
    pieces_[piece].reset();
    blockOwners_[piece].clear();
    collectGarbage();
}

void SchedulerSpine::resetPiece(const std::size_t piece)
{
    if (piece >= available_.size()) {
        throw std::out_of_range("scheduler piece index");
    }
    available_[piece] = false;
    critical_[piece] = false;
    pieces_[piece] = std::make_unique<PieceBuffer>(pieceLengths_[piece]);
    blockOwners_[piece] =
        std::vector<std::optional<std::string>>(pieces_[piece]->parts());
}

bool SchedulerSpine::isPieceAvailable(const std::size_t piece) const
{
    return piece < available_.size() && available_[piece];
}

bool SchedulerSpine::isSelected(const std::size_t piece) const noexcept
{
    return std::any_of(selections_.begin(), selections_.end(),
                       [piece](const SelectionHandle &selection) {
        return piece <= selection->to
            && selection->from + selection->offset <= piece;
    });
}

void SchedulerSpine::refresh()
{
    collectGarbage();
    updateInterest();
}

void SchedulerSpine::critical(const std::size_t piece, std::size_t width)
{
    if (width == 0) {
        width = 1;
    }
    for (std::size_t i = 0; i < width && piece + i < critical_.size(); ++i) {
        critical_[piece + i] = true;
    }
}

bool SchedulerSpine::isCritical(const std::size_t piece) const noexcept
{
    return piece < critical_.size() && critical_[piece];
}

void SchedulerSpine::lockPiece(const std::size_t piece)
{
    if (piece >= pieceLengths_.size()) {
        throw std::out_of_range("scheduler lock piece index");
    }
    lockedPieces_.push_back(piece);
}

void SchedulerSpine::unlockPiece(const std::size_t piece)
{
    const auto found = std::find(lockedPieces_.begin(), lockedPieces_.end(), piece);
    if (found != lockedPieces_.end()) {
        lockedPieces_.erase(found);
    }
}

bool SchedulerSpine::isPieceLocked(const std::size_t piece) const noexcept
{
    return std::find(lockedPieces_.begin(), lockedPieces_.end(), piece)
        != lockedPieces_.end();
}

void SchedulerSpine::collectGarbage()
{
    for (std::size_t i = 0; i < selections_.size();) {
        const auto &selection = selections_[i];
        const auto oldOffset = selection->offset;
        while (selection->from + selection->offset < selection->to
               && available_[selection->from + selection->offset]) {
            ++selection->offset;
        }
        if (oldOffset != selection->offset && selection->notify) {
            selection->notify();
        }
        const auto current = selection->from + selection->offset;
        if (current == selection->to && available_[current]) {
            const auto notify = selection->notify;
            selections_.erase(selections_.begin()
                              + static_cast<std::ptrdiff_t>(i));
            if (notify) {
                notify();
            }
            updateInterest();
            continue;
        }
        ++i;
    }
    if (selections_.empty() && idleObserver_) {
        idleObserver_();
    }
}

void SchedulerSpine::updateInterest()
{
    const bool next = !selections_.empty();
    if (next == amInterested_) {
        return;
    }
    amInterested_ = next;
    if (interestObserver_) {
        interestObserver_(amInterested_);
    }
}

void SchedulerSpine::shufflePriority(const std::size_t selectionIndex)
{
    if (selectionIndex >= selections_.size()) {
        return;
    }
    std::size_t last = selectionIndex;
    for (std::size_t j = selectionIndex;
         j < selections_.size() && selections_[j]->priority != 0;
         ++j) {
        last = j;
    }
    if (last == selectionIndex) {
        return;
    }
    auto selection = selections_[selectionIndex];
    selections_.erase(selections_.begin()
                      + static_cast<std::ptrdiff_t>(selectionIndex));
    selections_.insert(selections_.begin() + static_cast<std::ptrdiff_t>(last),
                       std::move(selection));
}

std::size_t SchedulerSpine::mappedPiece(const std::size_t streamPiece) const
{
    return pieceMapper_(streamPiece);
}

WireBlock SchedulerSpine::mappedWireBlock(const std::size_t streamPiece,
                                          const std::size_t offset,
                                          const std::size_t length) const
{
    return wireMapper_(streamPiece, offset, length);
}

void SchedulerSpine::upsertPeer(PeerState peerState)
{
    peerState.isSeeder = isSeeder(peerState.peerPieces, pieceLengths_.size());
    if (auto *existing = peer(peerState.id)) {
        auto requests = std::move(existing->requests);
        *existing = std::move(peerState);
        existing->requests = std::move(requests);
        return;
    }
    peers_.push_back(std::move(peerState));
}

PeerState *SchedulerSpine::peer(const std::string &id) noexcept
{
    const auto found = std::find_if(peers_.begin(), peers_.end(),
                                    [&id](const PeerState &candidate) {
        return candidate.id == id;
    });
    return found == peers_.end() ? nullptr : &*found;
}

const PeerState *SchedulerSpine::peer(const std::string &id) const noexcept
{
    const auto found = std::find_if(peers_.begin(), peers_.end(),
                                    [&id](const PeerState &candidate) {
        return candidate.id == id;
    });
    return found == peers_.end() ? nullptr : &*found;
}

std::size_t SchedulerSpine::unchokedPeerCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        peers_.begin(), peers_.end(),
        [](const PeerState &candidate) { return !candidate.peerChoking; }));
}

bool SchedulerSpine::rankAllows(const PeerState &candidate,
                                const std::size_t piece,
                                std::size_t &tries,
                                std::size_t &peerCursor) const
{
    const double speed = candidate.downloadSpeed > 0.0
        ? candidate.downloadSpeed : 1.0;
    if (speed > SpeedThreshold || tries == 0
        || piece >= pieces_.size() || !pieces_[piece]) {
        return true;
    }

    double missing = static_cast<double>(pieces_[piece]->missing());
    const double seconds = static_cast<double>(requestTarget(unchokedPeerCount()))
        * static_cast<double>(PieceBuffer::BlockSize) / speed;
    const auto wirePiece = mappedPiece(piece);
    for (; peerCursor < peers_.size(); ++peerCursor) {
        const auto &other = peers_[peerCursor];
        const bool hasPiece = wirePiece < other.peerPieces.size()
            && other.peerPieces[wirePiece];
        if (other.downloadSpeed >= SpeedThreshold
            && other.downloadSpeed > speed && hasPiece) {
            missing -= other.downloadSpeed * seconds;
            if (missing <= 0.0) {
                --tries;
                return false;
            }
        }
    }
    return true;
}

bool SchedulerSpine::requestBlock(PeerState &target,
                                  const std::size_t piece,
                                  const bool allowHotswap,
                                  std::vector<OutstandingRequest> &created)
{
    if (piece >= pieces_.size() || !pieces_[piece]) {
        return false;
    }
    auto reservation = pieces_[piece]->reserve();
    if (reservation < 0 && allowHotswap && hotswap(target, piece)) {
        reservation = pieces_[piece]->reserve();
    }
    if (reservation < 0) {
        return false;
    }

    const auto block = static_cast<std::size_t>(reservation);
    auto &owners = blockOwners_[piece];
    if (block >= owners.size()) {
        throw std::logic_error("piece reservation exceeds owner table");
    }
    owners[block] = target.id;
    OutstandingRequest request;
    request.id = nextRequestId_++;
    request.streamPiece = piece;
    request.blockIndex = block;
    request.wire = mappedWireBlock(piece,
                                   pieces_[piece]->blockOffset(block),
                                   pieces_[piece]->blockSize(block));
    target.requests.push_back(request);
    created.push_back(request);
    return true;
}

bool SchedulerSpine::hotswap(PeerState &target, const std::size_t piece)
{
    if (target.downloadSpeed < static_cast<double>(PieceBuffer::BlockSize)
        || piece >= pieces_.size() || !pieces_[piece]) {
        return false;
    }

    PeerState *slowest = nullptr;
    double slowestSpeed = std::numeric_limits<double>::infinity();
    for (const auto &owner : blockOwners_[piece]) {
        if (!owner || *owner == target.id) {
            continue;
        }
        auto *candidate = peer(*owner);
        if (!candidate) {
            continue;
        }
        const double speed = candidate->downloadSpeed;
        if (speed < SpeedThreshold
            && 2.0 * speed <= target.downloadSpeed
            && speed <= slowestSpeed) {
            slowest = candidate;
            slowestSpeed = speed;
        }
    }
    if (!slowest) {
        return false;
    }

    std::vector<bool> stolenBlocks(blockOwners_[piece].size(), false);
    for (std::size_t i = 0; i < blockOwners_[piece].size(); ++i) {
        auto &owner = blockOwners_[piece][i];
        if (owner && *owner == slowest->id) {
            owner.reset();
            stolenBlocks[i] = true;
        }
    }

    for (auto &request : slowest->requests) {
        if (request.completed || request.stolen
            || request.streamPiece != piece
            || request.blockIndex >= stolenBlocks.size()
            || !stolenBlocks[request.blockIndex]) {
            continue;
        }
        request.stolen = true;
        pieces_[piece]->cancel(request.blockIndex);
    }

    if (hotswapObserver_) {
        hotswapObserver_(slowest->id, target.id, piece);
    }
    return true;
}

bool SchedulerSpine::trySelect(PeerState &target,
                               const bool allowHotswap,
                               std::vector<OutstandingRequest> &created)
{
    const auto maxRequests =
        static_cast<std::size_t>(requestTarget(unchokedPeerCount()));
    if (target.activeRequestCount() >= maxRequests) {
        return true;
    }

    std::size_t tries = 10;
    std::size_t peerCursor = 0;
    for (std::size_t i = 0; i < selections_.size(); ++i) {
        const auto selection = selections_[i];
        const auto lastPiece = selection->selectTo.value_or(selection->to);
        for (std::size_t piece = selection->from + selection->offset;
             piece <= lastPiece; ++piece) {
            const auto wirePiece = mappedPiece(piece);
            const bool hasPiece = wirePiece < target.peerPieces.size()
                && target.peerPieces[wirePiece];
            if (!hasPiece || !rankAllows(target, piece, tries, peerCursor)) {
                continue;
            }
            while (target.activeRequestCount() < maxRequests
                   && requestBlock(target,
                                   piece,
                                   isCritical(piece) || allowHotswap,
                                   created)) {
            }
            if (target.activeRequestCount() >= maxRequests) {
                if (selection->priority != 0) {
                    shufflePriority(i);
                }
                return true;
            }
        }
    }
    return false;
}

std::vector<OutstandingRequest> SchedulerSpine::updatePeerRequests(
    const std::string &peerId)
{
    auto *target = peer(peerId);
    if (!target) {
        throw std::invalid_argument("unknown scheduler peer");
    }
    std::vector<OutstandingRequest> created;
    if (target->peerChoking) {
        return created;
    }

    if (target->downloaded > 0) {
        if (!trySelect(*target, false, created)) {
            trySelect(*target, true, created);
        }
        return created;
    }

    if (target->activeRequestCount() != 0) {
        return created;
    }

    for (auto selectionIt = selections_.rbegin();
         selectionIt != selections_.rend(); ++selectionIt) {
        const auto &selection = *selectionIt;
        const auto firstPiece = selection->from + selection->offset;
        std::size_t piece = selection->selectTo.value_or(selection->to);
        while (true) {
            const auto wirePiece = mappedPiece(piece);
            const bool hasPiece = wirePiece < target->peerPieces.size()
                && target->peerPieces[wirePiece];
            if (hasPiece && requestBlock(*target, piece, false, created)) {
                return created;
            }
            if (piece == firstPiece) {
                break;
            }
            --piece;
        }
    }
    return created;
}

std::optional<CompletedPiece> SchedulerSpine::completeRequest(
    const std::uint64_t requestId,
    const std::vector<std::byte> &data)
{
    for (auto &ownerPeer : peers_) {
        for (auto &request : ownerPeer.requests) {
            if (request.id != requestId || request.completed) {
                continue;
            }
            request.completed = true;
            const auto piece = request.streamPiece;
            if (piece >= pieces_.size() || !pieces_[piece]) {
                return std::nullopt;
            }
            if (request.blockIndex < blockOwners_[piece].size()) {
                auto &owner = blockOwners_[piece][request.blockIndex];
                if (owner && *owner == ownerPeer.id) {
                    owner.reset();
                }
            }
            const bool ready = pieces_[piece]->set(request.blockIndex, data);
            if (!ready) {
                return std::nullopt;
            }
            auto bytes = pieces_[piece]->flush();
            available_[piece] = true;
            critical_[piece] = false;
            pieces_[piece].reset();
            blockOwners_[piece].clear();
            collectGarbage();
            return CompletedPiece{piece, std::move(bytes)};
        }
    }
    return std::nullopt;
}

void SchedulerSpine::failRequest(const std::uint64_t requestId)
{
    for (auto &ownerPeer : peers_) {
        for (auto &request : ownerPeer.requests) {
            if (request.id != requestId || request.completed) {
                continue;
            }
            request.completed = true;
            const auto piece = request.streamPiece;
            if (piece >= pieces_.size() || !pieces_[piece]) {
                return;
            }
            if (request.blockIndex < blockOwners_[piece].size()) {
                auto &owner = blockOwners_[piece][request.blockIndex];
                if (owner && *owner == ownerPeer.id) {
                    owner.reset();
                }
            }
            pieces_[piece]->cancel(request.blockIndex);
            return;
        }
    }
}

} // namespace colosseum::server::scheduler
