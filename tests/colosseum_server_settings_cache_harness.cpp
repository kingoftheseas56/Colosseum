#include "../native/colosseum_server/cache/CachePolicy.h"
#include "../native/colosseum_server/settings/ServerSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>

#include <cmath>
#include <cstdio>

using colosseum::server::CacheCleanupDebouncer;
using colosseum::server::CacheEntry;
using colosseum::server::CachePolicy;
using colosseum::server::ServerSettings;

static int fails = 0;
#define CHECK(c,l) do { if (!(c)) { ++fails; std::printf("FAIL: %s\n", l); } } while (0)

static void settingsDefaultsMatchStremio42017()
{
    QTemporaryDir temp;
    CHECK(temp.isValid(), "temporary directory exists");
    ServerSettings settings(temp.path(), ServerSettings::Platform::Windows);
    const QJsonObject values = settings.values();

    CHECK(values.value("serverVersion").toString() == "4.20.17", "server version matches oracle");
    CHECK(values.value("appPath").toString() == temp.path(), "appPath matches injected path");
    CHECK(values.value("cacheRoot").toString() == temp.path(), "cacheRoot defaults to appPath");
    CHECK(values.value("cacheSize").toDouble() == 2147483648.0, "desktop cache defaults 2 GiB");
    CHECK(values.value("btMaxConnections").toInt() == 55, "btMaxConnections default");
    CHECK(values.value("btHandshakeTimeout").toInt() == 20000, "btHandshakeTimeout default");
    CHECK(values.value("btRequestTimeout").toInt() == 4000, "btRequestTimeout default");
    CHECK(values.value("btDownloadSpeedSoftLimit").toInt() == 2621440, "soft speed default");
    CHECK(values.value("btDownloadSpeedHardLimit").toInt() == 3670016, "hard speed default");
    CHECK(values.value("btMinPeersForStable").toInt() == 5, "stable peer default");
    CHECK(values.value("remoteHttps").toString().isEmpty(), "remoteHttps default");
    CHECK(!values.value("localAddonEnabled").toBool(), "local addon default");
    CHECK(values.value("transcodeHorsepower").toDouble() == 0.75, "horsepower default");
    CHECK(values.value("transcodeMaxBitRate").toInt() == 0, "max bitrate default");
    CHECK(values.value("transcodeConcurrency").toInt() == 1, "transcode concurrency default");
    CHECK(values.value("transcodeTrackConcurrency").toInt() == 1, "track concurrency default");
    CHECK(values.value("transcodeHardwareAccel").toBool(), "hardware accel default");
    CHECK(values.value("transcodeProfile").isNull(), "transcode profile default null");
    CHECK(values.value("allTranscodeProfiles").toArray().isEmpty(), "profile list default empty");
    CHECK(values.value("transcodeMaxWidth").toInt() == 1920, "max width default");
    CHECK(!values.value("proxyStreamsEnabled").toBool(), "proxy streams default");
}

static void platformCacheDefaultsMatchLegacy()
{
    ServerSettings android("/tmp/server", ServerSettings::Platform::Android);
    ServerSettings disabled("C:/server", ServerSettings::Platform::Windows, {}, true);
    CHECK(android.values().value("cacheSize").toInt() == 0, "Android disables cache by default");
    CHECK(disabled.values().value("cacheSize").toInt() == 0, "DISABLE_CACHING disables cache");
}

static void settingsExtendLoadSavePreserveLegacyDynamics()
{
    QTemporaryDir temp;
    QFile fixture(temp.filePath("server-settings.json"));
    CHECK(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate), "settings fixture opens");
    const QJsonObject persisted{
        {"cacheSize", QJsonValue::Null},
        {"localAddonEnabled", QStringLiteral("legacy-truthy")},
        {"serverVersion", QStringLiteral("0.0.0")},
        {"unknownLegacyField", 42},
    };
    fixture.write(QJsonDocument(persisted).toJson(QJsonDocument::Compact));
    fixture.close();

    ServerSettings settings(temp.path(), ServerSettings::Platform::Windows, temp.path());
    QJsonObject values = settings.values();
    CHECK(values.value("cacheSize").isNull(), "persisted null overwrites cache default");
    CHECK(values.value("localAddonEnabled").toString() == "legacy-truthy", "invalid type stays dynamic");
    CHECK(values.value("serverVersion").toString() == "4.20.17", "loaded version cannot override getter");
    CHECK(values.value("unknownLegacyField").toInt() == 42, "unknown persisted key survives");

    settings.extend(QJsonObject{{"btMaxConnections", 57}, {"serverVersion", "9.9.9"}});
    CHECK(settings.save(), "settings save succeeds");
    values = settings.values();
    CHECK(values.value("btMaxConnections").toInt() == 57, "runtime extend applies");
    CHECK(values.value("serverVersion").toString() == "4.20.17", "extended version remains read-only");

    QFile saved(temp.filePath("server-settings.json"));
    CHECK(saved.open(QIODevice::ReadOnly), "saved settings reopen");
    const QJsonObject savedObject = QJsonDocument::fromJson(saved.readAll()).object();
    CHECK(savedObject.value("btMaxConnections").toInt() == 57, "runtime override persists");
    CHECK(savedObject.value("serverVersion").toString() == "4.20.17", "saved version is authoritative");
}

