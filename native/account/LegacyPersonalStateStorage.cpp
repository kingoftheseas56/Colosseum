// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "LegacyPersonalStateStorage.h"

#include "ProfilePaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMetaType>
#include <QSaveFile>
#include <QVariant>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace {
constexpr int kSnapshotVersion = 4;
constexpr qsizetype kMaximumPrivateStateBytes = 1024 * 1024;
constexpr qsizetype kMaximumTheatreExtensions = 512;

QString stremioStatePath(const QString &profileRoot) {
    return profileRoot.isEmpty()
        ? QString()
        : QDir(profileRoot).filePath(QStringLiteral("stremio-sync.json"));
}

QString theatreExtensionsPath(const QString &profileRoot) {
    return profileRoot.isEmpty()
        ? QString()
        : QDir(profileRoot).filePath(QStringLiteral("extensions/installed.json"));
}

bool readJsonFile(const QString &path, QJsonObject *object, QString *error) {
    *object = QJsonObject();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return true;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open device-private profile state.");
        return false;
    }
    const QByteArray payload = file.read(kMaximumPrivateStateBytes + 1);
    if (payload.size() > kMaximumPrivateStateBytes) {
        if (error)
            *error = QStringLiteral("Device-private profile state is too large.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QStringLiteral("Device-private profile state is malformed.");
        return false;
    }
    *object = document.object();
    return true;
}

bool writeJsonFile(const QString &path, const QJsonObject &object, QString *error) {
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error)
            *error = QStringLiteral("Could not prepare device-private profile state.");
        return false;
    }
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.size() > kMaximumPrivateStateBytes) {
        if (error)
            *error = QStringLiteral("Device-private profile state is too large.");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(payload) != payload.size()
        || !file.commit()) {
        if (error)
            *error = QStringLiteral("Could not commit device-private profile state.");
        return false;
    }
    return true;
}

bool removePrivateFile(const QString &path, QString *error) {
    if (path.isEmpty() || !QFileInfo::exists(path) || QFile::remove(path))
        return true;
    if (error)
        *error = QStringLiteral("Could not retire device-private profile state.");
    return false;
}

bool jsonObjectValue(
    QSettings *settings,
    const QString &key,
    QJsonObject *target,
    QString *error) {
    const QVariant raw =
        settings->value(key);
    if (!raw.isValid()) {
        *target = QJsonObject();
        return true;
    }

    const QByteArray payload =
        raw.toByteArray();
    if (payload.isEmpty()) {
        *target = QJsonObject();
        return true;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            payload,
            &parseError);
    if (parseError.error
            != QJsonParseError::NoError
        || !document.isObject()) {
        if (error) {
            *error = QStringLiteral(
                "Personal-state field '%1' contains malformed JSON.")
                .arg(key);
        }
        return false;
    }

    *target = document.object();
    return true;
}

