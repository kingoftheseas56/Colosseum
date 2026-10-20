#ifndef NUVIO_ENGINE_VERIFIED_PIECE_CACHE_HPP
#define NUVIO_ENGINE_VERIFIED_PIECE_CACHE_HPP

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nuvio::cache {

struct PieceCacheKey {
    std::string torrent_id;
    std::uint32_t piece = 0;

    [[nodiscard]] bool operator==(const PieceCacheKey&) const = default;
};

struct PieceCacheKeyHash {
    [[nodiscard]] std::size_t operator()(const PieceCacheKey& key) const;
};

struct PieceCacheStats {
    std::uint64_t capacity_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t entries = 0;
};

class VerifiedPieceCache {
public:
    explicit VerifiedPieceCache(std::uint64_t capacity_bytes);

    [[nodiscard]] std::shared_ptr<std::vector<char>> get(const PieceCacheKey& key);
    [[nodiscard]] bool contains(const PieceCacheKey& key) const;
    void put(PieceCacheKey key, std::shared_ptr<std::vector<char>> data);
    void erase_torrent(const std::string& torrent_id);
    void clear();

    [[nodiscard]] PieceCacheStats stats() const;

private:
    struct Entry {
        std::shared_ptr<std::vector<char>> data;
        std::list<PieceCacheKey>::iterator recency;
    };

    void erase(std::unordered_map<PieceCacheKey, Entry, PieceCacheKeyHash>::iterator entry);
    static void increment(std::uint64_t& counter);

    const std::uint64_t capacity_bytes_;
    std::uint64_t used_bytes_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
    std::list<PieceCacheKey> recency_;
    std::unordered_map<PieceCacheKey, Entry, PieceCacheKeyHash> entries_;
};

}

#endif
