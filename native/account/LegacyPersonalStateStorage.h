#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QJsonObject>
#include <QSettings>
#include <QString>

#include <memory>
#include <optional>

class ProfilePaths;

struct PersonalStateSnapshot {
    QJsonObject progressEntries;
    QJsonObject progressLastSeason;
    QJsonObject progressWatchedMarks;
    QJsonObject collectionEntries;
    QJsonObject searchHistory;
    QJsonObject audioPairings;
    QJsonObject historyRecords;
    bool showExplicit = false;

    bool isEmpty() const;
    QJsonObject toJson() const;
    QString semanticDigest() const;
    QString legacySemanticDigestV1() const;
    bool matchesSemanticDigest(
        const QString &digest) const;

    static std::optional<PersonalStateSnapshot> fromJson(
        const QJsonObject &object,
        QString *error = nullptr);
};

class LegacyPersonalStateStorage {
public:
    static LegacyPersonalStateStorage forCurrentInstallation();
    static LegacyPersonalStateStorage isolated(
        const QString &root);
    static std::optional<LegacyPersonalStateStorage> forProfile(
        const ProfilePaths &paths,
        QString *error = nullptr);
    static std::optional<LegacyPersonalStateStorage> forProfileRoot(
        const ProfilePaths &paths,
        const QString &profileRoot,
        QString *error = nullptr);

    std::optional<PersonalStateSnapshot> capture(
        QString *error = nullptr) const;

    bool clearPersonalState(
        QString *error = nullptr) const;

    bool restorePersonalState(
        const PersonalStateSnapshot &snapshot,
        QString *error = nullptr) const;

    bool progressUsesExplicitIni() const;
    bool collectionUsesExplicitIni() const;
    bool searchHistoryUsesExplicitIni() const;
    bool audioPairingUsesExplicitIni() const;
    bool preferencesUseExplicitIni() const;
    bool historyUsesExplicitIni() const;

    QString progressIniPath() const;
    QString collectionIniPath() const;
    QString searchHistoryIniPath() const;
    QString audioPairingIniPath() const;
    QString preferencesIniPath() const;
    QString historyIniPath() const;

private:
    enum class Backend {
        DefaultApplication,
        BrotherhoodColosseum,
        Ini
    };

    struct Location {
        Backend backend = Backend::DefaultApplication;
        QString iniPath;
    };

    LegacyPersonalStateStorage(
        const Location &progress,
        const Location &collection,
        const Location &searchHistory,
        const Location &audioPairing,
        const Location &preferences,
        const Location &history);

    static std::unique_ptr<QSettings> open(
        const Location &location);

    static bool sync(
        QSettings *settings,
        QString *error);

    static bool setError(
        QString *error,
        const QString &message);

    Location m_progress;
    Location m_collection;
    Location m_searchHistory;
    Location m_audioPairing;
    Location m_preferences;
    Location m_history;
};