QStringList cleanSearchEntries(
    const QVariant &value) {
    QStringList raw;
    if (!value.isValid())
        return {};

    if (value.metaType().id()
        == QMetaType::QString) {
        raw.append(value.toString());
    } else if (value.metaType().id()
               == QMetaType::QStringList) {
        raw = value.toStringList();
    } else {
        return {};
    }

    QStringList clean;
    for (const QString &entry : raw) {
        const QString trimmed = entry.trimmed();
        if (trimmed.size() < 2)
            continue;

        bool duplicate = false;
        const QString folded =
            trimmed.toCaseFolded();
        for (const QString &kept : clean) {
            if (kept.toCaseFolded() == folded) {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            clean.append(trimmed);
        if (clean.size() == 6)
            break;
    }
    return clean;
}

QJsonArray stringListArray(
    const QStringList &values) {
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

QStringList jsonStringList(
    const QJsonValue &value) {
    QStringList result;
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue &entry : array) {
        if (entry.isString())
            result.append(entry.toString());
    }
    return result;
}

void capturePrefix(
    QSettings *settings,
    const QString &prefix,
    QJsonObject *target) {
    const QString slashPrefix =
        prefix.endsWith(QLatin1Char('/'))
        ? prefix
        : prefix + QLatin1Char('/');

    for (const QString &key : settings->allKeys()) {
        if (!key.startsWith(slashPrefix))
            continue;

        const QString suffix =
            key.mid(slashPrefix.size());
        if (suffix.isEmpty())
            continue;

        const QVariant raw =
            settings->value(key);
        QJsonValue value =
            QJsonValue::fromVariant(raw);

        // These prefixes are integral domains (season, mark and action
        // timestamp), but INI persistence is stringly typed on disk while an
        // in-memory QSettings session still holds the typed QVariant. A
        // fresh re-open therefore yields a decimal string. Normalize through
        // qint64 so an epoch-millisecond action time is not truncated to int.
        if (value.isString()) {
            bool ok = false;
            const qint64 number = raw.toString().toLongLong(&ok);
            if (ok)
                value = static_cast<double>(number);
        }

        target->insert(suffix, value);
    }
}

bool snapshotObject(
    const QJsonObject &parent,
    const QString &key,
    QJsonObject *target,
    QString *error) {
    const QJsonValue value = parent.value(key);
    if (!value.isObject()) {
        if (error) {
            *error = QStringLiteral(
                "Personal-state snapshot field '%1' is invalid.")
                .arg(key);
        }
        return false;
    }

    *target = value.toObject();
    return true;
}
}

bool PersonalStateSnapshot::isEmpty() const {
    return progressEntries.isEmpty()
        && progressLastSeason.isEmpty()
        && progressWatchedMarks.isEmpty()
        && progressWatchedMarkActionTimes.isEmpty()
        && collectionEntries.isEmpty()
        && searchHistory.isEmpty()
        && audioPairings.isEmpty()
        && historyRecords.isEmpty()
        && !showExplicit
        && mainSyncProvider.isEmpty()
        && stremioState.isEmpty()
        && theatreExtensions.isEmpty();
}

QJsonObject PersonalStateSnapshot::toJson() const {
    QJsonObject object;
    object.insert(
        QStringLiteral("version"),
        kSnapshotVersion);
    object.insert(
        QStringLiteral("progress_entries"),
        progressEntries);
    object.insert(
        QStringLiteral("progress_last_season"),
        progressLastSeason);
    object.insert(
        QStringLiteral("progress_watched_marks"),
        progressWatchedMarks);
    object.insert(
        QStringLiteral("progress_watched_mark_action_times"),
        progressWatchedMarkActionTimes);
    object.insert(
        QStringLiteral("collection_entries"),
        collectionEntries);
    object.insert(
        QStringLiteral("search_history"),
        searchHistory);
    object.insert(
        QStringLiteral("audio_pairings"),
        audioPairings);
    object.insert(
        QStringLiteral("history_records"),
        historyRecords);
    object.insert(
        QStringLiteral("show_explicit"),
        showExplicit);
    object.insert(QStringLiteral("main_sync_provider"), mainSyncProvider);
    object.insert(QStringLiteral("stremio_state"), stremioState);
    object.insert(QStringLiteral("theatre_extensions"), theatreExtensions);
    return object;
}

QString PersonalStateSnapshot::semanticDigest() const {
    const QByteArray payload =
        QJsonDocument(toJson())
            .toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(
            payload,
            QCryptographicHash::Sha256)
            .toHex());
}

QString PersonalStateSnapshot::legacySemanticDigestV3() const {
    QJsonObject object;
    object.insert(QStringLiteral("version"), 3);
    object.insert(QStringLiteral("progress_entries"), progressEntries);
    object.insert(QStringLiteral("progress_last_season"), progressLastSeason);
    object.insert(QStringLiteral("progress_watched_marks"), progressWatchedMarks);
    object.insert(QStringLiteral("progress_watched_mark_action_times"),
                  progressWatchedMarkActionTimes);
    object.insert(QStringLiteral("collection_entries"), collectionEntries);
    object.insert(QStringLiteral("search_history"), searchHistory);
    object.insert(QStringLiteral("audio_pairings"), audioPairings);
    object.insert(QStringLiteral("history_records"), historyRecords);
    object.insert(QStringLiteral("show_explicit"), showExplicit);
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(object).toJson(QJsonDocument::Compact),
        QCryptographicHash::Sha256).toHex());
}

