// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePreferencesStore.h"

#include <QJsonDocument>
#include <QSet>
#include <QSettings>

#include <algorithm>

namespace {
constexpr auto kShowExplicitKey =
    "content/showExplicit";
constexpr auto kRememberSearchHistoryKey = "privacy/rememberSearchHistory";
constexpr auto kKeepActivityHistoryKey = "privacy/keepActivityHistory";
constexpr auto kSyncActivityHistoryKey = "privacy/syncActivityHistory";
constexpr auto kMainSyncProviderKey = "sync/mainSyncProvider";
constexpr auto kRatingsReviewsProviderOrderKey = "ratingsReviews/providerOrder";
constexpr auto kRatingsReviewsDefaultRatingKey = "ratingsReviews/defaultRatingDestinations";
constexpr auto kRatingsReviewsDefaultReviewKey = "ratingsReviews/defaultReviewDestinations";
constexpr auto kRatingsReviewsConversionGroup = "ratingsReviews/conversionMaps";
}

ProfilePreferencesStore::
ProfilePreferencesStore(
    QObject *parent)
    : QObject(parent),
      m_settings(
          std::make_unique<QSettings>()) {
    setObjectName(
        QStringLiteral(
            "profilePreferencesStore"));
    load();
}

ProfilePreferencesStore::
ProfilePreferencesStore(
    const QString &iniPath,
    QObject *parent)
    : QObject(parent),
      m_settings(
          std::make_unique<QSettings>(
              iniPath,
              QSettings::IniFormat)) {
    setObjectName(
        QStringLiteral(
            "profilePreferencesStore"));
    load();
}

ProfilePreferencesStore::
ProfilePreferencesStore(
    const RatingsReviewsConversionTestHook &testHook,
    QObject *parent)
    : QObject(parent),
      m_settings(std::make_unique<QSettings>()),
      m_conversionTestHook(testHook) {
    setObjectName(QStringLiteral("profilePreferencesStore"));
    load();
}

ProfilePreferencesStore::
ProfilePreferencesStore(
    const QString &iniPath,
    const RatingsReviewsConversionTestHook &testHook,
    QObject *parent)
    : QObject(parent),
      m_settings(
          std::make_unique<QSettings>(
              iniPath,
              QSettings::IniFormat)),
      m_conversionTestHook(testHook) {
    setObjectName(
        QStringLiteral(
            "profilePreferencesStore"));
    load();
}

bool ProfilePreferencesStore::
showExplicit() const {
    return m_showExplicit;
}

bool ProfilePreferencesStore::
hasShowExplicitValue() const {
    return m_hasShowExplicitValue;
}

int ProfilePreferencesStore::
revision() const {
    return m_revision;
}

bool ProfilePreferencesStore::rememberSearchHistory() const { return m_rememberSearchHistory; }
bool ProfilePreferencesStore::keepActivityHistory() const { return m_keepActivityHistory; }
bool ProfilePreferencesStore::syncActivityHistory() const { return m_syncActivityHistory; }
QString ProfilePreferencesStore::mainSyncProvider() const { return m_mainSyncProvider; }

void ProfilePreferencesStore::
setShowExplicit(
    bool showExplicitValue) {
    commitShowExplicit(
        showExplicitValue,
        true);
}

void ProfilePreferencesStore::setRememberSearchHistory(bool enabled) {
    if (m_rememberSearchHistory == enabled)
        return;
    m_settings->setValue(QString::fromLatin1(kRememberSearchHistoryKey), enabled);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError)
        return;
    m_rememberSearchHistory = enabled;
    ++m_revision;
    emit rememberSearchHistoryChanged();
    emit changed();
}

void ProfilePreferencesStore::setKeepActivityHistory(bool enabled) {
    if (m_keepActivityHistory == enabled)
        return;
    m_settings->setValue(QString::fromLatin1(kKeepActivityHistoryKey), enabled);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError)
        return;
    m_keepActivityHistory = enabled;
    ++m_revision;
    emit keepActivityHistoryChanged();
    emit changed();
}

void ProfilePreferencesStore::setSyncActivityHistory(bool enabled) {
    if (m_syncActivityHistory == enabled)
        return;
    m_settings->setValue(QString::fromLatin1(kSyncActivityHistoryKey), enabled);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError)
        return;
    m_syncActivityHistory = enabled;
    ++m_revision;
    emit syncActivityHistoryChanged();
    emit changed();
}

