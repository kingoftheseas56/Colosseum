// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePreferencesStore.h"

#include <QByteArray>
#include <QDate>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace {
constexpr auto kShowExplicitKey =
    "content/showExplicit";
constexpr auto kRememberSearchHistoryKey = "privacy/rememberSearchHistory";
constexpr auto kKeepActivityHistoryKey = "privacy/keepActivityHistory";
constexpr auto kSyncActivityHistoryKey = "privacy/syncActivityHistory";
constexpr auto kMainSyncProviderKey = "sync/mainSyncProvider";
constexpr auto kVinylTasteKey = "vinyl/taste";
constexpr auto kVinylFeedbackSnapshotKey = "vinyl/feedbackSnapshot";
constexpr auto kVinylRecommendationExposureSnapshotKey =
    "vinyl/recommendationExposureSnapshot";
constexpr auto kVinylHomeSelectionSnapshotKey =
    "vinyl/homeSelectionSnapshot";
constexpr int kVinylTasteVersion = 1;
constexpr qsizetype kMaximumVinylTasteBytes = 4 * 1024 * 1024;
constexpr int kMaximumVinylTasteArtists = 512;
constexpr qsizetype kMaximumVinylTasteTextBytes = 4096;
constexpr qsizetype kMaximumVinylFeedbackSnapshotBytes = 4 * 1024 * 1024;
constexpr qsizetype kMaximumVinylHomeSelectionSnapshotBytes = 16 * 1024;
constexpr qsizetype kMaximumVinylHomeSelectionStringBytes = 1024;
constexpr qsizetype kMaximumVinylHomeSelectionProfileIdBytes = 512;
constexpr qsizetype kMaximumVinylHomeSelectionSeedHashBytes = 256;
constexpr double kMaximumVinylHomeSelectionSerial = 9007199254740991.0;
constexpr auto kRatingsReviewsProviderOrderKey = "ratingsReviews/providerOrder";
constexpr auto kRatingsReviewsDefaultRatingKey = "ratingsReviews/defaultRatingDestinations";
constexpr auto kRatingsReviewsDefaultReviewKey = "ratingsReviews/defaultReviewDestinations";
constexpr auto kRatingsReviewsConversionGroup = "ratingsReviews/conversionMaps";

bool isValidVinylFeedbackSnapshot(const QString &snapshot) {
    if (snapshot.contains(QChar::Null)
        || snapshot.size() > kMaximumVinylFeedbackSnapshotBytes) {
        return false;
    }
    return snapshot.toUtf8().size() <= kMaximumVinylFeedbackSnapshotBytes;
}

bool isValidVinylRecommendationExposureSnapshot(const QString &snapshot) {
    if (snapshot.contains(QChar::Null)
        || snapshot.size() > kMaximumVinylFeedbackSnapshotBytes) {
        return false;
    }
    return snapshot.toUtf8().size() <= kMaximumVinylFeedbackSnapshotBytes;
}

bool isBoundedSelectionString(
    const QJsonValue &value,
    qsizetype maxBytes,
    bool allowEmpty,
    QString *text = nullptr) {
    if (!value.isString())
        return false;
    const QString candidate = value.toString();
    if (candidate.contains(QChar::Null)
        || candidate.toUtf8().size() > maxBytes
        || (!allowEmpty && candidate.isEmpty())) {
        return false;
    }
    if (text)
        *text = candidate;
    return true;
}

bool isValidVinylHomeSelectionKeys(
    const QJsonValue &value,
    int maxCount) {
    if (!value.isArray())
        return false;
    const QJsonArray keys = value.toArray();
    if (keys.size() > maxCount)
        return false;

    QSet<QString> seen;
    for (const QJsonValue &keyValue : keys) {
        QString key;
        if (!isBoundedSelectionString(
                keyValue, kMaximumVinylHomeSelectionStringBytes, false, &key)
            || seen.contains(key)) {
            return false;
        }
        seen.insert(key);
    }
    return true;
}