QString PersonalStateSnapshot::legacySemanticDigestV2() const {
    QJsonObject object;
    object.insert(QStringLiteral("version"), 2);
    object.insert(QStringLiteral("progress_entries"), progressEntries);
    object.insert(QStringLiteral("progress_last_season"), progressLastSeason);
    object.insert(QStringLiteral("progress_watched_marks"), progressWatchedMarks);
    object.insert(QStringLiteral("collection_entries"), collectionEntries);
    object.insert(QStringLiteral("search_history"), searchHistory);
    object.insert(QStringLiteral("audio_pairings"), audioPairings);
    object.insert(QStringLiteral("history_records"), historyRecords);
    object.insert(QStringLiteral("show_explicit"), showExplicit);
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

QString PersonalStateSnapshot::
legacySemanticDigestV1() const {
    QJsonObject object;
    object.insert(
        QStringLiteral("version"),
        1);
    object.insert(
        QStringLiteral("progress_entries"),
        progressEntries);
    object.insert(
        QStringLiteral("progress_last_season"),
        progressLastSeason);
    object.insert(
        QStringLiteral("progress_watched_marks"),
        progressWatchedMarks);
    object.insert(
        QStringLiteral("collection_entries"),
        collectionEntries);
    object.insert(
        QStringLiteral("search_history"),
        searchHistory);
    object.insert(
        QStringLiteral("audio_pairings"),
        audioPairings);
    object.insert(
        QStringLiteral("show_explicit"),
        showExplicit);

    const QByteArray payload =
        QJsonDocument(object)
            .toJson(
                QJsonDocument::Compact);
    return QString::fromLatin1(
        QCryptographicHash::hash(
            payload,
            QCryptographicHash::Sha256)
            .toHex());
}

bool PersonalStateSnapshot::
matchesSemanticDigest(
    const QString &digest) const {
    const QString normalized =
        digest.trimmed().toLower();

    if (normalized.isEmpty())
        return false;

    if (semanticDigest() == normalized)
        return true;

    if (mainSyncProvider.isEmpty() && stremioState.isEmpty()
        && theatreExtensions.isEmpty() && legacySemanticDigestV3() == normalized) {
        return true;
    }

    if (progressWatchedMarkActionTimes.isEmpty()
        && legacySemanticDigestV2() == normalized) {
        return true;
    }

    return historyRecords.isEmpty()
        && progressWatchedMarkActionTimes.isEmpty()
        && legacySemanticDigestV1()
            == normalized;
}

std::optional<PersonalStateSnapshot>
PersonalStateSnapshot::fromJson(
    const QJsonObject &object,
    QString *error) {
    const int version =
        object
            .value(
                QStringLiteral("version"))
            .toInt();
    if (version < 1 || version > kSnapshotVersion) {
        if (error) {
            *error = QStringLiteral(
                "The personal-state snapshot version is unsupported.");
        }
        return std::nullopt;
    }

    PersonalStateSnapshot snapshot;
    if (!snapshotObject(
            object,
            QStringLiteral("progress_entries"),
            &snapshot.progressEntries,
            error)
        || !snapshotObject(
            object,
            QStringLiteral("progress_last_season"),
            &snapshot.progressLastSeason,
            error)
        || !snapshotObject(
            object,
            QStringLiteral("progress_watched_marks"),
            &snapshot.progressWatchedMarks,
            error)
        || !snapshotObject(
            object,
            QStringLiteral("collection_entries"),
            &snapshot.collectionEntries,
            error)
        || !snapshotObject(
            object,
            QStringLiteral("search_history"),
            &snapshot.searchHistory,
            error)
        || !snapshotObject(
            object,
            QStringLiteral("audio_pairings"),
            &snapshot.audioPairings,
            error)) {
        return std::nullopt;
    }

    if (version >= 3) {
        if (!snapshotObject(
                object,
                QStringLiteral("progress_watched_mark_action_times"),
                &snapshot.progressWatchedMarkActionTimes,
                error)) {
            return std::nullopt;
        }
    }

    if (version >= 2) {
        if (!snapshotObject(
                object,
                QStringLiteral("history_records"),
                &snapshot.historyRecords,
                error)) {
            return std::nullopt;
        }
    } else {
        snapshot.historyRecords =
            QJsonObject();
    }

    const QJsonValue showExplicit =
        object.value(QStringLiteral("show_explicit"));
    if (!showExplicit.isBool()) {
        if (error) {
            *error = QStringLiteral(
                "The personal-state snapshot preference field is invalid.");
        }
        return std::nullopt;
    }

    snapshot.showExplicit =
        showExplicit.toBool();
    if (version >= 4) {
        const QJsonValue provider = object.value(QStringLiteral("main_sync_provider"));
        const QJsonValue stremio = object.value(QStringLiteral("stremio_state"));
        const QJsonValue extensions = object.value(QStringLiteral("theatre_extensions"));
        if (!provider.isString() || !stremio.isObject() || !extensions.isArray()
            || (provider.toString() != QLatin1String("stremio")
                && !provider.toString().isEmpty())
            || extensions.toArray().size() > kMaximumTheatreExtensions) {
            if (error)
                *error = QStringLiteral("The device-private adoption snapshot is invalid.");
            return std::nullopt;
        }
        snapshot.mainSyncProvider = provider.toString();
        snapshot.stremioState = stremio.toObject();
        snapshot.theatreExtensions = extensions.toArray();
    }
    return snapshot;
}

LegacyPersonalStateStorage
LegacyPersonalStateStorage::forCurrentInstallation() {
    const bool tagged =
        qEnvironmentVariableIsSet(
            "COLOSSEUM_APPDATA_TAG");

    Location progress;
    Location collection;
    Location searchHistory;
    Location audioPairing;
    Location preferences;
    Location history;

    // activity.sqlite has no QSettings-registry equivalent — it is always an
    // explicit file, tagged or not, under the same AppData root the tagged
    // Ini locations below use (§17 "Legacy-local mode").
    const QString activityRoot =
        QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation);
    QDir().mkpath(activityRoot);
    const QString activityDbPath =
        QDir(activityRoot).filePath(
            QStringLiteral("activity.sqlite"));

    if (tagged) {
        const QString root =
            QStandardPaths::writableLocation(
                QStandardPaths::AppDataLocation);
        QDir().mkpath(root);

        progress.backend = Backend::Ini;
        progress.iniPath =
            QDir(root).filePath(
                QStringLiteral("progress-store.ini"));

        collection.backend = Backend::Ini;
        collection.iniPath =
            QDir(root).filePath(
                QStringLiteral("collection-store.ini"));

        searchHistory.backend = Backend::Ini;
        searchHistory.iniPath =
            QDir(root).filePath(
                QStringLiteral("search-history-store.ini"));

        audioPairing.backend =
            Backend::DefaultApplication;
        preferences.backend =
            Backend::DefaultApplication;
        history.backend =
            Backend::DefaultApplication;
    } else {
        progress.backend =
            Backend::BrotherhoodColosseum;
        collection.backend =
            Backend::BrotherhoodColosseum;
        searchHistory.backend =
            Backend::BrotherhoodColosseum;
        audioPairing.backend =
            Backend::DefaultApplication;
        preferences.backend =
            Backend::DefaultApplication;
        history.backend =
            Backend::DefaultApplication;
    }

    return LegacyPersonalStateStorage(
        progress,
        collection,
        searchHistory,
        audioPairing,
        preferences,
        history,
        activityDbPath,
        QDir(activityRoot).filePath(QStringLiteral("profiles/legacy")),
        QStringLiteral("legacy"));
}

