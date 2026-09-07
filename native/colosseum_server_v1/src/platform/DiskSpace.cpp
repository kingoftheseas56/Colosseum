#include "server1/platform/DiskSpace.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace server1::platform {

namespace {

bool environmentIsUnavailable()
{
    return std::getenv("TIZEN_ENV") != nullptr || std::getenv("WEBOS_ENV") != nullptr;
}

#ifdef _WIN32
bool isPriorityDrive(const std::filesystem::path &path)
{
    const auto root = path.root_name().wstring();
    if (root.empty())
        return false;
    const wchar_t drive = static_cast<wchar_t>(std::towupper(root.front()));
    return drive == L'E' || drive == L'D';
}
#endif

} // namespace

std::optional<DiskSpace> diskSpace(const std::filesystem::path &cachePath)
{
    if (environmentIsUnavailable())
        return std::nullopt;

#ifdef _WIN32
    ULARGE_INTEGER availableBytes {};
    ULARGE_INTEGER totalBytes {};
    ULARGE_INTEGER freeBytes {};
    if (!GetDiskFreeSpaceExW(cachePath.c_str(), &availableBytes, &totalBytes, &freeBytes))
        return std::nullopt;
    return DiskSpace {totalBytes.QuadPart, freeBytes.QuadPart};
#else
    std::error_code error;
    const auto space = std::filesystem::space(cachePath, error);
    if (error)
        return std::nullopt;
    return DiskSpace {space.capacity, space.free};
#endif
}

std::vector<std::filesystem::path> cacheLocations()
{
#ifdef _WIN32
    const DWORD required = GetLogicalDriveStringsW(0, nullptr);
    if (required == 0)
        return {};

    std::vector<wchar_t> buffer(static_cast<std::size_t>(required) + 1, L'\0');
    const DWORD written = GetLogicalDriveStringsW(required, buffer.data());
    if (written == 0 || written >= buffer.size())
        return {};

    std::vector<std::filesystem::path> locations;
    for (const wchar_t *cursor = buffer.data(); *cursor; cursor += std::wcslen(cursor) + 1) {
        if (GetDriveTypeW(cursor) != DRIVE_FIXED)
            continue;
        const wchar_t drive = static_cast<wchar_t>(std::towupper(cursor[0]));
        if (drive == L'Q')
            continue;
        locations.emplace_back(cursor);
    }

    // M414's comparator places the preferred D:/E: drives after other fixed drives.
    std::stable_sort(locations.begin(), locations.end(), [](const auto &left, const auto &right) {
        return isPriorityDrive(left) < isPriorityDrive(right);
    });
    return locations;
#else
    return {};
#endif
}

} // namespace server1::platform
