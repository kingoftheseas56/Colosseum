#ifndef NUVIO_ENGINE_DISK_CACHE_MANAGER_HPP
#define NUVIO_ENGINE_DISK_CACHE_MANAGER_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nuvio::cache {

struct DiskCacheStats {
    std::uint64_t capacity_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t protected_bytes = 0;
    std::uint64_t evictions = 0;
    std::uint64_t reclaimed_bytes = 0;
    bool over_budget = false;
};

class DiskCacheManager {
public:
    DiskCacheManager(std::filesystem::path payload_root, std::uint64_t capacity_bytes);

    void touch(const std::string& torrent_id);
    [[nodiscard]] DiskCacheStats enforce(
        const std::unordered_set<std::string>& protected_torrents
    );
    [[nodiscard]] DiskCacheStats enforce(
        const std::unordered_set<std::string>& protected_torrents,
        std::uint64_t target_bytes
    );
    [[nodiscard]] DiskCacheStats measure(
        const std::unordered_set<std::string>& protected_torrents
    );
    [[nodiscard]] const DiskCacheStats& stats() const;

private:
    std::filesystem::path payload_root_;
    DiskCacheStats stats_;
    std::unordered_map<std::string, std::uint64_t> measured_sizes_;
};

}

#endif
