#include "server1/settings/CachePolicy.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
#include <sys/stat.h>
#endif

namespace server1::settings {

namespace {

std::uint32_t positiveSetting(const SettingsStore &settings,
                              std::string_view key,
                              std::uint32_t fallback)
{
    const auto *value = settings.find(key);
    if (value && server1::policy::isPositiveInteger(*value))
        return static_cast<std::uint32_t>(value->asNumber());
    return fallback;
}

bool isNoCache(const SettingsStore &settings)
{
    const auto *cacheSize = settings.find("cacheSize");
    return cacheSize && cacheSize->kind() == server1::policy::Value::Kind::Number
           && cacheSize->asNumber() == 0.0;
}

} // namespace

EngineDefaults effectiveEngineDefaults(const SettingsStore &settings)
{
    EngineDefaults result;
    result.connections = positiveSetting(settings, "btMaxConnections", 35);
    if (isNoCache(settings)) {
        result.readerWindowBytes = 15U * 1024U * 1024U;
        result.circularStorageBytes = 45U * 1024U * 1024U;
        result.circularStorageInMemory = true;
        result.maxBufferFraction = .75;
    }
    return result;
}

} // namespace server1::settings

namespace server1::cache {

namespace {

std::uintmax_t saturatingAdd(std::uintmax_t left, std::uintmax_t right)
{
    if (std::numeric_limits<std::uintmax_t>::max() - left < right)
        return std::numeric_limits<std::uintmax_t>::max();
    return left + right;
}

std::int64_t accessTime(const std::filesystem::path &path)
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attributes {};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        ULARGE_INTEGER timestamp {};
        timestamp.LowPart = attributes.ftLastAccessTime.dwLowDateTime;
        timestamp.HighPart = attributes.ftLastAccessTime.dwHighDateTime;
        return static_cast<std::int64_t>(timestamp.QuadPart);
    }
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
    struct stat metadata {};
    if (::stat(path.c_str(), &metadata) == 0) {
#if defined(__APPLE__)
        return static_cast<std::int64_t>(metadata.st_atimespec.tv_sec) * 1000000000LL
               + static_cast<std::int64_t>(metadata.st_atimespec.tv_nsec);
#else
        return static_cast<std::int64_t>(metadata.st_atim.tv_sec) * 1000000000LL
               + static_cast<std::int64_t>(metadata.st_atim.tv_nsec);
#endif
    }
#else
    (void)path;
#endif

    // An unavailable access time is unknown. Never substitute modification time:
    // M414 orders eviction by atime, and mtime changes the eviction contract.
    return 0;
}

double adjustedTarget(double toSize,
                      std::uintmax_t cacheSize,
                      std::optional<std::uintmax_t> freeSpace,
                      std::uintmax_t requiredSize)
{
    if (!freeSpace)
        return toSize;

    const long double candidate = static_cast<long double>(cacheSize)
                                  + static_cast<long double>(*freeSpace)
                                  - static_cast<long double>(requiredSize);
    // M414 uses JavaScript's `candidate || toSize` before Math.min.
    if (candidate == 0.0L || std::isnan(static_cast<double>(candidate)))
        return toSize;
    return std::min(toSize, static_cast<double>(candidate));
}

std::uintmax_t totalSize(const std::vector<CacheEntry> &entries)
{
    std::uintmax_t result = 0;
    for (const auto &entry : entries)
        result = saturatingAdd(result, entry.size);
    return result;
}

} // namespace

std::vector<CacheEntry> enumerate(const std::filesystem::path &cachePath,
                                  const std::vector<std::string> &activeEngineIds)
{
    const std::set<std::string> active(activeEngineIds.begin(), activeEngineIds.end());
    std::vector<CacheEntry> entries;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        cachePath, std::filesystem::directory_options::skip_permission_denied, error);
    if (error)
        return entries;

    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const auto path = iterator->path();
        std::error_code entryError;
        if (iterator->is_regular_file(entryError)) {
            const auto size = iterator->file_size(entryError);
            if (!entryError) {
                const auto engineId = path.parent_path().filename().string();
                entries.push_back(CacheEntry {path,
                                              size,
                                              accessTime(path),
                                              active.find(engineId) != active.end()});
            }
        }

        iterator.increment(error);
        if (error)
            error.clear();
    }
    return entries;
}

