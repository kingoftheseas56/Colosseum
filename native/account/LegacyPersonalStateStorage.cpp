// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "LegacyPersonalStateStorage.h"

#include "ProfilePaths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaType>
#include <QVariant>
#include <QSettings>
#include <QStandardPaths>

#include <memory>

namespace {
constexpr int kSnapshotVersion = 2;

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

        // The captured prefixes are integer domains (lastSeason, watched
        // marks), but INI persistence is stringly-typed on disk while an
        // in-memory QSettings session still holds the typed QVariant. A
        // fresh re-open therefore yields "3" where the writer saw 3. The
        // semantic digest must be representation-independent, so normalize
        // clean integer spellings to numbers at the capture boundary.
        if (value.isString()) {
            bool ok = false;
            const int number =
                raw.toString().toInt(&ok);
            if (ok)
                value = number;
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
        && collectionEntries.isEmpty()
        && searchHistory.isEmpty()
        && audioPairings.isEmpty()
        && historyRecords.isEmpty()
        && !showExplicit;
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

    return historyRecords.isEmpty()
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
    if (version != 1
        && version != kSnapshotVersion) {
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
        history);
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

    return LegacyPersonalStateStorage(
        progress,
        collection,
        searchHistory,
        audioPairing,
        preferences,
        history);
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

    return LegacyPersonalStateStorage(
        progress,
        collection,
        searchHistory,
        audioPairing,
        preferences,
        history);
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

    collection->remove(
        QStringLiteral("collection/entries"));

    searchHistory->remove(
        QStringLiteral("searchHistory"));

    audioPairing->remove(
        QStringLiteral("audiobook/pairings"));
    preferences->remove(
        QStringLiteral("content/showExplicit"));
    history->remove(
        QStringLiteral("history/records"));

    return sync(progress.get(), error)
        && sync(collection.get(), error)
        && sync(searchHistory.get(), error)
        && sync(audioPairing.get(), error)
        && sync(preferences.get(), error)
        && sync(history.get(), error);
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

    history->remove(
        QStringLiteral("history/records"));
    if (!snapshot.historyRecords.isEmpty()) {
        history->setValue(
            QStringLiteral("history/records"),
            QJsonDocument(snapshot.historyRecords)
                .toJson(QJsonDocument::Compact));
    }

    return sync(progress.get(), error)
        && sync(collection.get(), error)
        && sync(searchHistory.get(), error)
        && sync(audioPairing.get(), error)
        && sync(preferences.get(), error)
        && sync(history.get(), error);
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

LegacyPersonalStateStorage::LegacyPersonalStateStorage(
    const Location &progress,
    const Location &collection,
    const Location &searchHistory,
    const Location &audioPairing,
    const Location &preferences,
    const Location &history)
    : m_progress(progress),
      m_collection(collection),
      m_searchHistory(searchHistory),
      m_audioPairing(audioPairing),
      m_preferences(preferences),
      m_history(history) {}

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