bool ProfilePreferencesStore::setMainSyncProvider(const QString &provider) {
    return commitMainSyncProvider(provider, true);
}

bool ProfilePreferencesStore::
applySyncedShowExplicit(
    bool showExplicitValue) {
    return commitShowExplicit(
        showExplicitValue,
        false);
}

bool ProfilePreferencesStore::
clearSyncedShowExplicit() {
    if (!m_hasShowExplicitValue
        && !m_showExplicit) {
        return true;
    }

    m_settings->remove(
        QString::fromLatin1(
            kShowExplicitKey));
    m_settings->sync();

    if (m_settings->status()
        != QSettings::NoError) {
        return false;
    }

    const bool visibleChanged =
        m_showExplicit;

    m_showExplicit = false;
    m_hasShowExplicitValue = false;
    ++m_revision;

    if (visibleChanged)
        emit showExplicitChanged();

    emit changed();
    return true;
}

bool ProfilePreferencesStore::applySyncedMainSyncProvider(const QString &provider) {
    return commitMainSyncProvider(provider, false);
}

bool ProfilePreferencesStore::clearSyncedMainSyncProvider() {
    return commitMainSyncProvider(QString(), false);
}

bool ProfilePreferencesStore::
commitShowExplicit(
    bool showExplicitValue,
    bool localMutation) {
    if (m_showExplicit
            == showExplicitValue
        && m_hasShowExplicitValue) {
        return true;
    }

    m_settings->setValue(
        QString::fromLatin1(
            kShowExplicitKey),
        showExplicitValue);
    m_settings->sync();

    if (m_settings->status()
        != QSettings::NoError) {
        return false;
    }

    const bool visibleChanged =
        m_showExplicit
        != showExplicitValue;

    m_showExplicit =
        showExplicitValue;
    m_hasShowExplicitValue = true;
    ++m_revision;

    if (visibleChanged)
        emit showExplicitChanged();
    emit changed();

    if (localMutation)
        emit syncDirty();

    return true;
}

bool ProfilePreferencesStore::commitMainSyncProvider(
    const QString &provider,
    bool localMutation) {
    const QString normalized = provider.trimmed().toLower();
    if (!normalized.isEmpty() && normalized != QStringLiteral("stremio"))
        return false;
    if (m_mainSyncProvider == normalized)
        return true;
    if (normalized.isEmpty())
        m_settings->remove(QString::fromLatin1(kMainSyncProviderKey));
    else
        m_settings->setValue(QString::fromLatin1(kMainSyncProviderKey), normalized);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError)
        return false;
    m_mainSyncProvider = normalized;
    ++m_revision;
    emit mainSyncProviderChanged();
    emit changed();
    if (localMutation)
        emit stremioLinkDirty();
    return true;
}

void ProfilePreferencesStore::load() {
    m_hasShowExplicitValue =
        m_settings->contains(
            QString::fromLatin1(
                kShowExplicitKey));

    m_showExplicit =
        m_settings
            ->value(
                QString::fromLatin1(
                    kShowExplicitKey),
                false)
            .toBool();
    m_rememberSearchHistory = m_settings->value(QString::fromLatin1(kRememberSearchHistoryKey), true).toBool();
    m_keepActivityHistory = m_settings->value(QString::fromLatin1(kKeepActivityHistoryKey), true).toBool();
    m_syncActivityHistory = m_settings->value(QString::fromLatin1(kSyncActivityHistoryKey), true).toBool();
    const QString provider = m_settings->value(QString::fromLatin1(kMainSyncProviderKey)).toString().trimmed().toLower();
    m_mainSyncProvider = provider == QStringLiteral("stremio") ? provider : QString();
    loadRatingsReviewsPreferences();
}

QStringList ProfilePreferencesStore::ratingsReviewsProviderOrder() const {
    if (m_hasRatingsReviewsProviderOrder)
        return m_ratingsReviewsProviderOrder;
    return RatingsReviewsConversionMap::canonicalProviderIds();
}

bool ProfilePreferencesStore::hasRatingsReviewsProviderOrder() const {
    return m_hasRatingsReviewsProviderOrder;
}

