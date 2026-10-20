#include "server1/settings/SettingsStore.h"

#include "server1/settings/CachePolicy.h"

#include "server1/policy/Value.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using server1::policy::Value;
using server1::settings::SettingsStore;
using server1::settings::SettingsStoreConfig;

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

const Value &requiredValue(const SettingsStore &settings, std::string_view key)
{
    const auto *value = settings.find(key);
    require(value != nullptr, "missing settings key: " + std::string(key));
    return *value;
}

double number(const SettingsStore &settings, std::string_view key)
{
    const auto &value = requiredValue(settings, key);
    require(value.kind() == Value::Kind::Number, "setting is not numeric: " + std::string(key));
    return value.asNumber();
}

void writeText(const fs::path &path, std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "cannot create fixture: " + path.string());
    output << text;
    require(output.good(), "cannot write fixture: " + path.string());
}

void writeBytes(const fs::path &path, std::size_t count, char value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "cannot create cache fixture: " + path.string());
    for (std::size_t index = 0; index < count; ++index)
        output.put(value);
    require(output.good(), "cannot write cache fixture: " + path.string());
}

bool setCrossedAccessAndWriteTimes(const fs::path &accessOlderWriteNewer,
                                   const fs::path &accessNewerWriteOlder)
{
#ifdef _WIN32
    FILETIME currentFileTime {};
    GetSystemTimeAsFileTime(&currentFileTime);
    ULARGE_INTEGER current {};
    current.LowPart = currentFileTime.dwLowDateTime;
    current.HighPart = currentFileTime.dwHighDateTime;

    const auto shifted = [current](std::int64_t seconds) {
        const auto value = static_cast<std::int64_t>(current.QuadPart)
                           + seconds * 10000000LL;
        return static_cast<ULONGLONG>(value);
    };
    const auto setTimes = [](const fs::path &path, ULONGLONG access, ULONGLONG write) {
        const HANDLE handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE
                                               | FILE_SHARE_DELETE,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;

        FILETIME accessTime {};
        accessTime.dwLowDateTime = static_cast<DWORD>(access);
        accessTime.dwHighDateTime = static_cast<DWORD>(access >> 32);
        FILETIME writeTime {};
        writeTime.dwLowDateTime = static_cast<DWORD>(write);
        writeTime.dwHighDateTime = static_cast<DWORD>(write >> 32);
        const bool result = SetFileTime(handle, nullptr, &accessTime, &writeTime) != 0;
        CloseHandle(handle);
        return result;
    };

    return setTimes(accessOlderWriteNewer, shifted(-20), shifted(0))
           && setTimes(accessNewerWriteOlder, shifted(0), shifted(-20));
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
    timespec now {};
    if (::clock_gettime(CLOCK_REALTIME, &now) != 0)
        return false;

    const auto shifted = [now](std::int64_t seconds) {
        timespec value = now;
        value.tv_sec += static_cast<decltype(value.tv_sec)>(seconds);
        return value;
    };
    const timespec firstTimes[2] {shifted(-20), shifted(0)};
    const timespec secondTimes[2] {shifted(0), shifted(-20)};
    return ::utimensat(AT_FDCWD, accessOlderWriteNewer.c_str(), firstTimes, 0) == 0
           && ::utimensat(AT_FDCWD, accessNewerWriteOlder.c_str(), secondTimes, 0) == 0;
#else
    (void)accessOlderWriteNewer;
    (void)accessNewerWriteOlder;
    return false;
#endif
}

SettingsStoreConfig configFor(const fs::path &root, bool disableCaching = false)
{
    return SettingsStoreConfig {root.string(), "4.21.1", root.string(), false, disableCaching};
}