LegacyPersonalStateStorage
LegacyPersonalStateStorage::isolated(
    const QString &root) {
    const QString base =
        QDir::cleanPath(
            QFileInfo(root).absoluteFilePath());
    QDir().mkpath(base);

    Location progress;
    progress.backend = Backend::Ini;
    progress.iniPath =
        QDir(base).filePath(
            QStringLiteral("progress-store.ini"));

    Location collection;
    collection.backend = Backend::Ini;
    collection.iniPath =
        QDir(base).filePath(
            QStringLiteral("collection-store.ini"));

    Location searchHistory;
    searchHistory.backend = Backend::Ini;
    searchHistory.iniPath =
        QDir(base).filePath(
            QStringLiteral("search-history-store.ini"));

    Location audioPairing;
    audioPairing.backend = Backend::Ini;
    audioPairing.iniPath =
        QDir(base).filePath(
            QStringLiteral("audio-pairing-store.ini"));

    Location preferences;
    preferences.backend = Backend::Ini;
    preferences.iniPath =
        QDir(base).filePath(
            QStringLiteral("preferences-store.ini"));

    Location history;
    history.backend = Backend::Ini;
    history.iniPath =
        QDir(base).filePath(
            QStringLiteral("history-store.ini"));

    const QString activityDbPath =
        QDir(base).filePath(
            QStringLiteral("activity.sqlite"));

    return LegacyPersonalStateStorage(
        progress,
        collection,
        searchHistory,
        audioPairing,
        preferences,
        history,
        activityDbPath,
        QDir(base).filePath(QStringLiteral("profiles/legacy")),
        QStringLiteral("legacy"));
}

