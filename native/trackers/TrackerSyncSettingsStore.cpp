#include "TrackerSyncSettingsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace {

bool setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

bool safeRemoteAccountId(const QString &value)
{
    return !value.isEmpty() && value == value.trimmed() && value.size() <= 128
        && !value.contains(QChar(0)) && !value.contains(QLatin1String(".."))
        && !QDir::isAbsolutePath(value);
}

QString providerSettingsKey(TrackerProviderId providerId, const QString &remoteAccountId)
{
    return trackerProviderKey(providerId) + QChar(0x1f) + remoteAccountId;
}

} // namespace

TrackerSyncSettingsStore::TrackerSyncSettingsStore(const ProfilePaths &profile)
    : m_path(storagePath(profile))
{
    if (m_path.isEmpty()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker settings are unavailable for this profile.");
    } else {
        load();
    }
}

QString TrackerSyncSettingsStore::storagePath(const ProfilePaths &profile)
{
    if (profile.kind() == ProfilePaths::Kind::Sealed
        || profile.kind() == ProfilePaths::Kind::LegacyLocal
        || profile.profileRoot().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(profile.profileRoot()
                           + QLatin1String("/tracker-sync-settings.json"));
}

bool TrackerSyncSettingsStore::healthy(QString *error) const
{
    if (!m_healthy && error)
        *error = m_error;
    else if (error)
        error->clear();
    return m_healthy;
}

QString TrackerSyncSettingsStore::persistenceError() const
{
    return m_error;
}

TrackerGlobalSyncSettings TrackerSyncSettingsStore::globalSettings() const
{
    return m_global;
}

bool TrackerSyncSettingsStore::setGlobalSetting(const QString &key,
                                                bool enabled,
                                                QString *error)
{
    if (!healthy(error))
        return false;
    TrackerGlobalSyncSettings candidate = m_global;
    if (key == QLatin1String("trackerSyncEnabled"))
        candidate.trackerSyncEnabled = enabled;
    else if (key == QLatin1String("checkOnLaunch"))
        candidate.checkOnLaunch = enabled;
    else if (key == QLatin1String("backgroundDelivery"))
        candidate.backgroundDelivery = enabled;
    else if (key == QLatin1String("completionMessages"))
        candidate.completionMessages = enabled;
    else
        return setError(error, QStringLiteral("Tracker setting is not recognized."));

    if (!persist(candidate, m_providers, error))
        return false;
    m_global = candidate;
    m_fileExists = true;
    return true;
}

bool TrackerSyncSettingsStore::pullAutomatically(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    bool defaultEnabled) const
{
    const QString key = providerSettingsKey(providerId, remoteAccountId);
    const auto found = std::find_if(m_providers.cbegin(), m_providers.cend(),
        [&key](const ProviderSettings &entry) {
            return providerSettingsKey(entry.providerId, entry.remoteAccountId) == key;
        });
    return found == m_providers.cend() || !found->pullAutomaticallyConfigured
        ? defaultEnabled : found->pullAutomatically;
}

bool TrackerSyncSettingsStore::setPullAutomatically(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    bool enabled,
    QString *error)
{
    if (!healthy(error) || !safeRemoteAccountId(remoteAccountId)
        || trackerProviderKey(providerId).isEmpty()) {
        return setError(error, QStringLiteral("Tracker pull preference is invalid."));
    }
    QList<ProviderSettings> candidate = m_providers;
    const QString key = providerSettingsKey(providerId, remoteAccountId);
    auto found = std::find_if(candidate.begin(), candidate.end(),
        [&key](const ProviderSettings &entry) {
            return providerSettingsKey(entry.providerId, entry.remoteAccountId) == key;
        });
    if (found == candidate.end()) {
        candidate.append({providerId, remoteAccountId, enabled, true, 0});
    } else {
        found->pullAutomatically = enabled;
        found->pullAutomaticallyConfigured = true;
    }
    if (!persist(m_global, candidate, error))
        return false;
    m_providers = candidate;
    m_fileExists = true;
    return true;
}

qint64 TrackerSyncSettingsStore::lastSuccessfulSyncAtMs(
    TrackerProviderId providerId,
    const QString &remoteAccountId) const
{
    const QString key = providerSettingsKey(providerId, remoteAccountId);
    const auto found = std::find_if(m_providers.cbegin(), m_providers.cend(),
        [&key](const ProviderSettings &entry) {
            return providerSettingsKey(entry.providerId, entry.remoteAccountId) == key;
        });
    return found == m_providers.cend() ? 0 : found->lastSuccessfulSyncAtMs;
}

bool TrackerSyncSettingsStore::recordSuccessfulSync(
    TrackerProviderId providerId,
    const QString &remoteAccountId,
    qint64 syncedAtMs,
    QString *error)
{
    if (!healthy(error) || !safeRemoteAccountId(remoteAccountId)
        || trackerProviderKey(providerId).isEmpty() || syncedAtMs <= 0) {
        return setError(error, QStringLiteral("Tracker synchronization receipt is invalid."));
    }
    QList<ProviderSettings> candidate = m_providers;
    const QString key = providerSettingsKey(providerId, remoteAccountId);
    auto found = std::find_if(candidate.begin(), candidate.end(),
        [&key](const ProviderSettings &entry) {
            return providerSettingsKey(entry.providerId, entry.remoteAccountId) == key;
        });
    if (found == candidate.end()) {
        // A receipt is sync history, not a user pull preference. Preserve the
        // review-dependent default until the person changes this setting.
        candidate.append({providerId, remoteAccountId, false, false, syncedAtMs});
    } else {
        found->lastSuccessfulSyncAtMs = syncedAtMs;
    }
    if (!persist(m_global, candidate, error))
        return false;
    m_providers = candidate;
    m_fileExists = true;
    return true;
}

bool TrackerSyncSettingsStore::adoptPrivateStateFrom(
    const TrackerSyncSettingsStore &source,
    QString *error)
{
    if (!healthy(error) || !source.healthy(error))
        return false;
    if (m_fileExists || !source.m_fileExists)
        return true;

    // Pull/reconciliation choices can follow a moved connection, but outbound
    // consent and send preferences belong to the destination profile. Start
    // with the destination's defaults for those controls even when its file
    // has not been created yet.
    TrackerGlobalSyncSettings adoptedGlobal = source.m_global;
    adoptedGlobal.trackerSyncEnabled = m_global.trackerSyncEnabled;
    adoptedGlobal.backgroundDelivery = m_global.backgroundDelivery;
    if (!persist(adoptedGlobal, source.m_providers, error))
        return false;
    m_global = adoptedGlobal;
    m_providers = source.m_providers;
    m_fileExists = true;
    return true;
}

bool TrackerSyncSettingsStore::load()
{
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker settings could not be read.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker settings are malformed.");
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonObject global = root.value(QStringLiteral("global")).toObject();
    const QJsonValue providersValue = root.value(QStringLiteral("providers"));
    const int schemaVersion = root.value(QStringLiteral("version")).toInt();
    if ((schemaVersion != 1 && schemaVersion != 2)
        || !global.value(QStringLiteral("trackerSyncEnabled")).isBool()
        || !global.value(QStringLiteral("checkOnLaunch")).isBool()
        || !global.value(QStringLiteral("backgroundDelivery")).isBool()
        || !global.value(QStringLiteral("completionMessages")).isBool()
        || !providersValue.isArray()) {
        m_healthy = false;
        m_error = QStringLiteral("Tracker settings have an unsupported or invalid schema.");
        return false;
    }

    TrackerGlobalSyncSettings loadedGlobal;
    loadedGlobal.trackerSyncEnabled = global.value(QStringLiteral("trackerSyncEnabled")).toBool();
    loadedGlobal.checkOnLaunch = global.value(QStringLiteral("checkOnLaunch")).toBool();
    loadedGlobal.backgroundDelivery = global.value(QStringLiteral("backgroundDelivery")).toBool();
    loadedGlobal.completionMessages = global.value(QStringLiteral("completionMessages")).toBool();
    QList<ProviderSettings> loadedProviders;
    for (const QJsonValue &value : providersValue.toArray()) {
        if (!value.isObject()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker provider settings are malformed.");
            return false;
        }
        const QJsonObject row = value.toObject();
        const auto providerId = trackerProviderIdFromKey(
            row.value(QStringLiteral("providerId")).toString());
        const QString remoteAccountId = row.value(QStringLiteral("remoteAccountId")).toString();
        const QJsonValue pull = row.value(QStringLiteral("pullAutomatically"));
        const QJsonValue pullConfigured = row.value(QStringLiteral("pullAutomaticallyConfigured"));
        const QJsonValue syncedAt = row.value(QStringLiteral("lastSuccessfulSyncAtMs"));
        if (!providerId || !safeRemoteAccountId(remoteAccountId) || !pull.isBool()
            || (schemaVersion == 2 && !pullConfigured.isBool())
            || !syncedAt.isString()) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker provider settings are invalid.");
            return false;
        }
        bool timestampOk = false;
        const qint64 timestamp = syncedAt.toString().toLongLong(&timestampOk);
        if (!timestampOk || timestamp < 0) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker synchronization receipt time is invalid.");
            return false;
        }
        const QString key = providerSettingsKey(*providerId, remoteAccountId);
        if (std::any_of(loadedProviders.cbegin(), loadedProviders.cend(),
                        [&key](const ProviderSettings &entry) {
                            return providerSettingsKey(entry.providerId,
                                                      entry.remoteAccountId) == key;
                        })) {
            m_healthy = false;
            m_error = QStringLiteral("Tracker provider settings contain a duplicate account.");
            return false;
        }
        // Version 1 used the same `true` value for the post-review default and
        // an explicit opt-in. Treat it as unconfigured; preserve explicit
        // opt-outs, which were always written as false.
        const bool configured = schemaVersion == 2
            ? pullConfigured.toBool() : !pull.toBool();
        loadedProviders.append({*providerId, remoteAccountId, pull.toBool(), configured,
                                timestamp});
    }
    m_global = loadedGlobal;
    m_providers = loadedProviders;
    m_fileExists = true;
    return true;
}