void caseK13_01_and_K13_02(const fs::path &root)
{
    const auto settingsDir = root / "settings";
    fs::create_directories(settingsDir);
    writeText(settingsDir / "server-settings.json", "{}");

    SettingsStore fresh(configFor(settingsDir));
    require(number(fresh, "cacheSize") == 2147483648.0,
            "fresh desktop cache default must be 2 GiB");
    require(number(fresh, "btMaxConnections") == 55.0,
            "fresh desktop connection default must be 55");

    const auto noCacheDir = root / "no-cache-settings";
    fs::create_directories(noCacheDir);
    writeText(noCacheDir / "server-settings.json", "{}");
    SettingsStore noCache(configFor(noCacheDir, true));
    require(number(noCache, "cacheSize") == 0.0, "DISABLE_CACHING must force cacheSize to zero");

    const auto defaults = server1::settings::effectiveEngineDefaults(noCache);
    require(defaults.connections == 55, "no-cache entrypoint must retain 55 connections");
    require(defaults.readerWindowBytes == 15728640,
            "no-cache entrypoint must use a 15 MiB reader window");
    require(defaults.circularStorageBytes == 47185920,
            "no-cache entrypoint must use a 45 MiB circular store");
    require(defaults.circularStorageInMemory, "no-cache circular store must be memory-backed");
    require(defaults.maxBufferFraction == .75, "no-cache swarm maxBuffer must be 0.75");

    const auto cacheRoot = root / "cache";
    const auto cacheDir = cacheRoot / "stremio-cache";
    const auto activeDir = cacheDir / "active-engine";
    const auto inactiveDir = cacheDir / "inactive-engine";
    fs::create_directories(activeDir);
    fs::create_directories(inactiveDir);
    writeBytes(activeDir / "active.bin", 4, 'a');
    writeBytes(inactiveDir / "old.bin", 4, 'b');
    writeBytes(inactiveDir / "new.bin", 4, 'c');

    const auto entries = server1::cache::enumerate(cacheDir, {"active-engine"});
    require(entries.size() == 3, "cache enumeration must return all regular files");
    bool sawActive = false;
    bool sawInactive = false;
    for (const auto &entry : entries) {
        sawActive = sawActive || (entry.path.parent_path().filename() == "active-engine"
                                  && entry.activeEngine);
        sawInactive = sawInactive || (entry.path.parent_path().filename() == "inactive-engine"
                                      && !entry.activeEngine);
    }
    require(sawActive && sawInactive, "active engine omission must be based on infohash directory");

    const auto equalAtimeA = cacheRoot / "equal-atime-a";
    const auto equalAtimeB = cacheRoot / "equal-atime-b";
    const auto active = cacheRoot / "active.bin";
    const auto newest = cacheRoot / "newest.bin";
    const std::int64_t equalAtime = 100;
    const auto deletions = server1::cache::planDeletions(
        {
            {active, 4, 300, true},
            {newest, 4, 200, false},
            {equalAtimeA, 4, equalAtime, false},
            {equalAtimeB, 4, equalAtime, false},
        },
        4.0);
    require(deletions.size() == 3, "cache trimming must delete files over the target");
    require(deletions[0] == newest, "newer inactive cache must be considered before equal-atime files");
    require(deletions[1] == equalAtimeA && deletions[2] == equalAtimeB,
            "equal-atime eviction must retain stable enumeration order");
    require(std::find(deletions.begin(), deletions.end(), active) == deletions.end(),
            "active-engine files must never be selected for deletion");

    const auto trim = server1::cache::clearCache(
        cacheDir, 10.0, 4, {"active-engine"}, server1::platform::DiskSpace {100, 1}, false);
    require(trim.current == 12, "cache trim must report the enumerated byte total");
    require(trim.target == 9.0,
            "insufficient free space must clamp target to cacheSize + free - requiredSize");
    require(fs::exists(activeDir / "active.bin"), "active-engine file must survive cache trimming");

    const auto atimeRoot = cacheRoot / "atime-order";
    fs::create_directories(atimeRoot);
    const auto accessOlderWriteNewer = atimeRoot / "access-older-write-newer.bin";
    const auto accessNewerWriteOlder = atimeRoot / "access-newer-write-older.bin";
    writeBytes(accessOlderWriteNewer, 1, 'd');
    writeBytes(accessNewerWriteOlder, 1, 'e');
    if (setCrossedAccessAndWriteTimes(accessOlderWriteNewer, accessNewerWriteOlder)) {
        const auto atimeEntries = server1::cache::enumerate(atimeRoot, {});
        const auto atimeDeletions = server1::cache::planDeletions(atimeEntries, 1.0);
        require(atimeDeletions.size() == 1
                    && atimeDeletions.front() == accessOlderWriteNewer,
                "cache eviction must order by access time, not modification time");
        std::cout << "K13-02 atime-ordering PASS\n";
    } else {
        std::cout << "K13-02 atime-ordering NOT_APPLICABLE\n";
    }

    std::cout << "K13-01 defaults/no-cache and K13-02 enumeration/eviction PASS\n";
}

void caseK13_03(const fs::path &root)
{
    const auto fallbackRoot = root / "fallback-root";
    fs::create_directories(fallbackRoot);
    writeText(fallbackRoot / "stremio-cache", "not a directory");
    const auto fallback = server1::cache::cachePath(fallbackRoot, "fallback-engine");
    require(fallback.filename() == "fallback-engine", "fallback cache path must retain the engine key");
    require(fs::equivalent(fallback.parent_path(), fs::temp_directory_path()),
            "cache creation failure must fall back to the system temporary directory: got "
                + fallback.parent_path().string() + " expected " + fs::temp_directory_path().string());

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<double> observed;
    server1::cache::CleanupDebouncer debouncer(
        [&](double value) {
            std::lock_guard lock(mutex);
            observed.push_back(value);
            condition.notify_all();
        },
        std::chrono::milliseconds(40));
    debouncer.setOptionValues(2147483648.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    debouncer.setOptionValues(5368709120.0);

    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock, std::chrono::seconds(2), [&] { return observed.size() == 1; }),
                "delayed cache cleaning did not fire");
    }
    require(observed[0] == 5368709120.0,
            "delayed cache cleaning must use the latest cache-size value");

    const auto locations = server1::platform::cacheLocations();