bool isValidVinylHomeSelectionSnapshot(const QString &snapshot) {
    if (snapshot.isEmpty())
        return true;
    if (snapshot.contains(QChar::Null)
        || snapshot.size() > kMaximumVinylHomeSelectionSnapshotBytes) {
        return false;
    }
    const QByteArray bytes = snapshot.toUtf8();
    if (bytes.size() > kMaximumVinylHomeSelectionSnapshotBytes)
        return false;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject object = document.object();
    const QSet<QString> allowedFields{
        QStringLiteral("version"),
        QStringLiteral("profileId"),
        QStringLiteral("day"),
        QStringLiteral("seedHash"),
        QStringLiteral("refreshSerial"),
        QStringLiteral("keys"),
        QStringLiteral("previousKeys")};
    if (object.size() < 6 || object.size() > 7)
        return false;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowedFields.contains(it.key()))
            return false;
    }

    const QJsonValue version = object.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != 1.0)
        return false;

    if (!isBoundedSelectionString(
            object.value(QStringLiteral("profileId")),
            kMaximumVinylHomeSelectionProfileIdBytes,
            false)
        || !isBoundedSelectionString(
            object.value(QStringLiteral("seedHash")),
            kMaximumVinylHomeSelectionSeedHashBytes,
            false)) {
        return false;
    }

    const QJsonValue dayValue = object.value(QStringLiteral("day"));
    QString day;
    if (!isBoundedSelectionString(dayValue, 10, false, &day)
        || day.size() != 10
        || day.at(4) != QLatin1Char('-')
        || day.at(7) != QLatin1Char('-')) {
        return false;
    }
    for (qsizetype index = 0; index < day.size(); ++index) {
        if (index == 4 || index == 7)
            continue;
        if (day.at(index) < QLatin1Char('0')
            || day.at(index) > QLatin1Char('9')) {
            return false;
        }
    }
    const QDate parsedDay = QDate::fromString(day, Qt::ISODate);
    if (!parsedDay.isValid() || parsedDay.toString(Qt::ISODate) != day)
        return false;

    const QJsonValue refreshSerial =
        object.value(QStringLiteral("refreshSerial"));
    if (!refreshSerial.isDouble())
        return false;
    const double serial = refreshSerial.toDouble();
    if (!std::isfinite(serial) || serial < 0.0
        || serial > kMaximumVinylHomeSelectionSerial
        || std::floor(serial) != serial) {
        return false;
    }

    if (!isValidVinylHomeSelectionKeys(
            object.value(QStringLiteral("keys")), 5)) {
        return false;
    }
    if (object.contains(QStringLiteral("previousKeys"))
        && !isValidVinylHomeSelectionKeys(
            object.value(QStringLiteral("previousKeys")), 10)) {
        return false;
    }
    return true;
}

bool containsNul(const QVariant &value) {
    switch (value.metaType().id()) {
    case QMetaType::QString:
        return value.toString().contains(QChar::Null);
    case QMetaType::QStringList:
        for (const QString &entry : value.toStringList()) {
            if (entry.contains(QChar::Null))
                return true;
        }
        return false;
    case QMetaType::QVariantList:
        for (const QVariant &entry : value.toList()) {
            if (containsNul(entry))
                return true;
        }
        return false;
    case QMetaType::QVariantMap:
        {
        const QVariantMap entries = value.toMap();
        for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
            if (it.key().contains(QChar::Null) || containsNul(it.value()))
                return true;
        }
        return false;
        }
    default:
        return false;
    }
}

bool isSaneTastePayload(const QVariant &payload) {
    if (containsNul(payload))
        return false;
    return QJsonDocument::fromVariant(payload)
               .toJson(QJsonDocument::Compact)
               .size()
        <= kMaximumVinylTasteBytes;
}