QStringList ProfilePreferencesStore::
ratingsReviewsDefaultRatingDestinations() const {
    return m_ratingsReviewsDefaultRatingDestinations;
}

bool ProfilePreferencesStore::
hasRatingsReviewsDefaultRatingDestinations() const {
    return m_hasRatingsReviewsDefaultRatingDestinations;
}

QStringList ProfilePreferencesStore::
ratingsReviewsDefaultReviewDestinations() const {
    return m_ratingsReviewsDefaultReviewDestinations;
}

bool ProfilePreferencesStore::
hasRatingsReviewsDefaultReviewDestinations() const {
    return m_hasRatingsReviewsDefaultReviewDestinations;
}

bool ProfilePreferencesStore::
isExactCanonicalProviderList(const QStringList &providerIds) {
    const QStringList canonical =
        RatingsReviewsConversionMap::canonicalProviderIds();
    if (providerIds.size() != canonical.size())
        return false;
    QSet<QString> seen;
    for (const QString &id : providerIds) {
        if (!canonical.contains(id) || seen.contains(id))
            return false;
        seen.insert(id);
    }
    return true;
}

bool ProfilePreferencesStore::
isValidDestinationList(const QStringList &providerIds) const {
    QStringList canonical =
        RatingsReviewsConversionMap::canonicalProviderIds();
    if (m_conversionTestHook.syntheticDomainsEnabledForTests()) {
        canonical.append(QStringLiteral("fixture-a"));
        canonical.append(QStringLiteral("fixture-b"));
    }
    QSet<QString> seen;
    for (const QString &id : providerIds) {
        if (!canonical.contains(id) || seen.contains(id))
            return false;
        seen.insert(id);
    }
    return true;
}

QStringList ProfilePreferencesStore::normalizedProviderOrder(
    const QStringList &providerIds) {
    const QStringList canonical = RatingsReviewsConversionMap::canonicalProviderIds();
    if (providerIds.isEmpty())
        return QStringList();
    QSet<QString> seen;
    for (const QString &id : providerIds) {
        if (!canonical.contains(id) || seen.contains(id))
            return QStringList();
        seen.insert(id);
    }
    if (providerIds.size() == canonical.size())
        return providerIds;
    QStringList result = canonical;
    QList<int> positions;
    for (int i = 0; i < result.size(); ++i) {
        if (seen.contains(result.at(i)))
            positions.append(i);
    }
    for (int i = 0; i < providerIds.size(); ++i)
        result[positions.at(i)] = providerIds.at(i);
    return result;
}