#ifdef _WIN32
    for (const auto &location : locations)
        require(location.root_name() != fs::path("Q:"), "cache location enumeration must omit Q:");
#else
    require(locations.empty(), "non-Windows cache location enumeration must be empty");
#endif

    std::cout << "K13-03 fallback cache directory and delayed cleaning PASS\n";
}

void traceCase(const fs::path &root)
{
    const auto settingsDir = root / "settings";
    fs::create_directories(settingsDir);
    writeText(settingsDir / "server-settings.json", "{}");
    SettingsStore fresh(configFor(settingsDir));
    std::cout << "K13-02 default.cacheSize="
              << static_cast<std::uint64_t>(number(fresh, "cacheSize")) << '\n';
    std::cout << "K13-02 default.btMaxConnections="
              << static_cast<std::uint64_t>(number(fresh, "btMaxConnections")) << '\n';

    const auto noCacheDir = root / "no-cache-settings";
    fs::create_directories(noCacheDir);
    writeText(noCacheDir / "server-settings.json", "{}");
    const SettingsStore noCache(configFor(noCacheDir, true));
    const auto defaults = server1::settings::effectiveEngineDefaults(noCache);
    std::cout << "K13-02 noCache.buffer=" << defaults.readerWindowBytes << '\n';
    std::cout << "K13-02 noCache.circularBuffer.size=" << defaults.circularStorageBytes << '\n';

    const auto equalAtimeA = root / "equal-atime-a";
    const auto equalAtimeB = root / "equal-atime-b";
    const auto active = root / "active.bin";
    const auto newest = root / "newest.bin";
    const auto deletions = server1::cache::planDeletions(
        {
            {active, 4, 300, true},
            {newest, 4, 200, false},
            {equalAtimeA, 4, 100, false},
            {equalAtimeB, 4, 100, false},
        },
        4.0);
    require(deletions.size() == 3, "trace equal-atime deletion count mismatch");
    std::cout << "K13-03 equal-atime-order=newest,equal-atime-a,equal-atime-b\n";
    require(std::find(deletions.begin(), deletions.end(), active) == deletions.end(),
            "trace active engine was selected");
    std::cout << "K13-03 active-omitted=1\n";

    const auto trimRoot = root / "trim";
    const auto trimDir = trimRoot / "stremio-cache";
    fs::create_directories(trimDir / "active-engine");
    fs::create_directories(trimDir / "inactive-engine");
    writeBytes(trimDir / "active-engine" / "active.bin", 4, 'a');
    writeBytes(trimDir / "inactive-engine" / "old.bin", 4, 'b');
    writeBytes(trimDir / "inactive-engine" / "new.bin", 4, 'c');
    const auto trim = server1::cache::clearCache(
        trimDir, 10.0, 4, {"active-engine"}, server1::platform::DiskSpace {100, 1}, false);
    require(trim.target == 9.0, "trace disk target mismatch");
    std::cout << "K13-03 disk-target=" << trim.target << '\n';

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<double> observed;
    server1::cache::CleanupDebouncer debouncer(
        [&](double value) {
            std::lock_guard lock(mutex);
            observed.push_back(value);
            condition.notify_all();
        },
        std::chrono::milliseconds(20));
    debouncer.setOptionValues(2147483648.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    debouncer.setOptionValues(5368709120.0);
    {
        std::unique_lock lock(mutex);
        require(condition.wait_for(lock, std::chrono::seconds(2), [&] { return observed.size() == 1; }),
                "trace delayed cleanup did not fire");
    }
    std::cout << "K13-03 delayed-latest="
              << static_cast<std::uint64_t>(observed.front()) << '\n';

    const auto fallbackRoot = root / "fallback";
    fs::create_directories(fallbackRoot);
    writeText(fallbackRoot / "stremio-cache", "not a directory");
    const auto fallback = server1::cache::cachePath(fallbackRoot, "fallback-engine");
    require(fs::equivalent(fallback.parent_path(), fs::temp_directory_path()),
            "trace fallback directory mismatch");
    std::cout << "K13-03 fallback-temp=1\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const fs::path root = argc > 1 ? fs::path(argv[1])
                                       : fs::temp_directory_path() / "server1-k13-b-test";
        fs::remove_all(root);
        fs::create_directories(root);

        const std::string requested = argc > 2 ? argv[2] : "all";
        if (requested == "--trace") {
            traceCase(root);
            fs::remove_all(root);
            return 0;
        }
        if (requested == "all" || requested == "K13-01" || requested == "K13-02")
            caseK13_01_and_K13_02(root / "K13-01-02");
        if (requested == "all" || requested == "K13-03")
            caseK13_03(root / "K13-03");
        if (requested != "all" && requested != "K13-01" && requested != "K13-02"
            && requested != "K13-03")
            throw std::runtime_error("unknown K13 case");

        fs::remove_all(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "K13-B test FAIL: " << error.what() << '\n';
        return 1;
    }
}
