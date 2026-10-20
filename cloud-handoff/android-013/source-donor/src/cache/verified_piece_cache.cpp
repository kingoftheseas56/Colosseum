#include "cache/verified_piece_cache.hpp"

#include <limits>
#include <utility>

namespace nuvio::cache {

std::size_t PieceCacheKeyHash::operator()(const PieceCacheKey& key) const {
    const auto left = std::hash<std::string>{}(key.torrent_id);
    const auto right = std::hash<std::uint32_t>{}(key.piece);
    return left ^ (
        right + static_cast<std::size_t>(0x9e3779b9U) +
        (left << 6U) + (left >> 2U)
    );
}

VerifiedPieceCache::VerifiedPieceCache(const std::uint64_t capacity_bytes)
    : capacity_bytes_(capacity_bytes) {
}

std::shared_ptr<std::vector<char>> VerifiedPieceCache::get(const PieceCacheKey& key) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        increment(misses_);
        return {};
    }
    recency_.splice(recency_.begin(), recency_, found->second.recency);
    increment(hits_);
    return found->second.data;
}

bool VerifiedPieceCache::contains(const PieceCacheKey& key) const {
    return entries_.contains(key);
}

void VerifiedPieceCache::put(
    PieceCacheKey key,
    std::shared_ptr<std::vector<char>> data
) {
    const auto existing = entries_.find(key);
    if (existing != entries_.end()) {
        erase(existing);
    }
    if (!data || data->empty() || data->size() > capacity_bytes_) {
        return;
    }
    while (!recency_.empty() &&
           used_bytes_ > capacity_bytes_ - data->size()) {
        const auto least_recent = entries_.find(recency_.back());
        if (least_recent == entries_.end()) {
            recency_.pop_back();
            continue;
        }
        erase(least_recent);
        increment(evictions_);
    }
    recency_.push_front(key);
    const auto data_size = static_cast<std::uint64_t>(data->size());
    used_bytes_ += data_size;
    try {
        const auto inserted = entries_.emplace(
            std::move(key),
            Entry{std::move(data), recency_.begin()}
        );
        if (!inserted.second) {
            used_bytes_ -= data_size;
            recency_.pop_front();
        }
    } catch (...) {
        used_bytes_ -= data_size;
        recency_.pop_front();
        throw;
    }
}

void VerifiedPieceCache::erase_torrent(const std::string& torrent_id) {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        if (entry->first.torrent_id == torrent_id) {
            const auto removed = entry++;
            erase(removed);
        } else {
            ++entry;
        }
    }
}

void VerifiedPieceCache::clear() {
    entries_.clear();
    recency_.clear();
    used_bytes_ = 0;
}

PieceCacheStats VerifiedPieceCache::stats() const {
    return {
        capacity_bytes_,
        used_bytes_,
        hits_,
        misses_,
        evictions_,
        static_cast<std::uint64_t>(entries_.size()),
    };
}

void VerifiedPieceCache::erase(
    const std::unordered_map<PieceCacheKey, Entry, PieceCacheKeyHash>::iterator entry
) {
    used_bytes_ -= static_cast<std::uint64_t>(entry->second.data->size());
    recency_.erase(entry->second.recency);
    entries_.erase(entry);
}

void VerifiedPieceCache::increment(std::uint64_t& counter) {
    if (counter < std::numeric_limits<std::uint64_t>::max()) {
        ++counter;
    }
}

}
