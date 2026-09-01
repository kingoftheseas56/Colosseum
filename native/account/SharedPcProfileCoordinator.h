#pragma once

// PRE-FLIGHT DRAFT STATUS: uncompiled / untested / unexecuted / unadopted / unverified.

#include "AccountProfileCoordinator.h"
#include "FirstAccountProfileCoordinator.h"

#include <QString>

#include <functional>
#include <optional>

class ProfileStoreRuntime;

class SharedPcProfileCoordinator final
    : public AccountProfileCoordinator {
public:
    // Arc 36 Wave 4B lane N-17: the LOCAL half of an existing-account
    // attachment. Reported after FirstAccountProfileCoordinator's local
    // merge commits and the account profile activates; the embedder
    // (AccountRuntime) turns it into an AccountAttachmentCoordinator network
    // flow once — and only once — an authenticated account session exists.
    // The digests are derived exactly the way the local merge derives them:
    // LegacyPersonalStateStorage::capture's semantic digest, and
    // ActivityStore::semanticEventDigest where the empty string is the
    // "source had no durable Activity ledger" sentinel.
    struct LocalAttachmentIdentity {
        QString accountId;
        QString sourceKind;      // legacy_local | local_only
        QString sourceProfileId;
        QString sourceSemanticDigest;
        QString sourceActivityDigest;
    };

    explicit SharedPcProfileCoordinator(
        ProfileStoreRuntime *profileRuntime,
        const QString &appDataRoot = QString());

    bool prepareCreatedAccount(
        const QString &accountId,
        QString *error = nullptr) override;

    bool prepareAccountSession(
        const QString &accountId,
        QString *error = nullptr) override;

    bool attachLocalProfileToAccount(
        const QString &accountId,
        QString *error = nullptr) override;

    bool prepareRememberedAccount(
        const QString &accountId,
        QString *error = nullptr) override;

    bool prepareLocalOnly(
        QString *error = nullptr) override;

    bool sealAccountSession(
        const QString &accountId,
        QString *error = nullptr) override;

    // Fail-closed seal guard (frozen Wave 4B contract): while the probe
    // reports an in-flight cloud attachment, sealAccountSession refuses —
    // sealing mid-flow would tear down the stores the flow's verification
    // gate reads and strand a local source that was never verified into the
    // cloud. A null probe (tests, non-runtime embedders) keeps the historic
    // unguarded behavior.
    void setAttachmentInFlightProbe(
        std::function<bool()> probe);

    // Moves out the identity of the last successful local merge that has
    // not yet been consumed by a network attachment flow. Reported exactly
    // once; any other lifecycle transition voids a still-pending identity.
    std::optional<LocalAttachmentIdentity>
    takePendingLocalAttachment();

private:
    std::optional<LocalAttachmentIdentity>
    captureLocalAttachmentIdentity(
        const QString &accountId,
        const ProfilePaths &source,
        QString *error) const;

    static bool setError(
        QString *error,
        const QString &message);

    ProfileStoreRuntime *m_profileRuntime = nullptr;
    FirstAccountProfileCoordinator m_firstAccount;
    std::function<bool()> m_attachmentInFlightProbe;
    std::optional<LocalAttachmentIdentity>
        m_pendingLocalAttachment;
};
