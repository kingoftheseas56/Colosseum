#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "LegacyPersonalStateStorage.h"
#include "ProfileAdoption.h"
#include "ProfilePaths.h"

#include <QSet>
#include <QString>

#include <optional>

class ProfileStoreRuntime;

class FirstAccountProfileCoordinator final {
public:
    explicit FirstAccountProfileCoordinator(
        ProfileStoreRuntime *profileRuntime,
        const QString &appDataRoot = QString());

    bool prepareCreatedAccount(
        const QString &accountId,
        QString *error = nullptr);

    bool prepareAccountSession(
        const QString &accountId,
        QString *error = nullptr);

    bool prepareRememberedAccount(
        const QString &accountId,
        QString *error = nullptr);

    bool prepareLocalOnly(
        QString *error = nullptr);

private:
    bool runFreshAdoption(
        const ProfilePaths &paths,
        QString *error);

    bool resumeAdoption(
        const ProfilePaths &paths,
        ProfileAdoption adoption,
        QString *error);

    bool finishPromotedAdoption(
        const ProfilePaths &paths,
        ProfileAdoption adoption,
        const PersonalStateSnapshot &source,
        QString *error);

    bool verifyRestartAndCommit(
        const ProfilePaths &paths,
        ProfileAdoption adoption,
        QString *error);

    bool createEmptyProfile(
        const ProfilePaths &paths,
        QString *error);

    bool verifyProfile(
        const ProfilePaths &paths,
        const QString &profileRoot,
        const PersonalStateSnapshot &expected,
        QString *semanticDigest,
        QString *error) const;

    bool writeBackup(
        const ProfilePaths &paths,
        const PersonalStateSnapshot &snapshot,
        QString *error) const;

    std::optional<PersonalStateSnapshot> readBackup(
        const ProfilePaths &paths,
        QString *error) const;

    bool restoreLegacyForRetry(
        const PersonalStateSnapshot &snapshot,
        QString *error);

    bool restoreLegacyAndRollback(
        ProfileAdoption *adoption,
        const PersonalStateSnapshot &snapshot,
        QString *error);

    bool activate(
        const ProfilePaths &paths,
        QString *error);

    std::optional<bool> legacyPersonalStateClaimed(
        QString *error) const;

    static QString backupFilePath(
        const ProfilePaths &paths);
    static QString creationIntentPath(
        const ProfilePaths &paths);

    static bool writeCreationIntent(
        const ProfilePaths &paths,
        QString *error);
    static bool clearCreationIntent(
        const ProfilePaths &paths,
        QString *error = nullptr);
    static bool hasCreationIntent(
        const ProfilePaths &paths);

    static bool setError(
        QString *error,
        const QString &message);

    ProfileStoreRuntime *m_profileRuntime = nullptr;
    QString m_appDataRoot;
    QSet<QString> m_quarantinedThisProcess;
};