std::optional<LegacyPersonalStateStorage>
LegacyPersonalStateStorage::forProfile(
    const ProfilePaths &paths,
    QString *error) {
    return forProfileRoot(
        paths,
        paths.profileRoot(),
        error);
}

std::optional<LegacyPersonalStateStorage>
LegacyPersonalStateStorage::forProfileRoot(
    const ProfilePaths &paths,
    const QString &profileRoot,
    QString *error) {
    if (paths.kind() == ProfilePaths::Kind::Sealed
        || paths.kind()
            == ProfilePaths::Kind::LegacyLocal) {
        setError(
            error,
            QStringLiteral(
                "Sealed and legacy-local states do not have explicit personal profile files."));
        return std::nullopt;
    }

    const QString root =
        QDir::cleanPath(
            QFileInfo(profileRoot).absoluteFilePath());
    if (root.isEmpty()
        || !paths.isManagedProfilePath(root)) {
        setError(
            error,
            QStringLiteral(
                "The profile root is outside the managed profile directory."));
        return std::nullopt;
    }

    Location progress;
    progress.backend = Backend::Ini;
    progress.iniPath =
        QDir(root).filePath(
            QStringLiteral("progress.ini"));

    Location collection;
    collection.backend = Backend::Ini;
    collection.iniPath =
        QDir(root).filePath(
            QStringLiteral("collection.ini"));

    Location searchHistory;
    searchHistory.backend = Backend::Ini;
    searchHistory.iniPath =
        QDir(root).filePath(
            QStringLiteral("search-history.ini"));

    Location audioPairing;
    audioPairing.backend = Backend::Ini;
    audioPairing.iniPath =
        QDir(root).filePath(
            QStringLiteral("audio-pairing.ini"));

    Location preferences;
    preferences.backend = Backend::Ini;
    preferences.iniPath =
        QDir(root).filePath(
            QStringLiteral("preferences.ini"));

    Location history;
    history.backend = Backend::Ini;
    history.iniPath =
        QDir(root).filePath(
            QStringLiteral("history.ini"));

    const QString activityDbPath =
        QDir(root).filePath(
            QStringLiteral("activity.sqlite"));

    return LegacyPersonalStateStorage(
        progress,
        collection,
        searchHistory,
        audioPairing,
        preferences,
        history,
        activityDbPath,
        root,
        paths.profileId());
}