static void runtimeEnvironmentOverridesMatchModule105()
{
    QTemporaryDir temp;
    CHECK(temp.isValid(), "runtime override temporary directory exists");
    const QString settingsDir = temp.filePath(QStringLiteral("env-settings"));
    CHECK(QDir().mkpath(settingsDir), "runtime SETTINGS_PATH fixture exists");

    const QByteArray oldSettingsPath = qgetenv("SETTINGS_PATH");
    const QByteArray oldDisableCaching = qgetenv("DISABLE_CACHING");
    qputenv("SETTINGS_PATH", settingsDir.toUtf8());
    qputenv("DISABLE_CACHING", QByteArrayLiteral("1"));

    ServerSettings settings(temp.filePath(QStringLiteral("app")), ServerSettings::Platform::Windows);
    CHECK(settings.settingsFilePath() == QDir(settingsDir).filePath(QStringLiteral("server-settings.json")),
          "SETTINGS_PATH overrides appPath for persistence");
    CHECK(settings.values().value(QStringLiteral("cacheSize")).toDouble() == 0.0,
          "DISABLE_CACHING forces cacheSize zero");

    oldSettingsPath.isNull() ? qunsetenv("SETTINGS_PATH") : qputenv("SETTINGS_PATH", oldSettingsPath);
    oldDisableCaching.isNull() ? qunsetenv("DISABLE_CACHING") : qputenv("DISABLE_CACHING", oldDisableCaching);
}

static void appAndSettingsPathsMatchModule413()
{
    QProcessEnvironment env;
    env.insert("APPDATA", QStringLiteral("C:\\Users\\Oracle\\AppData\\Roaming"));
    const QString resolved = ServerSettings::resolveAppPath(
        ServerSettings::Platform::Windows, env, QStringLiteral("C:\\Temp"));
    CHECK(resolved == QStringLiteral("C:\\Users\\Oracle\\AppData\\Roaming\\stremio\\stremio-server"),
          "Windows APPDATA path matches module 413");

    env.insert("APP_PATH", QStringLiteral("D:\\custom-server"));
    CHECK(ServerSettings::resolveAppPath(ServerSettings::Platform::Windows, env, "C:\\Temp")
              == QStringLiteral("D:\\custom-server"),
          "APP_PATH overrides platform path");

    QTemporaryDir temp;
    const QString settingsDir = temp.filePath("settings");
    CHECK(QDir().mkpath(settingsDir), "settings path fixture exists");
    ServerSettings settings(temp.path(), ServerSettings::Platform::Windows, settingsDir);
    CHECK(settings.settingsFilePath() == QDir(settingsDir).filePath("server-settings.json"),
          "SETTINGS_PATH-style directory owns settings file");
}
static void cacheOptionsMatchSettingsOracle()
{
    const QJsonArray options = CachePolicy::options(
        QStringList{QStringLiteral("192.0.2.4")}, {});
    CHECK(options.size() == 3, "no cacheRoot option when drive discovery is empty");
    CHECK(options.at(0).toObject().value("id").toString() == "localAddonEnabled", "local addon option first");
    CHECK(options.at(1).toObject().value("id").toString() == "remoteHttps", "remote HTTPS option second");
    const QJsonArray remoteSelections = options.at(1).toObject().value("selections").toArray();
    CHECK(remoteSelections.size() == 2, "remote HTTPS includes Disabled plus IPv4 interface");
    CHECK(remoteSelections.at(1).toObject().value("val").toString() == "192.0.2.4", "IPv4 value propagates");

    const QJsonArray cacheSelections = options.at(2).toObject().value("selections").toArray();
    CHECK(cacheSelections.size() == 5, "cache exposes five legacy sizes");
    CHECK(cacheSelections.at(0).toObject().value("val").toDouble() == 0, "no-cache value");
    CHECK(cacheSelections.at(1).toObject().value("val").toDouble() == 2147483648.0, "2 GiB value");
    CHECK(cacheSelections.at(2).toObject().value("val").toDouble() == 5368709120.0, "5 GiB value");
    CHECK(cacheSelections.at(3).toObject().value("val").toDouble() == 10737418240.0, "10 GiB value");
    CHECK(cacheSelections.at(4).toObject().value("val").isNull(), "Infinity JSON-serializes to null");

    const QJsonArray withDrive = CachePolicy::options({}, QStringList{QStringLiteral("D:\\")});
    CHECK(withDrive.size() == 4, "cacheRoot option appears when locations exist");
    CHECK(withDrive.at(3).toObject().value("selections").toArray().at(0).toObject().value("name").toString()
              == QStringLiteral("D:"),
          "cacheRoot display name is two-character drive prefix");
}
static void cacheCoercionAndTrimmingMatchLegacy()
{
    CHECK(CachePolicy::legacyNumber(QJsonValue(2147483648.0)) == 2147483648.0,
          "numeric cacheSize propagates unchanged");
    CHECK(CachePolicy::legacyNumber(QJsonValue(QStringLiteral("5368709120"))) == 5368709120.0,
          "numeric string follows JavaScript Number coercion");
    CHECK(CachePolicy::legacyNumber(QJsonValue::Null) == 0.0, "null coerces to zero in cache math");
    CHECK(CachePolicy::legacyNumber(QJsonValue(true)) == 1.0, "true coerces to one");
    CHECK(std::isnan(CachePolicy::legacyNumber(QJsonValue(QStringLiteral("garbage")))),
          "invalid numeric string becomes NaN");

    const QVector<CacheEntry> activeEntries{
        {QStringLiteral("newest"), 60, 300, false},
        {QStringLiteral("active"), 60, 200, true},
        {QStringLiteral("oldest"), 60, 100, false},
    };
    CHECK(CachePolicy::planDeletions(activeEntries, 100.0) == QStringList{QStringLiteral("oldest")},
          "active-engine file counts toward threshold but is excluded from deletion");

    const QVector<CacheEntry> pressureEntries{
        {QStringLiteral("newest"), 60, 300, false},
        {QStringLiteral("middle"), 60, 200, false},
        {QStringLiteral("oldest"), 60, 100, false},
    };
    CHECK(CachePolicy::planDeletions(pressureEntries, 200.0, 20, 100)
              == (QStringList{QStringLiteral("middle"), QStringLiteral("oldest")}),
          "disk pressure lowers cache target via cacheSize + free - requiredSize");
}
static void clearCacheDeletesFilesAndProtectsActiveEngines()
{
    QTemporaryDir temp;
    CHECK(temp.isValid(), "clear-cache temporary directory exists");
    const QString root = CachePolicy::cachePath(temp.path(), QString());
    const QString normalPath = QDir(root).filePath(QStringLiteral("normal/cache.bin"));
    const QString activePath = QDir(root).filePath(QStringLiteral("active/cache.bin"));
    CHECK(QDir().mkpath(QFileInfo(normalPath).absolutePath()), "normal cache directory exists");
    CHECK(QDir().mkpath(QFileInfo(activePath).absolutePath()), "active cache directory exists");

    auto writeSized = [](const QString &path) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && file.write(QByteArray(64, 'x')) == 64;
    };
    CHECK(writeSized(normalPath), "normal cache fixture written");
    CHECK(writeSized(activePath), "active cache fixture written");

    const auto result = CachePolicy::clearCache(
        root, QJsonValue(0.0), 0, QStringList{QStringLiteral("active")}, std::nullopt, false);
    CHECK(result.current == 128, "clearCache accounts for all cached bytes");
    CHECK(result.deleted == 1, "clearCache counts one attempted non-active deletion");
    CHECK(!QFileInfo::exists(normalPath), "normal cached file is deleted");
    CHECK(QFileInfo::exists(activePath), "active-engine cached file is retained");
}

