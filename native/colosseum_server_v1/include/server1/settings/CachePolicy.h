#pragma once

#include "server1/platform/DiskSpace.h"
#include "server1/settings/SettingsStore.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace server1::settings {

struct EngineDefaults final {
    std::uint32_t connections {0};
    std::uint64_t readerWindowBytes {0};
    std::uint64_t circularStorageBytes {0};
    bool circularStorageInMemory {false};
    double maxBufferFraction {0.0};
};

[[nodiscard]] EngineDefaults effectiveEngineDefaults(const SettingsStore &settings);

} // namespace server1::settings

namespace server1::cache {

struct CacheEntry final {
    std::filesystem::path path;
    std::uintmax_t size {0};
    std::int64_t accessTime {0};
    bool activeEngine {false};
};

struct CacheTrimResult final {
    std::uintmax_t current {0};
    double target {0.0};
    std::size_t deleted {0};
    std::vector<std::filesystem::path> deletedPaths;
};

[[nodiscard]] std::vector<CacheEntry> enumerate(
    const std::filesystem::path &cachePath,
    const std::vector<std::string> &activeEngineIds);

[[nodiscard]] std::vector<std::filesystem::path> planDeletions(
    std::vector<CacheEntry> entries,
    double toSize,
    std::optional<std::uintmax_t> freeSpace = std::nullopt,
    std::uintmax_t requiredSize = 0);

[[nodiscard]] CacheTrimResult clearCache(
    const std::filesystem::path &cachePath,
    double toSize,
    std::uintmax_t requiredSize,
    const std::vector<std::string> &activeEngineIds,
    std::optional<server1::platform::DiskSpace> space = std::nullopt,
    bool querySystemDiskSpace = true);

[[nodiscard]] std::filesystem::path cachePath(const std::filesystem::path &cacheRoot,
                                               std::string_view key);

class CleanupDebouncer final {
public:
    using Callback = std::function<void(double)>;

    explicit CleanupDebouncer(Callback callback,
                              std::chrono::milliseconds delay = std::chrono::seconds(10));
    ~CleanupDebouncer();

    CleanupDebouncer(const CleanupDebouncer &) = delete;
    CleanupDebouncer &operator=(const CleanupDebouncer &) = delete;

    void setOptionValues(double cacheSize);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace server1::cache