std::optional<PersonalStateSnapshot>
LegacyPersonalStateStorage::capture(
    QString *error) const {
    auto progress = open(m_progress);
    auto collection = open(m_collection);
    auto searchHistory = open(m_searchHistory);
    auto audioPairing = open(m_audioPairing);
    auto preferences = open(m_preferences);
    auto history = open(m_history);

    if (!progress
        || !collection
        || !searchHistory
        || !audioPairing
        || !preferences
        || !history) {
        setError(
            error,
            QStringLiteral(
                "Could not open legacy personal-state persistence."));
        return std::nullopt;
    }

    PersonalStateSnapshot snapshot;
    if (!jsonObjectValue(
            progress.get(),
            QStringLiteral("continue/entries"),
            &snapshot.progressEntries,
            error)) {
        return std::nullopt;
    }

    capturePrefix(
        progress.get(),
        QStringLiteral("video/lastSeason"),
        &snapshot.progressLastSeason);
    capturePrefix(
        progress.get(),
        QStringLiteral("video/watchedMark"),
        &snapshot.progressWatchedMarks);
    capturePrefix(
        progress.get(),
        QStringLiteral("video/watchedMarkActionAt"),
        &snapshot.progressWatchedMarkActionTimes);

    if (!jsonObjectValue(
            collection.get(),
            QStringLiteral("collection/entries"),
            &snapshot.collectionEntries,
            error)) {
        return std::nullopt;
    }

    for (const QString &key :
         searchHistory->allKeys()) {
        const QString prefix =
            QStringLiteral("searchHistory/");
        if (!key.startsWith(prefix))
            continue;

        const QString scope =
            key.mid(prefix.size());
        if (scope.isEmpty())
            continue;

        const QStringList entries =
            cleanSearchEntries(
                searchHistory->value(key));
        if (!entries.isEmpty()) {
            snapshot.searchHistory.insert(
                scope,
                stringListArray(entries));
        }
    }

    if (!jsonObjectValue(
            audioPairing.get(),
            QStringLiteral("audiobook/pairings"),
            &snapshot.audioPairings,
            error)) {
        return std::nullopt;
    }

    if (!jsonObjectValue(
            history.get(),
            QStringLiteral("history/records"),
            &snapshot.historyRecords,
            error)) {
        return std::nullopt;
    }

    snapshot.showExplicit =
        preferences
            ->value(
                QStringLiteral("content/showExplicit"),
                false)
            .toBool();

    const QString provider = preferences
        ->value(QStringLiteral("sync/mainSyncProvider"))
        .toString()
        .trimmed();
    snapshot.mainSyncProvider = provider == QLatin1String("stremio")
        ? provider
        : QString();

    if (!m_profileRoot.isEmpty()) {
        QJsonObject stremio;
        if (!readJsonFile(stremioStatePath(m_profileRoot), &stremio, error))
            return std::nullopt;
        if (!stremio.isEmpty()) {
            if (stremio.value(QStringLiteral("version")).toInt() != 1
                || stremio.value(QStringLiteral("profileId")).toString() != m_profileId
                || stremio.value(QStringLiteral("accountId")).toString().trimmed().isEmpty()
                || !stremio.value(QStringLiteral("acknowledgedBaselines")).isObject()
                || !stremio.value(QStringLiteral("importRedoReceipts")).isArray()
                || !stremio.value(QStringLiteral("intentionalMembershipDifferences")).isArray()
                || !stremio.value(QStringLiteral("pendingIntents")).isArray()) {
                setError(error, QStringLiteral("The device-private Stremio journal is invalid."));
                return std::nullopt;
            }
            // Profile ids identify the current on-disk binding, not the
            // private provider state itself. Adoption rebinds them to the
            // verified destination, so omit them from the semantic snapshot.
            stremio.insert(QStringLiteral("profileId"), QString());
            QJsonArray redos;
            for (const QJsonValue &value : stremio.value(
                     QStringLiteral("importRedoReceipts")).toArray()) {
                if (!value.isObject()) {
                    redos.append(value);
                    continue;
                }
                QJsonObject redo = value.toObject();
                redo.insert(QStringLiteral("profileId"), QString());
                redos.append(redo);
            }
            stremio.insert(QStringLiteral("importRedoReceipts"), redos);
            snapshot.stremioState = stremio;
        }

        QJsonObject extensions;
        if (!readJsonFile(theatreExtensionsPath(m_profileRoot), &extensions, error))
            return std::nullopt;
        if (!extensions.isEmpty()) {
            const QJsonValue rows = extensions.value(QStringLiteral("extensions"));
            if (extensions.value(QStringLiteral("version")).toInt() != 1
                || extensions.value(QStringLiteral("profileId")).toString() != m_profileId
                || !rows.isArray()
                || rows.toArray().size() > kMaximumTheatreExtensions) {
                setError(error, QStringLiteral("The private Theatre extension store is invalid."));
                return std::nullopt;
            }
            snapshot.theatreExtensions = rows.toArray();
        }
    }

    return snapshot;
}

bool LegacyPersonalStateStorage::clearPersonalState(
    QString *error) const {
    auto progress = open(m_progress);
    auto collection = open(m_collection);
    auto searchHistory = open(m_searchHistory);
    auto audioPairing = open(m_audioPairing);
    auto preferences = open(m_preferences);
    auto history = open(m_history);

    if (!progress
        || !collection
        || !searchHistory
        || !audioPairing
        || !preferences
        || !history) {
        return setError(
            error,
            QStringLiteral(
                "Could not open legacy personal-state persistence."));
    }

    progress->remove(
        QStringLiteral("continue"));
    progress->remove(
        QStringLiteral("video/lastSeason"));
    progress->remove(
        QStringLiteral("video/watchedMark"));
    progress->remove(
        QStringLiteral("video/watchedMarkActionAt"));

    collection->remove(
        QStringLiteral("collection/entries"));

    searchHistory->remove(
        QStringLiteral("searchHistory"));

    audioPairing->remove(
        QStringLiteral("audiobook/pairings"));
    preferences->remove(
        QStringLiteral("content/showExplicit"));
    preferences->remove(
        QStringLiteral("sync/mainSyncProvider"));
    history->remove(
        QStringLiteral("history/records"));

    const bool settingsCommitted = sync(progress.get(), error)
        && sync(collection.get(), error)
        && sync(searchHistory.get(), error)
        && sync(audioPairing.get(), error)
        && sync(preferences.get(), error)
        && sync(history.get(), error);
    if (!settingsCommitted)
        return false;
    return removePrivateFile(stremioStatePath(m_profileRoot), error)
        && removePrivateFile(theatreExtensionsPath(m_profileRoot), error);
}