static void cachePathAndDelayedCleanupMatchLegacy()
{
    QTemporaryDir temp;
    const QString path = CachePolicy::cachePath(temp.path(), QStringLiteral("abc123"));
    CHECK(path == QDir(temp.path()).filePath(QStringLiteral("stremio-cache/abc123")),
          "cache path is cacheRoot/stremio-cache/key");
    CHECK(QDir(QDir(temp.path()).filePath(QStringLiteral("stremio-cache"))).exists(),
          "cache directory is created");

    CHECK(CacheCleanupDebouncer::legacyDelayMs == 10000, "settings cache cleanup delay is 10 seconds");
    int callbacks = 0;
    QJsonValue delivered;
    QEventLoop loop;
    CacheCleanupDebouncer debouncer([&](const QJsonValue &cacheSize) {
        ++callbacks;
        delivered = cacheSize;
        loop.quit();
    }, 5);
    debouncer.setOptionValues(QJsonObject{{QStringLiteral("cacheSize"), 2147483648.0}});
    debouncer.setOptionValues(QJsonObject{{QStringLiteral("cacheSize"), QStringLiteral("5368709120")}});
    QTimer::singleShot(250, &loop, &QEventLoop::quit);
    loop.exec();
    CHECK(callbacks == 1, "rapid cache-size changes coalesce to one cleanup");
    CHECK(delivered.toString() == QStringLiteral("5368709120"), "latest dynamic cacheSize reaches cleanup");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    settingsDefaultsMatchStremio42017();
    platformCacheDefaultsMatchLegacy();
    settingsExtendLoadSavePreserveLegacyDynamics();
    runtimeEnvironmentOverridesMatchModule105();
    appAndSettingsPathsMatchModule413();
    cacheOptionsMatchSettingsOracle();
    cacheCoercionAndTrimmingMatchLegacy();
    clearCacheDeletesFilesAndProtectsActiveEngines();
    cachePathAndDelayedCleanupMatchLegacy();

    std::printf(fails ? "FAILS: %d\n" : "colosseum_server_settings_cache_harness: ALL PASS\n", fails);
    return fails;
}
