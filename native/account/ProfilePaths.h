#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include <QString>

#include <optional>

class ProfilePaths {
public:
    enum class Kind {
        Sealed,
        LegacyLocal,
        LocalOnly,
        Account
    };

    static ProfilePaths sealed(const QString &appDataRoot = QString());
    static ProfilePaths legacyLocal();
    static ProfilePaths localOnly(const QString &appDataRoot = QString());
    static std::optional<ProfilePaths> account(const QString &accountId,
                                               const QString &appDataRoot = QString());

    Kind kind() const;
    QString profileId() const;
    QString appDataRoot() const;
    QString profileRoot() const;
    bool usesLegacySettings() const;

    QString progressIniPath() const;
    QString collectionIniPath() const;
    QString searchHistoryIniPath() const;
    QString audioPairingIniPath() const;
    QString preferencesIniPath() const;
    QString historyIniPath() const;
    QString activityDbPath() const;
    QString syncStatePath() const;
    QString syncOutboxPath() const;
    QString syncMetaIniPath() const;

    QString accountStagingRoot() const;
    QString adoptionJournalPath() const;
    QString adoptionBackupRoot() const;

    bool isManagedProfilePath(const QString &path) const;

    static bool isValidAccountId(const QString &accountId);

private:
    ProfilePaths(Kind kind,
                 const QString &profileId,
                 const QString &appDataRoot,
                 const QString &profileRoot,
                 bool usesLegacySettings);

    static QString resolveAppDataRoot(const QString &appDataRoot);
    QString childPath(const QString &relativePath) const;

    Kind m_kind = Kind::LegacyLocal;
    QString m_profileId;
    QString m_appDataRoot;
    QString m_profileRoot;
    bool m_usesLegacySettings = true;
};