bool isTasteText(
    const QVariantMap &map,
    const QString &field,
    qsizetype maxBytes,
    bool allowEmpty,
    QString *value = nullptr,
    bool allowLegacyUtf16Bound = false) {
    const QVariant raw = map.value(field);
    if (raw.metaType().id() != QMetaType::QString)
        return false;
    const QString text = raw.toString();
    if (text.contains(QChar::Null)
        || (text.toUtf8().size() > maxBytes
            && !(allowLegacyUtf16Bound && text.size() <= 4096))
        || (!allowEmpty && text.trimmed().isEmpty())) {
        return false;
    }
    if (value)
        *value = text;
    return true;
}

QString vinylArtistKey(const QString &provider, const QString &providerId) {
    const QJsonArray identity{
        provider,
        QStringLiteral("artist"),
        providerId};
    return QString::fromUtf8(
        QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

QString vinylArtistLegacyId(const QString &provider, const QString &providerId) {
    if (provider == QStringLiteral("youtube-music"))
        return QStringLiteral("ytm:artist:") + providerId;
    return providerId;
}

bool isUuid(const QString &text) {
    const QUuid uuid = QUuid::fromString(text);
    return !uuid.isNull()
        && uuid.toString(QUuid::WithoutBraces).compare(text, Qt::CaseInsensitive) == 0;
}

bool normalizeVinylArtist(
    const QVariant &raw,
    bool allowLegacyCompatibility,
    QVariantMap *normalized,
    QString *legacyId,
    QString *error) {
    if (raw.metaType().id() != QMetaType::QVariantMap) {
        if (error)
            *error = QStringLiteral("Taste selection is not an artist map.");
        return false;
    }

    const QVariantMap input = raw.toMap();
    static const QSet<QString> allowedFields{
        QStringLiteral("key"), QStringLiteral("kind"), QStringLiteral("provider"),
        QStringLiteral("providerId"), QStringLiteral("name"), QStringLiteral("image"),
        QStringLiteral("canonicalMbid"), QStringLiteral("mappingStatus"),
        QStringLiteral("legacyId")};
    for (auto it = input.cbegin(); it != input.cend(); ++it) {
        if (!allowedFields.contains(it.key())) {
            if (error)
                *error = QStringLiteral("Taste selection contains an unsupported field.");
            return false;
        }
    }

    QString key;
    QString kind;
    QString provider;
    QString providerId;
    QString name;
    QString image;
    QString mappingStatus;
    if (!isTasteText(input, QStringLiteral("key"),
                     allowLegacyCompatibility ? 32 * 1024 : kMaximumVinylTasteTextBytes * 2,
                     false, &key)
        || !isTasteText(input, QStringLiteral("kind"), 32, false, &kind)
        || !isTasteText(input, QStringLiteral("provider"), 64, false, &provider)
        || !isTasteText(input, QStringLiteral("providerId"), kMaximumVinylTasteTextBytes,
                        true, &providerId, allowLegacyCompatibility)
        || !isTasteText(input, QStringLiteral("name"), kMaximumVinylTasteTextBytes,
                        false, &name, allowLegacyCompatibility)
        || !isTasteText(input, QStringLiteral("image"), 64 * 1024, true, &image)
        || !isTasteText(input, QStringLiteral("mappingStatus"), 128, false, &mappingStatus)) {
        if (error)
            *error = QStringLiteral("Taste artist fields are malformed or oversized.");
        return false;
    }

    if (kind != QStringLiteral("artist")
        || (provider != QStringLiteral("youtube-music")
            && provider != QStringLiteral("local")
            && provider != QStringLiteral("musicbrainz"))) {
        if (error)
            *error = QStringLiteral("Taste artist provider or kind is unsupported.");
        return false;
    }

    const QString expectedLegacyId = vinylArtistLegacyId(provider, providerId);
    if (expectedLegacyId.size() > kMaximumVinylTasteTextBytes) {
        if (error)
            *error = QStringLiteral("Taste artist legacy ID is oversized.");
        return false;
    }

    static const QSet<QString> mappingStatuses{
        QStringLiteral("unresolved"), QStringLiteral("ambiguous"),
        QStringLiteral("unavailable"), QStringLiteral("verified")};
    if (!mappingStatuses.contains(mappingStatus)) {
        if (error)
            *error = QStringLiteral("Taste artist mapping status is unsupported.");
        return false;
    }

    const QVariant suppliedLegacyId = input.value(QStringLiteral("legacyId"));
    const bool retainedLegacyYoutube = allowLegacyCompatibility
        && provider == QStringLiteral("youtube-music")
        && suppliedLegacyId.metaType().id() == QMetaType::QString
        && suppliedLegacyId.toString() == expectedLegacyId;
    if (providerId.trimmed().isEmpty() && !retainedLegacyYoutube) {
        if (error)
            *error = QStringLiteral("Taste artist provider ID is empty.");
        return false;
    }
    if (provider == QStringLiteral("musicbrainz") && !isUuid(providerId)) {
        if (error)
            *error = QStringLiteral("MusicBrainz artist IDs must be UUIDs.");
        return false;
    }

    const QString expectedKey = vinylArtistKey(provider, providerId);
    if (key != expectedKey) {
        if (error)
            *error = QStringLiteral("Taste artist key does not match its provider identity.");
        return false;
    }
    const qsizetype maximumKeyBytes = allowLegacyCompatibility
        ? static_cast<qsizetype>(providerId.size()) * 6 + 128
        : kMaximumVinylTasteTextBytes * 2;
    if (expectedKey.toUtf8().size() > maximumKeyBytes) {
        if (error)
            *error = QStringLiteral("Taste artist key is oversized.");
        return false;
    }
    if (expectedLegacyId.toUtf8().size() > kMaximumVinylTasteTextBytes
        && !allowLegacyCompatibility) {
        if (error)
            *error = QStringLiteral("Taste artist legacy ID is oversized.");
        return false;
    }
    if (input.contains(QStringLiteral("legacyId"))) {
        QString suppliedLegacyId;
        if (!isTasteText(input, QStringLiteral("legacyId"), kMaximumVinylTasteTextBytes,
                         false, &suppliedLegacyId, allowLegacyCompatibility)
            || suppliedLegacyId != expectedLegacyId) {
            if (error)
                *error = QStringLiteral("Taste artist legacy ID does not match its provider identity.");
            return false;
        }
    }

    QVariantMap result{
        {QStringLiteral("key"), key},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("name"), name},
        {QStringLiteral("image"), image},
        {QStringLiteral("mappingStatus"), mappingStatus}};
    if (input.contains(QStringLiteral("legacyId")))
        result.insert(QStringLiteral("legacyId"), expectedLegacyId);
    if (mappingStatus == QStringLiteral("verified")
        && !input.contains(QStringLiteral("canonicalMbid"))) {
        if (error)
            *error = QStringLiteral("Verified taste artists require a canonical MusicBrainz UUID.");
        return false;
    }
    if (input.contains(QStringLiteral("canonicalMbid"))) {
        QString canonicalMbid;
        if (!isTasteText(input, QStringLiteral("canonicalMbid"), 36, false, &canonicalMbid)
            || !isUuid(canonicalMbid)
            || mappingStatus != QStringLiteral("verified")) {
            if (error)
                *error = QStringLiteral("Canonical MusicBrainz UUIDs require verified mapping status.");
            return false;
        }
        result.insert(QStringLiteral("canonicalMbid"), canonicalMbid);
    }

    if (normalized)
        *normalized = result;
    if (legacyId)
        *legacyId = expectedLegacyId;
    return true;
}

QVariantMap vinylArtistFromLegacyId(const QString &id) {
    QString provider = QStringLiteral("local");
    QString providerId = id;
    const QString youtubePrefix = QStringLiteral("ytm:artist:");
    if (id.startsWith(youtubePrefix)) {
        provider = QStringLiteral("youtube-music");
        providerId = id.mid(youtubePrefix.size());
    }

    return QVariantMap{
        {QStringLiteral("key"), vinylArtistKey(provider, providerId)},
        {QStringLiteral("kind"), QStringLiteral("artist")},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("providerId"), providerId},
        {QStringLiteral("name"), id},
        {QStringLiteral("image"), QString()},
        {QStringLiteral("mappingStatus"), QStringLiteral("unresolved")},
        {QStringLiteral("legacyId"), id}};
}

