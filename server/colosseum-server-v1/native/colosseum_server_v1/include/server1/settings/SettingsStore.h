#pragma once

#include "server1/policy/Value.h"

#include <string>
#include <string_view>

namespace server1::settings {

struct SettingsStoreConfig final {
    std::string appPath;
    std::string serverVersion;
    std::string settingsPathOverride;
    bool android {false};
    bool disableCaching {false};
};

class SettingsStore final {
public:
    explicit SettingsStore(SettingsStoreConfig config);

    void extend(const policy::Value &extension);
    [[nodiscard]] bool set(std::string_view key, policy::Value value);
    [[nodiscard]] bool load();
    [[nodiscard]] bool save() const;

    [[nodiscard]] policy::Value value() const;
    [[nodiscard]] const policy::Value *find(std::string_view key) const noexcept;
    [[nodiscard]] const std::string &appPath() const noexcept;
    [[nodiscard]] const std::string &settingsPath() const noexcept;
    [[nodiscard]] const std::string &serverVersion() const noexcept;
    [[nodiscard]] std::string serialized() const;

private:
    std::string appPath_;
    std::string settingsPath_;
    std::string serverVersion_;
    bool android_ {false};
    bool disableCaching_ {false};
    policy::Settings settings_;
};

} // namespace server1::settings