bool LegacyPersonalStateStorage::restorePersonalState(
    const PersonalStateSnapshot &snapshot,
    QString *error) const {
    auto progress = open(m_progress);
    auto collection = open(m_collection);
    auto searchHistory = open(m_searchHistory);
    auto audioPairing = open(m_audioPairing);
    auto preferences = open(m_preferences);
    auto history = open(m_history);

    if (!progress
        || !collection
        || !searchHistory
        || !audioPairing
        || !preferences
        || !history) {
        return setError(
            error,
            QStringLiteral(
                "Could not open legacy personal-state persistence."));
    }

    progress->remove(
        QStringLiteral("continue"));
    progress->remove(
        QStringLiteral("video/lastSeason"));
    progress->remove(
        QStringLiteral("video/watchedMark"));
    progress->remove(
        QStringLiteral("video/watchedMarkActionAt"));

    if (!snapshot.progressEntries.isEmpty()) {
        progress->setValue(
            QStringLiteral("continue/entries"),
            QJsonDocument(snapshot.progressEntries)
                .toJson(QJsonDocument::Compact));
    }

    for (auto it =
             snapshot.progressLastSeason.constBegin();
         it != snapshot.progressLastSeason.constEnd();
         ++it) {
        progress->setValue(
            QStringLiteral("video/lastSeason/")
                + it.key(),
            it.value().toVariant());
    }

    for (auto it =
             snapshot.progressWatchedMarks.constBegin();
         it != snapshot.progressWatchedMarks.constEnd();
         ++it) {
        progress->setValue(
            QStringLiteral("video/watchedMark/")
                + it.key(),
            it.value().toVariant());
    }

    for (auto it =
             snapshot.progressWatchedMarkActionTimes.constBegin();
         it != snapshot.progressWatchedMarkActionTimes.constEnd();
         ++it) {
        progress->setValue(
            QStringLiteral("video/watchedMarkActionAt/") + it.key(),
            it.value().toVariant());
    }

    collection->remove(
        QStringLiteral("collection/entries"));
    if (!snapshot.collectionEntries.isEmpty()) {
        collection->setValue(
            QStringLiteral("collection/entries"),
            QJsonDocument(snapshot.collectionEntries)
                .toJson(QJsonDocument::Compact));
    }

    searchHistory->remove(
        QStringLiteral("searchHistory"));
    for (auto it =
             snapshot.searchHistory.constBegin();
         it != snapshot.searchHistory.constEnd();
         ++it) {
        searchHistory->setValue(
            QStringLiteral("searchHistory/")
                + it.key(),
            jsonStringList(it.value()));
    }

    audioPairing->remove(
        QStringLiteral("audiobook/pairings"));
    if (!snapshot.audioPairings.isEmpty()) {
        audioPairing->setValue(
            QStringLiteral("audiobook/pairings"),
            QJsonDocument(snapshot.audioPairings)
                .toJson(QJsonDocument::Compact));
    }

    preferences->setValue(
        QStringLiteral("content/showExplicit"),
        snapshot.showExplicit);
    if (snapshot.mainSyncProvider.isEmpty()) {
        preferences->remove(QStringLiteral("sync/mainSyncProvider"));
    } else {
        preferences->setValue(QStringLiteral("sync/mainSyncProvider"),
                              snapshot.mainSyncProvider);
    }

    history->remove(
        QStringLiteral("history/records"));
    if (!snapshot.historyRecords.isEmpty()) {
        history->setValue(
            QStringLiteral("history/records"),
            QJsonDocument(snapshot.historyRecords)
                .toJson(QJsonDocument::Compact));
    }

    const bool settingsCommitted = sync(progress.get(), error)
        && sync(collection.get(), error)
        && sync(searchHistory.get(), error)
        && sync(audioPairing.get(), error)
        && sync(preferences.get(), error)
        && sync(history.get(), error);
    if (!settingsCommitted)
        return false;

    if (!m_profileRoot.isEmpty()) {
        if (snapshot.stremioState.isEmpty()) {
            if (!removePrivateFile(stremioStatePath(m_profileRoot), error))
                return false;
        } else {
            QJsonObject state = snapshot.stremioState;
            state.insert(QStringLiteral("profileId"), m_profileId);
            QJsonArray redos;
            for (const QJsonValue &value : state.value(
                     QStringLiteral("importRedoReceipts")).toArray()) {
                if (!value.isObject()) {
                    redos.append(value);
                    continue;
                }
                QJsonObject redo = value.toObject();
                redo.insert(QStringLiteral("profileId"), m_profileId);
                redos.append(redo);
            }
            state.insert(QStringLiteral("importRedoReceipts"), redos);
            if (!writeJsonFile(stremioStatePath(m_profileRoot), state, error))
                return false;
        }

        if (snapshot.theatreExtensions.isEmpty()) {
            if (!removePrivateFile(theatreExtensionsPath(m_profileRoot), error))
                return false;
        } else {
            const QJsonObject extensions{
                {QStringLiteral("version"), 1},
                {QStringLiteral("profileId"), m_profileId},
                {QStringLiteral("extensions"), snapshot.theatreExtensions}};
            if (!writeJsonFile(theatreExtensionsPath(m_profileRoot), extensions, error))
                return false;
        }
    }
    return true;
}

