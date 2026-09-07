#include "server1/settings/SettingsStore.h" // Production declaration; K13-A ownership correction.

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using server1::policy::Value;
using server1::settings::SettingsStore;
using server1::settings::SettingsStoreConfig;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

const Value &requiredValue(const SettingsStore &store, std::string_view key)
{
    const auto *value = store.find(key);
    require(value != nullptr, "missing settings key: " + std::string(key));
    return *value;
}

void requireString(const SettingsStore &store, std::string_view key, const std::string &expected)
{
    const auto &value = requiredValue(store, key);
    require(value.kind() == Value::Kind::String && value.asString() == expected,
            "unexpected string setting: " + std::string(key));
}

void requireNumber(const SettingsStore &store, std::string_view key, double expected)
{
    const auto &value = requiredValue(store, key);
    require(value.kind() == Value::Kind::Number && value.asNumber() == expected,
            "unexpected numeric setting: " + std::string(key));
}

void requireBoolean(const SettingsStore &store, std::string_view key, bool expected)
{
    const auto &value = requiredValue(store, key);
    require(value.kind() == Value::Kind::Boolean && value.asBoolean() == expected,
            "unexpected boolean setting: " + std::string(key));
}

void writeText(const fs::path &path, const std::string &text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "cannot create fixture: " + path.string());
    output << text;
    require(output.good(), "cannot write fixture: " + path.string());
}

