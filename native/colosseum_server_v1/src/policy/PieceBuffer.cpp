#include "server1/policy/PieceBuffer.h"

#include <stdexcept>

namespace server1::policy {

PieceBuffer::PieceBuffer(std::size_t length, std::uint64_t generation)
    : length_(length)
    , parts_((length + kBlockSize - 1) / kBlockSize)
    , remainder_(length % kBlockSize == 0 && length != 0 ? kBlockSize
                                                        : length % kBlockSize)
    , missing_(length)
    , generation_(generation)
{
}

std::size_t PieceBuffer::length() const noexcept { return length_; }
std::size_t PieceBuffer::parts() const noexcept { return parts_; }
std::size_t PieceBuffer::missing() const noexcept { return missing_; }
std::size_t PieceBuffer::buffered() const noexcept { return buffered_; }
std::uint64_t PieceBuffer::generation() const noexcept { return generation_; }
bool PieceBuffer::flushed() const noexcept { return flushed_; }

std::size_t PieceBuffer::size(std::size_t block) const
{
    if (block >= parts_)
        throw std::out_of_range("piece block index");
    return block + 1 == parts_ ? remainder_ : kBlockSize;
}

std::size_t PieceBuffer::offset(std::size_t block) const
{
    if (block >= parts_)
        throw std::out_of_range("piece block index");
    return kBlockSize * block;
}

int PieceBuffer::reserve()
{
    if (!init())
        return kNoReservation;
    if (!cancellations_.empty()) {
        const auto block = cancellations_.back();
        cancellations_.pop_back();
        if (reservationStates_[block] != ReservationState::Canceled)
            return kNoReservation;
        reservationStates_[block] = ReservationState::Reserved;
        return static_cast<int>(block);
    }
    if (reservations_ >= parts_)
        return kNoReservation;
    const auto block = reservations_++;
    reservationStates_[block] = ReservationState::Reserved;
    return static_cast<int>(block);
}

bool PieceBuffer::cancel(std::size_t block)
{
    if (!init() || block >= parts_
        || reservationStates_[block] != ReservationState::Reserved)
        return false;
    reservationStates_[block] = ReservationState::Canceled;
    cancellations_.push_back(block);
    return true;
}

std::optional<ByteBuffer> PieceBuffer::get(std::size_t block)
{
    if (!init() || block >= parts_ || !blocks_[block].has_value())
        return std::nullopt;
    return blocks_[block];
}

bool PieceBuffer::set(std::uint64_t generation, std::size_t block, ByteBuffer data)
{
    if (generation != generation_ || !init() || block >= parts_
        || (reservationStates_[block] != ReservationState::Reserved
            && reservationStates_[block] != ReservationState::Delivered)
        || data.size() != size(block))
        return false;
    if (!blocks_[block].has_value()) {
        missing_ -= data.size();
        ++buffered_;
        blocks_[block] = std::move(data);
        reservationStates_[block] = ReservationState::Delivered;
    }
    return buffered_ == parts_;
}

std::optional<ByteBuffer> PieceBuffer::flush()
{
    if (!initialized_ || buffered_ != parts_)
        return std::nullopt;
    ByteBuffer result;
    result.reserve(length_);
    for (const auto &block : blocks_) {
        if (!block.has_value())
            return std::nullopt;
        result.insert(result.end(), block->begin(), block->end());
    }
    result.resize(length_);
    blocks_.clear();
    cancellations_.clear();
    flushed_ = true;
    initialized_ = false;
    return result;
}

bool PieceBuffer::init()
{
    if (flushed_)
        return false;
    if (!initialized_) {
        blocks_.resize(parts_);
        reservationStates_.assign(parts_, ReservationState::Available);
        cancellations_.clear();
        initialized_ = true;
    }
    return true;
}

} // namespace server1::policy