bool isLegacyCompatibilitySelection(const QVariantMap &artist) {
    const QString provider = artist.value(QStringLiteral("provider")).toString();
    const QString providerId = artist.value(QStringLiteral("providerId")).toString();
    const QString key = artist.value(QStringLiteral("key")).toString();
    const QString name = artist.value(QStringLiteral("name")).toString();
    const QString legacyId = artist.value(QStringLiteral("legacyId")).toString();
    if (key != vinylArtistKey(provider, providerId)
        || legacyId != vinylArtistLegacyId(provider, providerId)) {
        return false;
    }

    return providerId.toUtf8().size() > kMaximumVinylTasteTextBytes
        || name.toUtf8().size() > kMaximumVinylTasteTextBytes
        || legacyId.toUtf8().size() > kMaximumVinylTasteTextBytes
        || key.toUtf8().size() > kMaximumVinylTasteTextBytes * 2
        || (provider == QStringLiteral("youtube-music") && providerId.trimmed().isEmpty());
}

bool matchesExistingLegacyIdentity(
    const QVariant &candidate,
    const QVariantList &existingArtists) {
    if (candidate.metaType().id() != QMetaType::QVariantMap)
        return false;
    const QVariantMap input = candidate.toMap();
    for (const QVariant &existing : existingArtists) {
        if (existing.metaType().id() != QMetaType::QVariantMap)
            continue;
        const QVariantMap previous = existing.toMap();
        if (!isLegacyCompatibilitySelection(previous))
            continue;

        bool identityMatches = true;
        for (const QString &field : {QStringLiteral("key"), QStringLiteral("provider"),
                                     QStringLiteral("providerId"), QStringLiteral("legacyId")}) {
            const QVariant candidateValue = input.value(field);
            const QVariant previousValue = previous.value(field);
            if (candidateValue.metaType().id() != QMetaType::QString
                || previousValue.metaType().id() != QMetaType::QString
                || candidateValue.toString() != previousValue.toString()) {
                identityMatches = false;
                break;
            }
        }
        if (!identityMatches)
            continue;

        // If a caller changes a field that needs the legacy size allowance,
        // the value must still be the one retained from the trusted selection.
        for (const QString &field : {QStringLiteral("key"), QStringLiteral("providerId"),
                                     QStringLiteral("name"), QStringLiteral("legacyId")}) {
            const QVariant candidateValue = input.value(field);
            if (candidateValue.metaType().id() != QMetaType::QString)
                continue;
            const qsizetype strictByteLimit = field == QStringLiteral("key")
                ? kMaximumVinylTasteTextBytes * 2
                : kMaximumVinylTasteTextBytes;
            if (candidateValue.toString().toUtf8().size() > strictByteLimit
                && candidateValue.toString() != previous.value(field).toString()) {
                identityMatches = false;
                break;
            }
        }
        if (identityMatches)
            return true;
    }
    return false;
}