bool LegacyPersonalStateStorage::progressUsesExplicitIni() const {
    return m_progress.backend == Backend::Ini;
}

bool LegacyPersonalStateStorage::collectionUsesExplicitIni() const {
    return m_collection.backend == Backend::Ini;
}

bool LegacyPersonalStateStorage::searchHistoryUsesExplicitIni() const {
    return m_searchHistory.backend == Backend::Ini;
}

bool LegacyPersonalStateStorage::audioPairingUsesExplicitIni() const {
    return m_audioPairing.backend == Backend::Ini;
}

bool LegacyPersonalStateStorage::preferencesUseExplicitIni() const {
    return m_preferences.backend == Backend::Ini;
}

bool LegacyPersonalStateStorage::historyUsesExplicitIni() const {
    return m_history.backend == Backend::Ini;
}

QString LegacyPersonalStateStorage::progressIniPath() const {
    return m_progress.iniPath;
}

QString LegacyPersonalStateStorage::collectionIniPath() const {
    return m_collection.iniPath;
}

QString LegacyPersonalStateStorage::searchHistoryIniPath() const {
    return m_searchHistory.iniPath;
}

QString LegacyPersonalStateStorage::audioPairingIniPath() const {
    return m_audioPairing.iniPath;
}

QString LegacyPersonalStateStorage::preferencesIniPath() const {
    return m_preferences.iniPath;
}

QString LegacyPersonalStateStorage::historyIniPath() const {
    return m_history.iniPath;
}

QString LegacyPersonalStateStorage::devicePrivateProfileRoot() const {
    return m_profileRoot;
}

QString LegacyPersonalStateStorage::devicePrivateStremioStatePath() const {
    return stremioStatePath(m_profileRoot);
}

QString LegacyPersonalStateStorage::activityDbPath() const {
    return m_activityDbPath;
}

LegacyPersonalStateStorage::LegacyPersonalStateStorage(
    const Location &progress,
    const Location &collection,
    const Location &searchHistory,
    const Location &audioPairing,
    const Location &preferences,
    const Location &history,
    const QString &activityDbPath,
    const QString &profileRoot,
    const QString &profileId)
    : m_progress(progress),
      m_collection(collection),
      m_searchHistory(searchHistory),
      m_audioPairing(audioPairing),
      m_preferences(preferences),
      m_history(history),
      m_activityDbPath(activityDbPath),
      m_profileRoot(profileRoot),
      m_profileId(profileId) {}

std::unique_ptr<QSettings>
LegacyPersonalStateStorage::open(
    const Location &location) {
    switch (location.backend) {
    case Backend::DefaultApplication:
        return std::make_unique<QSettings>();
    case Backend::BrotherhoodColosseum:
        return std::make_unique<QSettings>(
            QStringLiteral("Brotherhood"),
            QStringLiteral("Colosseum"));
    case Backend::Ini:
        return std::make_unique<QSettings>(
            location.iniPath,
            QSettings::IniFormat);
    }
    return {};
}

bool LegacyPersonalStateStorage::sync(
    QSettings *settings,
    QString *error) {
    settings->sync();
    if (settings->status()
        == QSettings::NoError) {
        return true;
    }

    return setError(
        error,
        QStringLiteral(
            "Could not persist legacy personal state."));
}

bool LegacyPersonalStateStorage::setError(
    QString *error,
    const QString &message) {
    if (error)
        *error = message;
    return false;
}