bool TrackerSyncSettingsStore::persist(
    const TrackerGlobalSyncSettings &global,
    const QList<ProviderSettings> &providers,
    QString *error) const
{
    if (m_path.isEmpty())
        return setError(error, QStringLiteral("Tracker settings are unavailable for this profile."));
    QJsonArray providerRows;
    QList<ProviderSettings> sorted = providers;
    std::sort(sorted.begin(), sorted.end(), [](const ProviderSettings &left,
                                               const ProviderSettings &right) {
        const QString leftKey = providerSettingsKey(left.providerId, left.remoteAccountId);
        const QString rightKey = providerSettingsKey(right.providerId, right.remoteAccountId);
        return leftKey < rightKey;
    });
    for (const ProviderSettings &entry : sorted) {
        providerRows.append(QJsonObject{
            {QStringLiteral("providerId"), trackerProviderKey(entry.providerId)},
            {QStringLiteral("remoteAccountId"), entry.remoteAccountId},
            {QStringLiteral("pullAutomatically"), entry.pullAutomatically},
            {QStringLiteral("pullAutomaticallyConfigured"),
             entry.pullAutomaticallyConfigured},
            {QStringLiteral("lastSuccessfulSyncAtMs"),
             QString::number(entry.lastSuccessfulSyncAtMs)}});
    }
    const QJsonObject document{
        {QStringLiteral("version"), 2},
        {QStringLiteral("global"), QJsonObject{
            {QStringLiteral("trackerSyncEnabled"), global.trackerSyncEnabled},
            {QStringLiteral("checkOnLaunch"), global.checkOnLaunch},
            {QStringLiteral("backgroundDelivery"), global.backgroundDelivery},
            {QStringLiteral("completionMessages"), global.completionMessages}}},
        {QStringLiteral("providers"), providerRows}};
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return setError(error, QStringLiteral("Tracker settings directory is unavailable."));
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Tracker settings could not be written."));
    const QByteArray payload = QJsonDocument(document).toJson(QJsonDocument::Compact);
    if (file.write(payload) != payload.size())
        return setError(error, QStringLiteral("Tracker settings could not be saved."));
    if (!file.commit())
        return setError(error, QStringLiteral("Tracker settings could not be committed atomically."));
    if (error)
        error->clear();
    return true;
}