std::vector<std::filesystem::path> planDeletions(std::vector<CacheEntry> entries,
                                                 double toSize,
                                                 std::optional<std::uintmax_t> freeSpace,
                                                 std::uintmax_t requiredSize)
{
    std::stable_sort(entries.begin(), entries.end(), [](const CacheEntry &left,
                                                        const CacheEntry &right) {
        return left.accessTime > right.accessTime;
    });

    const std::uintmax_t cacheSize = totalSize(entries);
    toSize = adjustedTarget(toSize, cacheSize, freeSpace, requiredSize);

    std::vector<std::filesystem::path> deletions;
    std::uintmax_t sizeSum = 0;
    for (const auto &entry : entries) {
        sizeSum = saturatingAdd(sizeSum, entry.size);
        if (static_cast<long double>(sizeSum) > static_cast<long double>(toSize)
            && !entry.activeEngine)
            deletions.push_back(entry.path);
    }
    return deletions;
}

CacheTrimResult clearCache(const std::filesystem::path &cachePath,
                           double toSize,
                           std::uintmax_t requiredSize,
                           const std::vector<std::string> &activeEngineIds,
                           std::optional<server1::platform::DiskSpace> space,
                           bool querySystemDiskSpace)
{
    CacheTrimResult result;
    result.target = toSize;
    if (std::isinf(toSize) && toSize > 0.0)
        return result;

    const auto entries = enumerate(cachePath, activeEngineIds);
    result.current = totalSize(entries);

    if (!space && querySystemDiskSpace)
        space = server1::platform::diskSpace(cachePath);
    if (space)
        result.target = adjustedTarget(toSize, result.current, space->free, requiredSize);

    std::vector<CacheEntry> ordered = entries;
    std::stable_sort(ordered.begin(), ordered.end(), [](const CacheEntry &left,
                                                       const CacheEntry &right) {
        return left.accessTime > right.accessTime;
    });
    std::uintmax_t sizeSum = 0;
    for (const auto &entry : ordered) {
        sizeSum = saturatingAdd(sizeSum, entry.size);
        if (static_cast<long double>(sizeSum) <= static_cast<long double>(result.target)
            || entry.activeEngine)
            continue;

        ++result.deleted;
        result.deletedPaths.push_back(entry.path);
        std::error_code error;
        (void)std::filesystem::remove(entry.path, error);
    }
    return result;
}

std::filesystem::path cachePath(const std::filesystem::path &cacheRoot, std::string_view key)
{
    std::error_code error;
    auto cacheDirectory = cacheRoot / "stremio-cache";
    std::filesystem::create_directories(cacheDirectory, error);
    if (error || !std::filesystem::is_directory(cacheDirectory, error)) {
        error.clear();
        cacheDirectory = std::filesystem::temp_directory_path(error);
        if (error)
            return cacheRoot / std::string(key);
        std::filesystem::create_directories(cacheDirectory, error);
    }
    return cacheDirectory / std::string(key);
}

class CleanupDebouncer::Impl final {
public:
    Impl(Callback callback, std::chrono::milliseconds delay)
        : callback_(std::move(callback))
        , delay_(delay)
        , worker_([this] { run(); })
    {
    }

    ~Impl()
    {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        if (worker_.joinable())
            worker_.join();
    }

    void setOptionValues(double cacheSize)
    {
        {
            std::lock_guard lock(mutex_);
            latest_ = cacheSize;
            ++generation_;
        }
        condition_.notify_all();
    }

private:
    void run()
    {
        std::unique_lock lock(mutex_);
        while (!stopping_) {
            condition_.wait(lock, [this] { return stopping_ || latest_.has_value(); });
            if (stopping_)
                return;

            std::uint64_t observedGeneration = generation_;
            auto deadline = std::chrono::steady_clock::now() + delay_;
            while (!stopping_) {
                if (condition_.wait_until(lock, deadline, [this, observedGeneration] {
                        return stopping_ || generation_ != observedGeneration;
                    })) {
                    if (stopping_)
                        return;
                    observedGeneration = generation_;
                    deadline = std::chrono::steady_clock::now() + delay_;
                    continue;
                }

                const double value = *latest_;
                latest_.reset();
                lock.unlock();
                if (callback_)
                    callback_(value);
                lock.lock();
                break;
            }
        }
    }

    Callback callback_;
    std::chrono::milliseconds delay_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<double> latest_;
    std::uint64_t generation_ {0};
    bool stopping_ {false};
    std::thread worker_;
};

CleanupDebouncer::CleanupDebouncer(Callback callback, std::chrono::milliseconds delay)
    : impl_(std::make_unique<Impl>(std::move(callback), delay))
{
}

CleanupDebouncer::~CleanupDebouncer() = default;

void CleanupDebouncer::setOptionValues(double cacheSize)
{
    impl_->setOptionValues(cacheSize);
}

} // namespace server1::cache
