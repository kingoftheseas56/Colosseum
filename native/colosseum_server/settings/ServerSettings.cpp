#include "ServerSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

namespace colosseum::server {
namespace {
constexpr auto kServerName = "stremio-server";
constexpr auto kServerVersion = "4.20.17";

QString joinNative(const QString &base, const QString &child)
{
    return QDir::toNativeSeparators(QDir(base).filePath(child));
}
} // namespace

ServerSettings::ServerSettings(QString appPath,
                               Platform platform,
                               QString settingsDirectory,
                               bool disableCaching)
{
    if (appPath.isEmpty()) {
        appPath = resolveAppPath(platform);
    }
    m_appPath = std::move(appPath);
    if (settingsDirectory.isEmpty()) {
        settingsDirectory = qEnvironmentVariable("SETTINGS_PATH");
    }
    m_settingsDirectory = settingsDirectory.isEmpty() ? m_appPath : std::move(settingsDirectory);
    const bool cachingDisabledByEnvironment = !qEnvironmentVariableIsEmpty("DISABLE_CACHING");
    m_values = defaults(m_appPath, platform, disableCaching || cachingDisabledByEnvironment);
    load();
}

QString ServerSettings::resolveAppPath(Platform platform,
                                       const QProcessEnvironment &environment,
                                       QString tempPath)
{
    const QString overridePath = environment.value(QStringLiteral("APP_PATH"));
    if (!overridePath.isEmpty()) {
        return overridePath;
    }
    if (tempPath.isEmpty()) {
        tempPath = QDir::tempPath();
    }

    const QString home = environment.value(QStringLiteral("HOME"));
    switch (platform) {
    case Platform::Windows: {
        const QString appData = environment.value(QStringLiteral("APPDATA"));
        return joinNative(appData, QStringLiteral("stremio/") + QString::fromLatin1(kServerName));
    }
    case Platform::Linux:
        return QDir::cleanPath(home + QStringLiteral("/.") + QString::fromLatin1(kServerName));
    case Platform::MacOS:
        return QDir::cleanPath(home + QStringLiteral("/Library/Application Support/")
                               + QString::fromLatin1(kServerName));
    case Platform::Android:
    case Platform::Other:
        return QDir::cleanPath(tempPath + QLatin1Char('/') + QString::fromLatin1(kServerName));
    }
    return QDir::cleanPath(tempPath + QLatin1Char('/') + QString::fromLatin1(kServerName));
}

QJsonObject ServerSettings::defaults(const QString &appPath,
                                     Platform platform,
                                     bool disableCaching)
{
    const bool cachingDisabled = disableCaching || platform == Platform::Android;
    QJsonObject values;
    values.insert(QStringLiteral("appPath"), appPath);
    values.insert(QStringLiteral("cacheRoot"), appPath);
    values.insert(QStringLiteral("cacheSize"), cachingDisabled ? 0.0 : 2147483648.0);
    values.insert(QStringLiteral("btMaxConnections"), 55);
    values.insert(QStringLiteral("btHandshakeTimeout"), 20000);
    values.insert(QStringLiteral("btRequestTimeout"), 4000);
    values.insert(QStringLiteral("btDownloadSpeedSoftLimit"), 2621440);
    values.insert(QStringLiteral("btDownloadSpeedHardLimit"), 3670016);
    values.insert(QStringLiteral("btMinPeersForStable"), 5);
    values.insert(QStringLiteral("remoteHttps"), QString());
    values.insert(QStringLiteral("localAddonEnabled"), false);
    values.insert(QStringLiteral("transcodeHorsepower"), 0.75);
    values.insert(QStringLiteral("transcodeMaxBitRate"), 0);
    values.insert(QStringLiteral("transcodeConcurrency"), 1);
    values.insert(QStringLiteral("transcodeTrackConcurrency"), 1);
    values.insert(QStringLiteral("transcodeHardwareAccel"), true);
    values.insert(QStringLiteral("transcodeProfile"), QJsonValue::Null);
    values.insert(QStringLiteral("allTranscodeProfiles"), QJsonArray());
    values.insert(QStringLiteral("transcodeMaxWidth"), 1920);
    values.insert(QStringLiteral("proxyStreamsEnabled"), false);
    values.insert(QStringLiteral("serverVersion"), QString::fromLatin1(kServerVersion));
    return values;
}

QString ServerSettings::settingsFilePath() const
{
    return QDir(m_settingsDirectory).filePath(QStringLiteral("server-settings.json"));
}

void ServerSettings::extend(const QJsonObject &overrides)
{
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
        if (it.key() == QStringLiteral("serverVersion")) {
            continue;
        }
        m_values.insert(it.key(), it.value());
    }
    m_values.insert(QStringLiteral("serverVersion"), QString::fromLatin1(kServerVersion));
}

bool ServerSettings::load(QString *errorMessage)
{
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (errorMessage) {
            *errorMessage = parseError.errorString();
        }
        return false;
    }

    if (document.isObject()) {
        extend(document.object());
    }

    // Module 105 rewrites a successfully loaded file with the merged defaults
    // and any unknown legacy keys still present on the dynamic settings object.
    save();
    return true;
}

bool ServerSettings::save(QString *errorMessage) const
{
    QFile file(settingsFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    QByteArray serialized = QJsonDocument(m_values).toJson(QJsonDocument::Indented);
    if (serialized.endsWith('\n')) {
        serialized.chop(1);
    }
    if (file.write(serialized) != serialized.size()) {
        if (errorMessage) {
            *errorMessage = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace colosseum::server