bool ProfilePreferencesStore::persistStringList(
    const QString &key,
    const QStringList &values) {
    if (m_conversionTestHook.settingsFailureRequestedForTests())
        return false;
    m_settings->setValue(key, values);
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

bool ProfilePreferencesStore::setRatingsReviewsProviderOrder(
    const QStringList &providerIds) {
    if (providerIds.isEmpty())
        return false;
    const QStringList canonical = RatingsReviewsConversionMap::canonicalProviderIds();
    QSet<QString> moved;
    for (const QString &id : providerIds) {
        if (!canonical.contains(id) || moved.contains(id))
            return false;
        moved.insert(id);
    }
    QStringList normalized;
    if (providerIds.size() == canonical.size()) {
        normalized = providerIds;
    } else {
        normalized = ratingsReviewsProviderOrder();
        QList<int> positions;
        for (int i = 0; i < normalized.size(); ++i) {
            if (moved.contains(normalized.at(i)))
                positions.append(i);
        }
        if (positions.size() != providerIds.size())
            return false;
        for (int i = 0; i < providerIds.size(); ++i)
            normalized[positions.at(i)] = providerIds.at(i);
    }
    if (!isExactCanonicalProviderList(normalized))
        return false;
    if (m_hasRatingsReviewsProviderOrder
        && normalized == m_ratingsReviewsProviderOrder)
        return true;
    if (!persistStringList(
            QString::fromLatin1(kRatingsReviewsProviderOrderKey),
            normalized))
        return false;
    m_ratingsReviewsProviderOrder = normalized;
    m_hasRatingsReviewsProviderOrder = true;
    ++m_revision;
    emit ratingsReviewsPrivatePreferencesChanged();
    emit changed();
    return true;
}

bool ProfilePreferencesStore::setRatingsReviewsDefaultRatingDestinations(
    const QStringList &providerIds) {
    if (!isValidDestinationList(providerIds))
        return false;
    if (m_hasRatingsReviewsDefaultRatingDestinations
        && providerIds == m_ratingsReviewsDefaultRatingDestinations)
        return true;
    if (!persistStringList(
            QString::fromLatin1(kRatingsReviewsDefaultRatingKey),
            providerIds))
        return false;
    m_ratingsReviewsDefaultRatingDestinations = providerIds;
    m_hasRatingsReviewsDefaultRatingDestinations = true;
    ++m_revision;
    emit ratingsReviewsPrivatePreferencesChanged();
    emit changed();
    return true;
}

bool ProfilePreferencesStore::setRatingsReviewsDefaultReviewDestinations(
    const QStringList &providerIds) {
    if (!isValidDestinationList(providerIds))
        return false;
    if (m_hasRatingsReviewsDefaultReviewDestinations
        && providerIds == m_ratingsReviewsDefaultReviewDestinations)
        return true;
    if (!persistStringList(
            QString::fromLatin1(kRatingsReviewsDefaultReviewKey),
            providerIds))
        return false;
    m_ratingsReviewsDefaultReviewDestinations = providerIds;
    m_hasRatingsReviewsDefaultReviewDestinations = true;
    ++m_revision;
    emit ratingsReviewsPrivatePreferencesChanged();
    emit changed();
    return true;
}

QList<RatingsReviewsConversionMap>
ProfilePreferencesStore::ratingsReviewsConversionMaps() const {
    QList<RatingsReviewsConversionMap> result;
    QStringList keys = m_ratingsReviewsConversionMaps.keys();
    std::sort(keys.begin(), keys.end());
    result.reserve(keys.size());
    for (const QString &key : keys)
        result.append(m_ratingsReviewsConversionMaps.value(key));
    return result;
}

std::optional<RatingsReviewsConversionMap>
ProfilePreferencesStore::ratingsReviewsConversionMap(
    const QString &providerId) const {
    const auto it = m_ratingsReviewsConversionMaps.constFind(providerId);
    if (it == m_ratingsReviewsConversionMaps.cend())
        return std::nullopt;
    return it.value();
}

bool ProfilePreferencesStore::persistConversionMap(
    const RatingsReviewsConversionMap &map) {
    if (m_conversionTestHook.settingsFailureRequestedForTests())
        return false;
    const QString key = QString::fromLatin1(kRatingsReviewsConversionGroup)
        + QLatin1Char('/') + map.providerId;
    const QByteArray payload =
        QJsonDocument(map.toJson()).toJson(QJsonDocument::Compact);
    m_settings->setValue(key, payload);
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

bool ProfilePreferencesStore::removeConversionMap(
    const QString &providerId) {
    if (m_conversionTestHook.settingsFailureRequestedForTests())
        return false;
    const QString key = QString::fromLatin1(kRatingsReviewsConversionGroup)
        + QLatin1Char('/') + providerId;
    m_settings->remove(key);
    m_settings->sync();
    return m_settings->status() == QSettings::NoError;
}

bool ProfilePreferencesStore::commitRatingsReviewsConversionMap(
    const RatingsReviewsConversionMap &map,
    bool localMutation) {
    QString error;
    if (!RatingsReviewsConversionMap::validate(
            map, m_conversionTestHook, &error))
        return false;
    const auto existing = ratingsReviewsConversionMap(map.providerId);
    if (existing.has_value() && existing->digest() == map.digest())
        return true;
    if (!persistConversionMap(map))
        return false;

    m_ratingsReviewsConversionMaps.insert(map.providerId, map);
    ++m_revision;
    emit ratingsReviewsConversionMapsChanged();
    emit changed();
    if (localMutation)
        emit ratingsReviewsConversionSyncDirty();
    return true;
}

bool ProfilePreferencesStore::setRatingsReviewsConversionMap(
    const RatingsReviewsConversionMap &map) {
    return commitRatingsReviewsConversionMap(map, true);
}

bool ProfilePreferencesStore::applySyncedRatingsReviewsConversionMap(
    const RatingsReviewsConversionMap &map) {
    return commitRatingsReviewsConversionMap(map, false);
}

bool ProfilePreferencesStore::clearRatingsReviewsConversionMapInternal(
    const QString &providerId,
    bool localMutation) {
    const auto existing = ratingsReviewsConversionMap(providerId);
    if (!existing.has_value())
        return true;
    if (!removeConversionMap(providerId))
        return false;
    m_ratingsReviewsConversionMaps.remove(providerId);
    ++m_revision;
    emit ratingsReviewsConversionMapsChanged();
    emit changed();
    if (localMutation)
        emit ratingsReviewsConversionSyncDirty();
    return true;
}

bool ProfilePreferencesStore::clearRatingsReviewsConversionMap(
    const QString &providerId) {
    return clearRatingsReviewsConversionMapInternal(providerId, true);
}

bool ProfilePreferencesStore::clearSyncedRatingsReviewsConversionMap(
    const QString &providerId) {
    return clearRatingsReviewsConversionMapInternal(providerId, false);
}

bool ProfilePreferencesStore::ratingsReviewsConversionMapsHealthy(
    QString *error) const {
    if (error)
        *error = m_ratingsReviewsConversionMapsError;
    return m_ratingsReviewsConversionMapsHealthy;
}

void ProfilePreferencesStore::loadRatingsReviewsPreferences() {
    m_ratingsReviewsProviderOrder.clear();
    m_hasRatingsReviewsProviderOrder = false;
    const QString providerOrderKey =
        QString::fromLatin1(kRatingsReviewsProviderOrderKey);
    if (m_settings->contains(providerOrderKey)) {
        const QStringList saved = m_settings->value(providerOrderKey).toStringList();
        if (isExactCanonicalProviderList(saved)) {
            m_ratingsReviewsProviderOrder = saved;
            m_hasRatingsReviewsProviderOrder = true;
        }
    }
    const QString ratingKey = QString::fromLatin1(kRatingsReviewsDefaultRatingKey);
    m_ratingsReviewsDefaultRatingDestinations.clear();
    m_hasRatingsReviewsDefaultRatingDestinations = false;
    if (m_settings->contains(ratingKey)) {
        const QStringList saved = m_settings->value(ratingKey).toStringList();
        if (isValidDestinationList(saved)) {
            m_ratingsReviewsDefaultRatingDestinations = saved;
            m_hasRatingsReviewsDefaultRatingDestinations = true;
        }
    }

    const QString reviewKey = QString::fromLatin1(kRatingsReviewsDefaultReviewKey);
    m_ratingsReviewsDefaultReviewDestinations.clear();
    m_hasRatingsReviewsDefaultReviewDestinations = false;
    if (m_settings->contains(reviewKey)) {
        const QStringList saved = m_settings->value(reviewKey).toStringList();
        if (isValidDestinationList(saved)) {
            m_ratingsReviewsDefaultReviewDestinations = saved;
            m_hasRatingsReviewsDefaultReviewDestinations = true;
        }
    }

    m_ratingsReviewsConversionMaps.clear();
    m_ratingsReviewsConversionMapsHealthy = true;
    m_ratingsReviewsConversionMapsError.clear();
    m_settings->beginGroup(QString::fromLatin1(kRatingsReviewsConversionGroup));
    const QStringList keys = m_settings->childKeys();
    for (const QString &providerId : keys) {
        const QByteArray payload = m_settings->value(providerId).toByteArray();
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            m_ratingsReviewsConversionMapsHealthy = false;
            m_ratingsReviewsConversionMapsError =
                QStringLiteral("conversion_map_malformed:%1").arg(providerId);
            break;
        }
        QString validationError;
        const auto map = RatingsReviewsConversionMap::fromJson(
            document.object(), m_conversionTestHook, &validationError);
        if (!map.has_value() || map->providerId != providerId) {
            m_ratingsReviewsConversionMapsHealthy = false;
            m_ratingsReviewsConversionMapsError =
                validationError.isEmpty()
                    ? QStringLiteral("conversion_provider_key_mismatch:%1").arg(providerId)
                    : validationError;
            break;
        }
        m_ratingsReviewsConversionMaps.insert(providerId, *map);
    }
    m_settings->endGroup();
    if (!m_ratingsReviewsConversionMapsHealthy)
        m_ratingsReviewsConversionMaps.clear();
}
