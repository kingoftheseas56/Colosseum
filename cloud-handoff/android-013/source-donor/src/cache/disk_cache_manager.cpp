#include "cache/disk_cache_manager.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace nuvio::cache {
namespace {

constexpr std::size_t maximum_payload_directories = 4096;
constexpr std::size_t maximum_payload_entries = 100'000;

struct Candidate {
    std::string torrent_id;
    std::filesystem::path path;
    std::filesystem::file_time_type last_use;
    std::uint64_t size = 0;
};

bool canonical_torrent_id(const std::string& value) {
    if (value.size() != 40 && value.size() != 64) {
        return false;
    }
    return std::ranges::all_of(value, [](const unsigned char character) {
        return std::isxdigit(character) != 0 &&
            character == static_cast<unsigned char>(std::tolower(character));
    });
}

void saturating_add(std::uint64_t& total, const std::uint64_t value) {
    total = value > std::numeric_limits<std::uint64_t>::max() - total
        ? std::numeric_limits<std::uint64_t>::max()
        : total + value;
}

void increment(std::uint64_t& value) {
    if (value < std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

std::uint64_t stored_file_size(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto handle = CreateFileW(
        path.c_str(),
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        const auto open_error = GetLastError();
        if (open_error == ERROR_FILE_NOT_FOUND || open_error == ERROR_PATH_NOT_FOUND) {
            return 0;
        }
        throw std::system_error(
            static_cast<int>(open_error),
            std::system_category(),
            "open torrent payload entry"
        );
    }
    FILE_STANDARD_INFO information{};
    const auto succeeded = GetFileInformationByHandleEx(
        handle,
        FileStandardInfo,
        &information,
        sizeof(information)
    );
    const auto error = succeeded == 0 ? GetLastError() : ERROR_SUCCESS;
    CloseHandle(handle);
    if (succeeded == 0) {
        throw std::system_error(
            static_cast<int>(error),
            std::system_category(),
            "measure allocated torrent payload entry"
        );
    }
    const auto logical_size = information.EndOfFile.QuadPart > 0
        ? static_cast<std::uint64_t>(information.EndOfFile.QuadPart)
        : 0;
    const auto allocated_size = information.AllocationSize.QuadPart > 0
        ? static_cast<std::uint64_t>(information.AllocationSize.QuadPart)
        : 0;
    return std::min(logical_size, allocated_size);
#else
    struct stat information {};
    if (::stat(path.c_str(), &information) != 0) {
        if (errno == ENOENT) {
            return 0;
        }
        throw std::system_error(
            errno,
            std::generic_category(),
            "measure allocated torrent payload entry"
        );
    }
    constexpr std::uint64_t stat_block_bytes = 512;
    const auto logical_size = information.st_size > 0
        ? static_cast<std::uint64_t>(information.st_size)
        : 0;
    const auto blocks = information.st_blocks > 0
        ? static_cast<std::uint64_t>(information.st_blocks)
        : 0;
    const auto allocated_size =
        blocks > std::numeric_limits<std::uint64_t>::max() / stat_block_bytes
        ? std::numeric_limits<std::uint64_t>::max()
        : blocks * stat_block_bytes;
    return std::min(logical_size, allocated_size);
#endif
}

std::uint64_t directory_size(
    const std::filesystem::path& root,
    std::size_t& inspected_entries
) {
    std::uint64_t size = 0;
    std::error_code error;
    std::filesystem::recursive_directory_iterator entries(
        root,
        std::filesystem::directory_options::none,
        error
    );
    if (error == std::errc::no_such_file_or_directory) {
        return 0;
    }
    if (error) {
        throw std::system_error(error, "scan torrent payload directory");
    }
    for (const auto& entry : entries) {
        if (++inspected_entries > maximum_payload_entries) {
            throw std::runtime_error("torrent payload scan entry limit exceeded");
        }
        const auto status = entry.symlink_status(error);
        if (error) {
            throw std::system_error(error, "inspect torrent payload entry");
        }
        if (status.type() != std::filesystem::file_type::regular) {
            continue;
        }
        saturating_add(size, stored_file_size(entry.path()));
    }
    return size;
}

template <typename Visitor>
void visit_torrent_directories(const std::filesystem::path& root, Visitor&& visit) {
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(root, error);
    if (error == std::errc::no_such_file_or_directory ||
        root_status.type() == std::filesystem::file_type::not_found) {
        return;
    }
    if (error) {
        throw std::system_error(error, "inspect disk cache root");
    }
    if (root_status.type() != std::filesystem::file_type::directory) {
        throw std::runtime_error("disk cache root is not a real directory");
    }
    std::filesystem::directory_iterator entries(root, error);
    if (error) {
        throw std::system_error(error, "scan disk cache root");
    }
    std::size_t inspected_directories = 0;
    for (const auto& entry : entries) {
        if (++inspected_directories > maximum_payload_directories) {
            throw std::runtime_error("disk cache directory limit exceeded");
        }
        const auto status = entry.symlink_status(error);
        if (error) {
            throw std::system_error(error, "inspect disk cache directory");
        }
        const auto id = entry.path().filename().string();
        if (!canonical_torrent_id(id)) {
            continue;
        }
        if (status.type() != std::filesystem::file_type::directory) {
            throw std::runtime_error(
                "canonical disk cache entry is not a real directory"
            );
        }
        visit(id, entry);
    }
}

}

DiskCacheManager::DiskCacheManager(
    std::filesystem::path payload_root,
    const std::uint64_t capacity_bytes
)
    : payload_root_(std::move(payload_root)) {
    stats_.capacity_bytes = capacity_bytes;
}

void DiskCacheManager::touch(const std::string& torrent_id) {
    if (!canonical_torrent_id(torrent_id)) {
        throw std::invalid_argument("disk cache touch requires a canonical torrent ID");
    }
    const auto path = payload_root_ / torrent_id;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
        return;
    }
    if (error) {
        throw std::system_error(error, "inspect disk cache entry");
    }
    if (status.type() != std::filesystem::file_type::directory) {
        throw std::runtime_error("disk cache entry is not a real directory");
    }
    std::filesystem::last_write_time(
        path,
        std::filesystem::file_time_type::clock::now(),
        error
    );
    if (error) {
        throw std::system_error(error, "touch disk cache entry");
    }
}

DiskCacheStats DiskCacheManager::enforce(
    const std::unordered_set<std::string>& protected_torrents
) {
    return enforce(protected_torrents, stats_.capacity_bytes);
}

DiskCacheStats DiskCacheManager::enforce(
    const std::unordered_set<std::string>& protected_torrents,
    const std::uint64_t requested_target_bytes
) {
    const auto target_bytes = std::min(requested_target_bytes, stats_.capacity_bytes);
    std::vector<Candidate> candidates;
    std::unordered_map<std::string, std::uint64_t> sizes;
    std::uint64_t used = 0;
    std::uint64_t protected_bytes = 0;
    std::size_t inspected_entries = 0;
    visit_torrent_directories(
        payload_root_,
        [&](const std::string& id, const std::filesystem::directory_entry& entry) {
            const auto size = directory_size(entry.path(), inspected_entries);
            sizes.emplace(id, size);
            saturating_add(used, size);
            if (protected_torrents.contains(id)) {
                saturating_add(protected_bytes, size);
                return;
            }
            std::error_code error;
            const auto last_use = entry.last_write_time(error);
            if (error) {
                throw std::system_error(error, "inspect disk cache recency");
            }
            candidates.push_back({id, entry.path(), last_use, size});
        }
    );
    measured_sizes_ = std::move(sizes);

    std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
        if (left.last_use != right.last_use) {
            return left.last_use < right.last_use;
        }
        return left.torrent_id < right.torrent_id;
    });
    for (const auto& candidate : candidates) {
        if (used <= target_bytes) {
            break;
        }
        std::error_code error;
        const auto removed = std::filesystem::remove_all(candidate.path, error);
        if (error) {
            throw std::system_error(error, "evict disk cache directory");
        }
        if (removed == 0) {
            continue;
        }
        measured_sizes_.erase(candidate.torrent_id);
        used = candidate.size > used ? 0 : used - candidate.size;
        saturating_add(stats_.reclaimed_bytes, candidate.size);
        increment(stats_.evictions);
    }
    stats_.used_bytes = used;
    stats_.protected_bytes = protected_bytes;
    stats_.over_budget = used > stats_.capacity_bytes;
    return stats_;
}

DiskCacheStats DiskCacheManager::measure(
    const std::unordered_set<std::string>& protected_torrents
) {
    std::unordered_map<std::string, std::uint64_t> sizes;
    std::uint64_t used = 0;
    std::uint64_t protected_bytes = 0;
    std::size_t inspected_entries = 0;
    visit_torrent_directories(
        payload_root_,
        [&](const std::string& id, const std::filesystem::directory_entry& entry) {
            const auto is_protected = protected_torrents.contains(id);
            const auto known = measured_sizes_.find(id);
            const auto size = !is_protected && known != measured_sizes_.end()
                ? known->second
                : directory_size(entry.path(), inspected_entries);
            sizes.emplace(id, size);
            saturating_add(used, size);
            if (is_protected) {
                saturating_add(protected_bytes, size);
            }
        }
    );
    measured_sizes_ = std::move(sizes);
    stats_.used_bytes = used;
    stats_.protected_bytes = protected_bytes;
    stats_.over_budget = used > stats_.capacity_bytes;
    return stats_;
}

const DiskCacheStats& DiskCacheManager::stats() const {
    return stats_;
}

}
