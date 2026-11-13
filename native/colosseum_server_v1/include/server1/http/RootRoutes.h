#pragma once
#include "server1/settings/SettingsStore.h"
#include <functional>
#include <string>
#include <vector>

namespace server1::http {
struct RootRoutesConfig final {
    // Borrowed; must outlive the router. All callbacks run on the application lane.
    settings::SettingsStore *settings = nullptr;
    std::function<std::string()> baseUrl;
    std::function<void()> settingsChanged;
    std::string webUILocation;
    // Empty callbacks select real platform enumeration; overrides support deterministic tests.
    std::function<std::vector<std::string>()> networkInterfaces;
    std::function<std::vector<std::string>()> cacheLocations;
};
[[nodiscard]] bool installRootRoutes(void *router, RootRoutesConfig config);
} // namespace server1::http