bool normalizeVinylArtists(
    const QVariantList &input,
    bool answered,
    bool allowLegacyCompatibility,
    QVariantList *artists,
    QStringList *artistIds,
    QString *error,
    const QVariantList *existingLegacyArtists = nullptr) {
    if (input.size() > kMaximumVinylTasteArtists || (!answered && !input.isEmpty())) {
        if (error)
            *error = QStringLiteral("Taste selections exceed their limit or are unanswered.");
        return false;
    }

    QVariantList normalized;
    QStringList ids;
    QSet<QString> keys;
    QSet<QString> legacyIds;
    for (const QVariant &raw : input) {
        QVariantMap artist;
        QString legacyId;
        const bool preserveLegacyIdentity = allowLegacyCompatibility
            || (existingLegacyArtists
                && matchesExistingLegacyIdentity(raw, *existingLegacyArtists));
        if (!normalizeVinylArtist(raw, preserveLegacyIdentity, &artist, &legacyId, error))
            return false;
        const QString key = artist.value(QStringLiteral("key")).toString();
        if (keys.contains(key) || legacyIds.contains(legacyId)) {
            if (error)
                *error = QStringLiteral("Taste selections contain duplicate artist identities.");
            return false;
        }
        keys.insert(key);
        legacyIds.insert(legacyId);
        ids.append(legacyId);
        normalized.append(artist);
    }

    if (artists)
        *artists = normalized;
    if (artistIds)
        *artistIds = ids;
    return true;
}

