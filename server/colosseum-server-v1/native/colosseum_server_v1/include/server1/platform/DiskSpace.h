#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace server1::platform {

struct DiskSpace final {
    std::uintmax_t size {0};
    std::uintmax_t free {0};
};

[[nodiscard]] std::optional<DiskSpace> diskSpace(const std::filesystem::path &cachePath);
[[nodiscard]] std::vector<std::filesystem::path> cacheLocations();

} // namespace server1::platform
