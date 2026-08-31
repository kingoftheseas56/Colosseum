// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "ProfilePaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

namespace {
QString normalizedPath(const QString &path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString accountUuid(const QString &accountId) {
    const QUuid id(accountId.trimmed());
    if (id.isNull())
        return QString();
    return id.toString(QUuid::WithoutBraces).toLower();
}
}

ProfilePaths ProfilePaths::sealed(const QString &appDataRoot) {
    const QString root = resolveAppDataRoot(appDataRoot);
    return ProfilePaths(
        Kind::Sealed,
        QStringLiteral("sealed"),
        root,
        QString(),
        false);
}

ProfilePaths ProfilePaths::legacyLocal() {
    return ProfilePaths(Kind::LegacyLocal,
                        QStringLiteral("legacy"),
                        QString(),
                        QString(),
                        true);
}

ProfilePaths ProfilePaths::localOnly(const QString &appDataRoot) {
    const QString root = resolveAppDataRoot(appDataRoot);
    const QString profileRoot = root
        + QLatin1String("/profiles/local");
    return ProfilePaths(Kind::LocalOnly,
                        QStringLiteral("local"),
                        root,
                        QDir::cleanPath(profileRoot),
                        false);
}

std::optional<ProfilePaths> ProfilePaths::account(const QString &accountId,
                                                  const QString &appDataRoot) {
    const QString id = accountUuid(accountId);
    if (id.isEmpty())
        return std::nullopt;

    const QString root = resolveAppDataRoot(appDataRoot);
    const QString profileRoot = root
        + QLatin1String("/profiles/")
        + id;
    return ProfilePaths(Kind::Account,
                        id,
                        root,
                        QDir::cleanPath(profileRoot),
                        false);
}

ProfilePaths::Kind ProfilePaths::kind() const {
    return m_kind;
}

QString ProfilePaths::profileId() const {
    return m_profileId;
}

QString ProfilePaths::appDataRoot() const {
    return m_appDataRoot;
}

QString ProfilePaths::profileRoot() const {
    return m_profileRoot;
}

bool ProfilePaths::usesLegacySettings() const {
    return m_usesLegacySettings;
}

QString ProfilePaths::progressIniPath() const {
    return childPath(QStringLiteral("progress.ini"));
}

QString ProfilePaths::collectionIniPath() const {
    return childPath(QStringLiteral("collection.ini"));
}

QString ProfilePaths::searchHistoryIniPath() const {
    return childPath(QStringLiteral("search-history.ini"));
}

QString ProfilePaths::audioPairingIniPath() const {
    return childPath(QStringLiteral("audio-pairing.ini"));
}

QString ProfilePaths::preferencesIniPath() const {
    return childPath(QStringLiteral("preferences.ini"));
}

QString ProfilePaths::historyIniPath() const {
    return childPath(QStringLiteral("history.ini"));
}

QString ProfilePaths::activityDbPath() const {
    return childPath(QStringLiteral("activity.sqlite"));
}

QString ProfilePaths::syncStatePath() const {
    return childPath(QStringLiteral("sync/state.json"));
}

QString ProfilePaths::syncOutboxPath() const {
    return childPath(QStringLiteral("sync/outbox.sqlite"));
}

QString ProfilePaths::syncMetaIniPath() const {
    return childPath(QStringLiteral("sync/meta.ini"));
}

QString ProfilePaths::accountStagingRoot() const {
    if (m_kind != Kind::Account)
        return QString();
    return QDir::cleanPath(m_profileRoot + QLatin1String(".adopting"));
}

QString ProfilePaths::accountReplacementBackupRoot() const {
    if (m_kind != Kind::Account)
        return QString();
    return QDir::cleanPath(m_profileRoot + QLatin1String(".pre-attachment"));
}

QString ProfilePaths::adoptionJournalPath() const {
    if (m_kind != Kind::Account)
        return QString();
    return QDir::cleanPath(m_appDataRoot
                           + QLatin1String("/profile-adoption/")
                           + m_profileId
                           + QLatin1String(".json"));
}

QString ProfilePaths::localAttachmentJournalPath() const {
    if (m_kind != Kind::Account)
        return QString();
    return QDir::cleanPath(m_appDataRoot
                           + QLatin1String("/profile-attachment/")
                           + m_profileId
                           + QLatin1String(".json"));
}

QString ProfilePaths::adoptionBackupRoot() const {
    if (m_kind != Kind::Account)
        return QString();
    return QDir::cleanPath(m_appDataRoot
                           + QLatin1String("/profile-adoption/backups/")
                           + m_profileId
                           + QLatin1String(".legacy"));
}

bool ProfilePaths::isManagedProfilePath(const QString &path) const {
    if (m_kind == Kind::Sealed
        || m_kind == Kind::LegacyLocal
        || m_profileRoot.isEmpty()
        || path.trimmed().isEmpty()) {
        return false;
    }

    const QString profileBase = normalizedPath(m_appDataRoot + QLatin1String("/profiles"));
    const QString candidate = normalizedPath(path);
    return candidate.startsWith(profileBase + QLatin1Char('/'));
}

bool ProfilePaths::isValidAccountId(const QString &accountId) {
    return !accountUuid(accountId).isEmpty();
}

ProfilePaths::ProfilePaths(Kind kind,
                           const QString &profileId,
                           const QString &appDataRoot,
                           const QString &profileRoot,
                           bool usesLegacySettings)
    : m_kind(kind),
      m_profileId(profileId),
      m_appDataRoot(appDataRoot),
      m_profileRoot(profileRoot),
      m_usesLegacySettings(usesLegacySettings) {}

QString ProfilePaths::resolveAppDataRoot(const QString &appDataRoot) {
    const QString explicitRoot = appDataRoot.trimmed();
    if (!explicitRoot.isEmpty())
        return QDir::cleanPath(QFileInfo(explicitRoot).absoluteFilePath());

    return QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

QString ProfilePaths::childPath(const QString &relativePath) const {
    if (m_kind == Kind::Sealed
        || m_kind == Kind::LegacyLocal
        || m_profileRoot.isEmpty()) {
        return QString();
    }
    return QDir::cleanPath(m_profileRoot + QLatin1Char('/') + relativePath);
}