bool isValidLegacyTasteIds(const QStringList &ids, bool answered) {
    if (ids.size() > kMaximumVinylTasteArtists || (!answered && !ids.isEmpty()))
        return false;
    QSet<QString> seen;
    for (const QString &id : ids) {
        if (id.trimmed().isEmpty() || id.contains(QChar::Null)
            || id.size() > kMaximumVinylTasteTextBytes || seen.contains(id)) {
            return false;
        }
        seen.insert(id);
    }
    return true;
}
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

bool ProfilePreferencesStore::setVinylTaste(bool answered, const QStringList &artistIds) {
    if (!isValidLegacyTasteIds(artistIds, answered)) {
        m_vinylPersistenceError = QStringLiteral("Taste choices are malformed or oversized.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    QVariantList legacyArtists;
    legacyArtists.reserve(artistIds.size());
    for (const QString &id : artistIds)
        legacyArtists.append(vinylArtistFromLegacyId(id));

    QVariantList normalizedArtists;
    QStringList projectedIds;
    QString validationError;
    if (!normalizeVinylArtists(legacyArtists, answered, true,
                               &normalizedArtists, &projectedIds, &validationError)
        || projectedIds != artistIds) {
        m_vinylPersistenceError = validationError.isEmpty()
            ? QStringLiteral("Taste choices are malformed or oversized.")
            : validationError;
        emit vinylPersistenceErrorChanged();
        return false;
    }
    return persistVinylTaste(answered, artistIds, normalizedArtists);
}

bool ProfilePreferencesStore::setVinylTasteSelections(
    bool answered,
    const QVariantList &artists) {
    QVariantList normalizedArtists;
    QStringList artistIds;
    QString validationError;
    if (!normalizeVinylArtists(artists, answered, false,
                               &normalizedArtists, &artistIds, &validationError,
                               &m_vinylTasteArtists)) {
        m_vinylPersistenceError = validationError;
        emit vinylPersistenceErrorChanged();
        return false;
    }
    return persistVinylTaste(answered, artistIds, normalizedArtists);
}

bool ProfilePreferencesStore::persistVinylTaste(
    bool answered,
    const QStringList &artistIds,
    const QVariantList &artists) {
    const QVariantMap payload{
        {QStringLiteral("answered"), answered},
        {QStringLiteral("artistIds"), artistIds},
        {QStringLiteral("artists"), artists},
        {QStringLiteral("version"), kVinylTasteVersion}};
    if (!isSaneTastePayload(payload)) {
        m_vinylPersistenceError = QStringLiteral("Taste choices contain NUL or exceed 4 MiB.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    const QString key = QString::fromLatin1(kVinylTasteKey);
    const bool existed = m_settings->contains(key);
    const QVariant previous = m_settings->value(key);
    m_settings->setValue(key, payload);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        if (existed)
            m_settings->setValue(key, previous);
        else
            m_settings->remove(key);
        m_vinylPersistenceError = QStringLiteral("Taste choices could not be saved.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    m_vinylTasteAnswered = answered;
    m_vinylTasteArtistIds = artistIds;
    m_vinylTasteArtists = artists;
    m_vinylPersistenceError.clear();
    ++m_revision;
    emit vinylTasteChanged();
    emit vinylPersistenceErrorChanged();
    emit changed();
    return true;
}

bool ProfilePreferencesStore::setVinylFeedbackSnapshot(const QString &snapshot) {
    if (!isValidVinylFeedbackSnapshot(snapshot)) {
        m_vinylPersistenceError = QStringLiteral(
            "Feedback snapshot must be at most 4 MiB of UTF-8 and contain no NUL.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    const QString key = QString::fromLatin1(kVinylFeedbackSnapshotKey);
    const bool existed = m_settings->contains(key);
    const QVariant previous = m_settings->value(key);
    m_settings->setValue(key, snapshot);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        if (existed)
            m_settings->setValue(key, previous);
        else
            m_settings->remove(key);
        m_vinylPersistenceError = QStringLiteral(
            "Feedback snapshot could not be saved.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    m_vinylFeedbackSnapshot = snapshot;
    m_vinylPersistenceError.clear();
    ++m_revision;
    emit vinylFeedbackChanged();
    emit vinylPersistenceErrorChanged();
    emit changed();
    return true;
}

bool ProfilePreferencesStore::setVinylRecommendationExposureSnapshot(
    const QString &snapshot) {
    if (!isValidVinylRecommendationExposureSnapshot(snapshot)) {
        m_vinylPersistenceError = QStringLiteral(
            "Recommendation exposure snapshot must be at most 4 MiB of UTF-8 and contain no NUL.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    const QString key = QString::fromLatin1(
        kVinylRecommendationExposureSnapshotKey);
    const bool existed = m_settings->contains(key);
    const QVariant previous = m_settings->value(key);
    m_settings->setValue(key, snapshot);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        if (existed)
            m_settings->setValue(key, previous);
        else
            m_settings->remove(key);
        m_vinylPersistenceError = QStringLiteral(
            "Recommendation exposure snapshot could not be saved.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    m_vinylRecommendationExposureSnapshot = snapshot;
    m_vinylPersistenceError.clear();
    ++m_revision;
    emit vinylRecommendationExposureChanged();
    emit vinylPersistenceErrorChanged();
    emit changed();
    return true;
}

bool ProfilePreferencesStore::setVinylHomeSelectionSnapshot(
    const QString &snapshot) {
    if (!isValidVinylHomeSelectionSnapshot(snapshot)) {
        m_vinylPersistenceError = QStringLiteral(
            "Home selection snapshot is malformed or exceeds 16 KiB.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    const QString key = QString::fromLatin1(kVinylHomeSelectionSnapshotKey);
    const bool existed = m_settings->contains(key);
    const QVariant previous = m_settings->value(key);
    m_settings->setValue(key, snapshot);
    m_settings->sync();
    if (m_settings->status() != QSettings::NoError) {
        if (existed)
            m_settings->setValue(key, previous);
        else
            m_settings->remove(key);
        m_vinylPersistenceError = QStringLiteral(
            "Home selection snapshot could not be saved.");
        emit vinylPersistenceErrorChanged();
        return false;
    }

    m_vinylHomeSelectionSnapshot = snapshot;
    m_vinylPersistenceError.clear();
    ++m_revision;
    emit vinylHomeSelectionChanged();
    emit vinylPersistenceErrorChanged();
    emit changed();
    return true;
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
    const QString vinylTasteKey = QString::fromLatin1(kVinylTasteKey);
    if (m_settings->contains(vinylTasteKey)) {
        const QVariant raw = m_settings->value(vinylTasteKey);
        const QVariantMap taste = raw.toMap();
        const QVariant answered = taste.value(QStringLiteral("answered"));
        const QVariant ids = taste.value(QStringLiteral("artistIds"));
        const bool hasArtists = taste.contains(QStringLiteral("artists"));
        const bool hasVersion = taste.contains(QStringLiteral("version"));
        bool valid = raw.metaType().id() == QMetaType::QVariantMap
            && answered.metaType().id() == QMetaType::Bool
            && (ids.metaType().id() == QMetaType::QStringList
                || ids.metaType().id() == QMetaType::QVariantList);

        QStringList checkedIds;
        const QVariantList values = ids.toList();
        valid = valid && values.size() <= kMaximumVinylTasteArtists;
        for (const QVariant &value : values) {
            if (value.metaType().id() != QMetaType::QString) {
                valid = false;
                break;
            }
            checkedIds.append(value.toString());
        }
        const bool answeredValue = answered.toBool();
        valid = valid && isValidLegacyTasteIds(checkedIds, answeredValue);

        QVariantList loadedArtists;
        QStringList projectedIds;
        QString validationError;
        if (valid && !hasArtists && !hasVersion) {
            QVariantList migrated;
            migrated.reserve(checkedIds.size());
            for (const QString &id : checkedIds)
                migrated.append(vinylArtistFromLegacyId(id));
            valid = normalizeVinylArtists(
                migrated, answeredValue, true, &loadedArtists, &projectedIds,
                &validationError)
                && projectedIds == checkedIds;
        } else if (valid) {
            const QVariant version = taste.value(QStringLiteral("version"));
            const QVariant rawArtists = taste.value(QStringLiteral("artists"));
            valid = isSaneTastePayload(raw)
                && hasArtists && hasVersion
                && version.metaType().id() == QMetaType::Int
                && version.toInt() == kVinylTasteVersion
                && rawArtists.metaType().id() == QMetaType::QVariantList;
            if (valid) {
                valid = normalizeVinylArtists(
                    rawArtists.toList(), answeredValue, true, &loadedArtists,
                    &projectedIds, &validationError)
                    && projectedIds == checkedIds;
            }
        }

        if (valid) {
            m_vinylTasteAnswered = answeredValue;
            m_vinylTasteArtistIds = checkedIds;
            m_vinylTasteArtists = loadedArtists;
        } else {
            m_vinylPersistenceError = validationError.isEmpty()
                ? QStringLiteral("Saved taste choices are malformed and could not be loaded.")
                : validationError;
        }
    }

    const QString feedbackSnapshotKey =
        QString::fromLatin1(kVinylFeedbackSnapshotKey);
    if (m_settings->contains(feedbackSnapshotKey)) {
        const QVariant raw = m_settings->value(feedbackSnapshotKey);
        const QString snapshot = raw.toString();
        if (raw.metaType().id() == QMetaType::QString
            && isValidVinylFeedbackSnapshot(snapshot)) {
            m_vinylFeedbackSnapshot = snapshot;
        } else {
            m_vinylPersistenceError = QStringLiteral(
                "Saved feedback snapshot is malformed and could not be loaded.");
        }
    }

    const QString recommendationExposureSnapshotKey = QString::fromLatin1(
        kVinylRecommendationExposureSnapshotKey);
    if (m_settings->contains(recommendationExposureSnapshotKey)) {
        const QVariant raw = m_settings->value(
            recommendationExposureSnapshotKey);
        const QString snapshot = raw.toString();
        if (raw.metaType().id() == QMetaType::QString
            && isValidVinylRecommendationExposureSnapshot(snapshot)) {
            m_vinylRecommendationExposureSnapshot = snapshot;
        } else {
            m_vinylPersistenceError = QStringLiteral(
                "Saved recommendation exposure snapshot is malformed and could not be loaded.");
        }
    }

    const QString homeSelectionSnapshotKey = QString::fromLatin1(
        kVinylHomeSelectionSnapshotKey);
    if (m_settings->contains(homeSelectionSnapshotKey)) {
        const QVariant raw = m_settings->value(homeSelectionSnapshotKey);
        const QString snapshot = raw.toString();
        if (raw.metaType().id() == QMetaType::QString
            && isValidVinylHomeSelectionSnapshot(snapshot)) {
            m_vinylHomeSelectionSnapshot = snapshot;
        } else {
            m_vinylPersistenceError = QStringLiteral(
                "Saved home selection snapshot is malformed and could not be loaded.");
        }
    }

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