std::string readText(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "cannot read fixture: " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

SettingsStoreConfig configFor(const fs::path &root)
{
    return SettingsStoreConfig {root.string(), "4.21.1", root.string(), false, false};
}

void caseK13_01(const fs::path &root)
{
    const auto settingsDir = root / "settings";
    fs::create_directories(settingsDir);
    auto config = configFor(settingsDir);

    SettingsStore fresh(config);
    requireString(fresh, "serverVersion", "4.21.1");
    requireString(fresh, "appPath", settingsDir.string());
    requireString(fresh, "cacheRoot", settingsDir.string());
    requireNumber(fresh, "cacheSize", 2147483648.0);
    requireNumber(fresh, "btMaxConnections", 55.0);
    requireNumber(fresh, "btHandshakeTimeout", 20000.0);
    requireNumber(fresh, "btRequestTimeout", 4000.0);
    requireNumber(fresh, "btDownloadSpeedSoftLimit", 2621440.0);
    requireNumber(fresh, "btDownloadSpeedHardLimit", 3670016.0);
    requireNumber(fresh, "btMinPeersForStable", 5.0);
    requireString(fresh, "remoteHttps", "");
    requireBoolean(fresh, "localAddonEnabled", false);
    requireNumber(fresh, "transcodeHorsepower", .75);
    requireNumber(fresh, "transcodeMaxBitRate", 0.0);
    requireNumber(fresh, "transcodeConcurrency", 1.0);
    requireNumber(fresh, "transcodeTrackConcurrency", 1.0);
    requireBoolean(fresh, "transcodeHardwareAccel", true);
    require(requiredValue(fresh, "transcodeProfile").isNull(), "transcodeProfile default is null");
    require(requiredValue(fresh, "allTranscodeProfiles").kind() == Value::Kind::Array,
            "allTranscodeProfiles default is an array");
    requireNumber(fresh, "transcodeMaxWidth", 1920.0);
    requireBoolean(fresh, "proxyStreamsEnabled", false);

    require(!fresh.set("serverVersion", Value::string("9.9.9")),
            "serverVersion override must be readonly");
    require(fresh.serverVersion() == "4.21.1", "serverVersion override changed effective version");

    auto noCache = config;
    noCache.settingsPathOverride = (settingsDir / "no-cache").string();
    fs::create_directories(noCache.settingsPathOverride);
    noCache.disableCaching = true;
    SettingsStore disabled(noCache);
    requireNumber(disabled, "cacheSize", 0.0);

    std::cout << "K13-01 defaults, readonly override, and DISABLE_CACHING PASS\n";
}

void caseK13_03(const fs::path &root)
{
    const auto appDir = root / "app";
    const auto overrideDir = root / "override";
    fs::create_directories(appDir);
    fs::create_directories(overrideDir);
    const auto settingsFile = overrideDir / "server-settings.json";
    writeText(settingsFile,
              R"({"serverVersion":"9.9.9","cacheSize":0,"btMaxConnections":99,"unknownKey":"retained"})");

    auto config = configFor(appDir);
    config.settingsPathOverride = overrideDir.string();
    SettingsStore loaded(config);
    require(loaded.settingsPath() == settingsFile.string(), "SETTINGS_PATH override did not win");
    require(loaded.serverVersion() == "4.21.1", "loaded serverVersion bypassed readonly setter");
    requireNumber(loaded, "cacheSize", 0.0);
    requireNumber(loaded, "btMaxConnections", 99.0);
    requireString(loaded, "unknownKey", "retained");

    loaded.extend(Value::object({
        {"unknownKey", Value::string("overridden")},
        {"newUnknown", Value::boolean(true)},
    }));
    require(loaded.save(), "writable settings path failed to save");
    require(loaded.serialized().find("\"unknownKey\": \"overridden\"") != std::string::npos,
            "serialized persistence omitted the overridden unknown key");

    SettingsStore roundTrip(config);
    requireString(roundTrip, "unknownKey", "overridden");
    requireBoolean(roundTrip, "newUnknown", true);
    require(!roundTrip.set("serverVersion", Value::string("override")),
            "serverVersion setter accepted a persisted override");

    (void)roundTrip.set("cacheSize", Value::number(std::numeric_limits<double>::infinity()));
    require(roundTrip.save(), "writable settings path rejected non-finite option value");
    const auto saved = readText(settingsFile);
    require(saved.find("\"cacheSize\": null") != std::string::npos,
            "non-finite cacheSize was not JSON-normalized to null");

    const auto corruptFile = appDir / "server-settings.json";
    writeText(corruptFile, "{not-json");
    auto corruptConfig = configFor(appDir);
    SettingsStore corrupt(corruptConfig);
    requireNumber(corrupt, "cacheSize", 2147483648.0);
    require(readText(corruptFile) == "{not-json", "corrupt settings were overwritten");

    auto unwritable = configFor(root / "missing-parent" / "app");
    SettingsStore missingParent(unwritable);
    require(!missingParent.save(), "save unexpectedly succeeded through a missing parent");

    std::cout << "K13-03 load/merge, unknown fields, persistence, corrupt input, and path fallback PASS\n";
}

void traceCase(const fs::path &root)
{
    const auto defaultsDir = root / "defaults";
    fs::create_directories(defaultsDir);
    writeText(defaultsDir / "server-settings.json", "{}");
    auto defaultsConfig = configFor(defaultsDir);
    SettingsStore defaults(defaultsConfig);
    requireNumber(defaults, "cacheSize", 2147483648.0);
    requireNumber(defaults, "btMaxConnections", 55.0);
    requireString(defaults, "remoteHttps", "");
    require(requiredValue(defaults, "transcodeProfile").isNull(),
            "trace transcodeProfile default is null");
    std::cout << "K13-01 default.cacheSize="
              << static_cast<std::uint64_t>(requiredValue(defaults, "cacheSize").asNumber()) << '\n';
    std::cout << "K13-01 default.btMaxConnections="
              << requiredValue(defaults, "btMaxConnections").asNumber() << '\n';
    std::cout << "K13-01 default.remoteHttps=" << requiredValue(defaults, "remoteHttps").asString()
              << '\n';
    std::cout << "K13-01 default.transcodeProfile=null\n";

    const auto loadedDir = root / "loaded";
    fs::create_directories(loadedDir);
    writeText(loadedDir / "server-settings.json",
              R"({"serverVersion":"9.9.9","cacheSize":0,"btMaxConnections":99,"unknownKey":"retained"})");
    auto loadedConfig = configFor(loadedDir);
    SettingsStore loaded(loadedConfig);
    requireNumber(loaded, "cacheSize", 0.0);
    requireNumber(loaded, "btMaxConnections", 99.0);
    requireString(loaded, "unknownKey", "retained");
    require(loaded.serverVersion() == "4.21.1", "trace persisted serverVersion override changed version");
    std::cout << "K13-01 load.cacheSize=" << requiredValue(loaded, "cacheSize").asNumber() << '\n';
    std::cout << "K13-01 load.btMaxConnections="
              << requiredValue(loaded, "btMaxConnections").asNumber() << '\n';
    std::cout << "K13-01 load.unknownKey=" << requiredValue(loaded, "unknownKey").asString() << '\n';
    std::cout << "K13-01 load.serverVersion=" << loaded.serverVersion() << '\n';

    loaded.extend(Value::object({
        {"unknownKey", Value::string("overridden")},
        {"newUnknown", Value::boolean(true)},
    }));
    require(loaded.save(), "trace persistence save failed");
    SettingsStore persisted(loadedConfig);
    requireString(persisted, "unknownKey", "overridden");
    requireBoolean(persisted, "newUnknown", true);
    std::cout << "K13-03 persisted.unknownKey="
              << requiredValue(persisted, "unknownKey").asString() << '\n';
    std::cout << "K13-03 persisted.newUnknown="
              << (requiredValue(persisted, "newUnknown").asBoolean() ? "true" : "false") << '\n';

    (void)persisted.set("cacheSize", Value::number(std::numeric_limits<double>::infinity()));
    const bool infiniteIsNull = persisted.serialized().find("\"cacheSize\": null") != std::string::npos;
    require(infiniteIsNull, "trace non-finite cacheSize was not serialized as null");
    std::cout << "K13-03 infinite-null=" << (infiniteIsNull ? "1" : "0") << '\n';
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const fs::path root = argc > 1 ? fs::path(argv[1])
                                       : fs::temp_directory_path() / "server1-k13-a-probe";
        fs::remove_all(root);
        fs::create_directories(root);

        const std::string requested = argc > 2 ? argv[2] : "all";
        if (requested == "--trace") {
            traceCase(root);
            fs::remove_all(root);
            return 0;
        }
        if (requested == "all" || requested == "K13-01")
            caseK13_01(root / "K13-01");
        if (requested == "all" || requested == "K13-03")
            caseK13_03(root / "K13-03");
        if (requested != "all" && requested != "K13-01" && requested != "K13-03")
            throw std::runtime_error("unknown K13 case");

        fs::remove_all(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "K13-A probe FAIL: " << error.what() << '\n';
        return 1;
    }
}
